#include "r2_w3_rebind.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx_hal_dma_ex.h"

#include <stddef.h>
#include <stdint.h>

extern ADC_HandleTypeDef hadc1;
extern DMA_HandleTypeDef hdma_adc1;
extern TIM_HandleTypeDef htim2;

#define W3_MAGIC 0x52325733U
#define W3_STACK_WORDS 768U
#define W3_DMA_ERRORS (DMA_LISR_TEIF0 | DMA_LISR_DMEIF0 | DMA_LISR_FEIF0)
#define W3_ALL_FLAGS (W3_DMA_ERRORS | DMA_LISR_TCIF0 | DMA_LISR_HTIF0)
#define W3_GUARD_LO 0x13579BDFU
#define W3_GUARD_HI 0x2468ACE0U
#define W3_SENTINEL 0xA55AU
#define W3_RUN_TIMEOUT_MS 100U
#define W3_SETTLE_CYCLES 1200U
#define W3_DMA_REQUIRED_CR 0x00062D17U
#define W3_ADC_REQUIRED_CR2 0x16000701U

typedef struct
{
    volatile uint32_t guard_lo;
    volatile uint16_t samples[R2_W3_BLOCK_SAMPLES];
    volatile uint32_t guard_hi;
} W3_Buffer;

/* Only these ten physical r2_w3_buffers exist in the W3 profile. R1 driver and
 * R1 harness are excluded by CMake; their source files remain unmodified. */
static W3_Buffer r2_w3_buffers[R2_W3_BUFFERS] __attribute__((aligned(4)));
static StaticTask_t task_control;
static StackType_t task_stack[W3_STACK_WORDS];
static uint32_t irq_flags;
static uint32_t irq_ct;
static uint32_t irq_enter_cycle;
static uint32_t trace_for_exit;
volatile R2_W3_Result g_r2_w3_result;

static uint32_t Address(uint32_t id)
{
    return (uint32_t)(uintptr_t)&r2_w3_buffers[id].samples[0];
}

static uint32_t CurrentCt(void)
{
    return (DMA2_Stream0->CR & DMA_SxCR_CT) != 0U ? 1U : 0U;
}

static void DisableDmaInterrupts(void)
{
    CLEAR_BIT(DMA2_Stream0->CR,
        DMA_SxCR_TCIE | DMA_SxCR_HTIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE);
    CLEAR_BIT(DMA2_Stream0->FCR, DMA_SxFCR_FEIE);
}

static void LatchFault(uint32_t bits)
{
    /* Latch first evidence before clearing flags. No blocking HAL call or
     * RTOS API here. The task later performs hardware quiescence. */
    if (g_r2_w3_result.fault_bits == 0U)
    {
        g_r2_w3_result.first_fault_cycle = DWT->CYCCNT;
        g_r2_w3_result.first_fault_dma_cr = DMA2_Stream0->CR;
        g_r2_w3_result.first_fault_ndtr = DMA2_Stream0->NDTR;
        g_r2_w3_result.first_fault_lisr = DMA2->LISR;
        g_r2_w3_result.first_fault_adc_sr = ADC1->SR;
    }
    g_r2_w3_result.fault_bits |= bits;
    g_r2_w3_result.test_pass = 0U;
    g_r2_w3_result.phase = R2_W3_QUIESCING;
    CLEAR_BIT(TIM2->CR1, TIM_CR1_CEN);
    DisableDmaInterrupts();
    __DSB();
}

static uint32_t HardwareHealthy(void)
{
    uint32_t errors = DMA2->LISR & W3_DMA_ERRORS;
    uint32_t ovr = ADC1->SR & ADC_SR_OVR;
    g_r2_w3_result.dma_error_flags_seen |= errors;
    g_r2_w3_result.adc_ovr_seen |= ovr;
    return (errors == 0U) && (ovr == 0U) &&
        ((DMA2_Stream0->CR & ~DMA_SxCR_CT) == W3_DMA_REQUIRED_CR) &&
        ((TIM2->CR1 & TIM_CR1_CEN) != 0U) &&
        (ADC1->CR2 == W3_ADC_REQUIRED_CR2);
}

void R2_W3_IrqEnter(uint32_t dma_lisr)
{
    irq_enter_cycle = DWT->CYCCNT;
    irq_flags = dma_lisr;
    irq_ct = CurrentCt();
    trace_for_exit = R2_W3_EVENTS;
    ++g_r2_w3_result.irq_count;
    if (g_r2_w3_result.phase != R2_W3_RUNNING)
    {
        ++g_r2_w3_result.suppressed_stop_irqs;
        DisableDmaInterrupts();
        return;
    }
    g_r2_w3_result.dma_error_flags_seen |= dma_lisr & W3_DMA_ERRORS;
    g_r2_w3_result.adc_ovr_seen |= ADC1->SR & ADC_SR_OVR;
    if ((dma_lisr & W3_DMA_ERRORS) != 0U)
    {
        LatchFault(R2_W3_FAULT_DMA);
    }
    else if ((ADC1->SR & ADC_SR_OVR) != 0U)
    {
        LatchFault(R2_W3_FAULT_OVR);
    }
    else if ((dma_lisr & DMA_LISR_TCIF0) == 0U)
    {
        LatchFault(R2_W3_FAULT_EVENT);
    }
}

static void Completed(DMA_HandleTypeDef *dma, uint32_t completed_slot)
{
    uint32_t sequence;
    uint32_t replacement;
    uint32_t active_address;
    uint32_t critical_start;
    uint32_t primask;
    uint32_t now;
    uint32_t elapsed;
    uint32_t ct_after;
    uint32_t healthy;
    uint32_t nominal;
    R2_BufferState completed_owner;
    R2_BufferState replacement_owner;
    R2_DmaSlotsObservation observation;
    R2_DmaSlotsRebindPlan plan;
    R2_DmaSlotsSnapshot mapping;
    R2_BufferPoolSnapshot ownership;
    volatile R2_W3_Trace *t;
    volatile uint32_t *inactive_address_register;
    volatile uint32_t *active_address_register;

    if ((dma != &hdma_adc1) ||
        (g_r2_w3_result.phase != R2_W3_RUNNING))
    {
        LatchFault(R2_W3_FAULT_EVENT);
        return;
    }
    sequence = g_r2_w3_result.full_tc_count + 1U;
    if ((sequence > R2_W3_EVENTS) ||
        ((irq_flags & DMA_LISR_TCIF0) == 0U) ||
        (irq_ct != (sequence & 1U)) || (completed_slot != (irq_ct ^ 1U)))
    {
        LatchFault(R2_W3_FAULT_EVENT);
        return;
    }
    g_r2_w3_result.full_tc_count = sequence;
    trace_for_exit = sequence - 1U;
    t = &g_r2_w3_result.trace[trace_for_exit];
    nominal = sequence * R2_W3_BLOCK_CYCLES;
    t->sequence = sequence;
    t->ct_entry = irq_ct;
    t->completed_slot = completed_slot;
    t->lisr_entry = irq_flags;
    t->nominal_offset_cycles = nominal;
    t->irq_entry_offset_cycles = irq_enter_cycle - g_r2_w3_result.epoch_before;
    t->m0_before = DMA2_Stream0->M0AR;
    t->m1_before = DMA2_Stream0->M1AR;

    /* W3-only deterministic list B2..B9, consumed once, no recycling and no
     * concurrent consumer. This is NOT a replacement FreeBufferQueue. */
    replacement = sequence + 1U;
    if ((R2_DmaSlots_ObserveCt(irq_ct, &observation) != R2_DMA_SLOTS_OK) ||
        ((uint32_t)observation.inactive_slot != completed_slot) ||
        ((uint32_t)observation.completed_buffer != sequence - 1U) ||
        (R2_DmaSlots_GetSnapshot(&mapping) != R2_DMA_SLOTS_OK) ||
        (R2_BufferPool_GetState(observation.completed_buffer,
            &completed_owner) != R2_BUFFER_POOL_OK) ||
        (R2_BufferPool_GetState((R2_BufferId)replacement,
            &replacement_owner) != R2_BUFFER_POOL_OK) ||
        (completed_owner != R2_BUFFER_STATE_DMA_OWNED) ||
        (replacement_owner != R2_BUFFER_STATE_FREE) ||
        (R2_DmaSlots_PrepareInactiveRebind(irq_ct,
            (R2_BufferId)replacement, &plan) != R2_DMA_SLOTS_OK))
    {
        LatchFault(R2_W3_FAULT_MODEL);
        return;
    }
    t->completed_id = observation.completed_buffer;
    t->replacement_id = replacement;
    t->decision_offset_cycles = DWT->CYCCNT - g_r2_w3_result.epoch_before;
    if ((t->m0_before != Address(mapping.m0_buffer)) ||
        (t->m1_before != Address(mapping.m1_buffer)))
    {
        LatchFault(R2_W3_FAULT_ADDRESS);
        return;
    }
    inactive_address_register = completed_slot == 0U ?
        &DMA2_Stream0->M0AR : &DMA2_Stream0->M1AR;
    active_address_register = completed_slot == 0U ?
        &DMA2_Stream0->M1AR : &DMA2_Stream0->M0AR;
    active_address = Address(observation.active_buffer);

    /* Short, nonblocking final check/write/readback only. Masking interrupts
     * does NOT stop DMA: the independent epoch-derived window is mandatory.
     * The budget includes preemption before this section. No UART, queue,
     * allocation, payload scan, retry, or HAL call inside this section. */
    primask = __get_PRIMASK();
    __disable_irq();
    __DSB();
    critical_start = DWT->CYCCNT;
    t->ct_prewrite = CurrentCt();
    t->ndtr_prewrite = DMA2_Stream0->NDTR;
    healthy = HardwareHealthy();
    if ((g_r2_w3_result.phase != R2_W3_RUNNING) ||
        (R2_DmaSlots_CheckPlanCt(&plan, t->ct_prewrite) != R2_DMA_SLOTS_OK) ||
        (*active_address_register != active_address) ||
        (*inactive_address_register != Address(observation.completed_buffer)))
    {
        LatchFault(R2_W3_FAULT_PRECHECK);
        __set_PRIMASK(primask);
        return;
    }
    now = DWT->CYCCNT;
    elapsed = now - g_r2_w3_result.epoch_before;
    t->prewrite_offset_cycles = elapsed;
    t->guard_status = (uint32_t)R2_W3_CheckWindow(sequence, irq_ct,
        t->ct_prewrite, elapsed, t->ndtr_prewrite, healthy);
    if (t->guard_status != R2_W3_GUARD_OK)
    {
        LatchFault(R2_W3_FAULT_PRECHECK);
        __set_PRIMASK(primask);
        return;
    }

    *inactive_address_register = Address(replacement);
    __DSB();
    t->write_performed = 1U;
    t->readback_ok = *inactive_address_register == Address(replacement) ? 1U : 0U;
    t->active_address_unchanged = *active_address_register == active_address ? 1U : 0U;
    ct_after = CurrentCt();
    t->ct_postwrite = ct_after;
    t->m0_after = DMA2_Stream0->M0AR;
    t->m1_after = DMA2_Stream0->M1AR;
    healthy = HardwareHealthy();
    now = DWT->CYCCNT;
    t->commit_offset_cycles = now - g_r2_w3_result.epoch_before;
    t->final_window_cycles = now - critical_start;
    if ((t->readback_ok == 0U) || (t->active_address_unchanged == 0U) ||
        (ct_after != irq_ct) || (healthy == 0U))
    {
        LatchFault(R2_W3_FAULT_ADDRESS);
    }
    if ((t->final_window_cycles > R2_W3_FINAL_RESERVE_CYCLES) ||
        (t->commit_offset_cycles < nominal) ||
        (t->commit_offset_cycles - nominal > R2_W3_WRITE_LIMIT_CYCLES))
    {
        LatchFault(R2_W3_FAULT_TIMING);
    }
    __set_PRIMASK(primask);
    if (g_r2_w3_result.fault_bits != 0U)
    {
        return;
    }

    /* Hardware commit precedes both software records. A failure here cannot
     * be rolled back safely: latch/stop, never publish or 'repair and retry'. */
    if (R2_DmaSlots_CommitPreparedRebind(&plan) != R2_DMA_SLOTS_OK)
    {
        LatchFault(R2_W3_FAULT_MODEL);
        return;
    }
    t->mapping_committed = 1U;
    if (R2_BufferPool_CommitDmaRotation(observation.completed_buffer,
        (R2_BufferId)replacement) != R2_BUFFER_POOL_OK)
    {
        LatchFault(R2_W3_FAULT_MODEL);
        return;
    }
    t->ownership_committed = 1U;
    if ((R2_DmaSlots_GetSnapshot(&mapping) != R2_DMA_SLOTS_OK) ||
        (R2_BufferPool_GetSnapshot(&ownership) != R2_BUFFER_POOL_OK) ||
        (ownership.states[mapping.m0_buffer] != R2_BUFFER_STATE_DMA_OWNED) ||
        (ownership.states[mapping.m1_buffer] != R2_BUFFER_STATE_DMA_OWNED) ||
        (ownership.free_count != R2_W3_EVENTS - sequence) ||
        (ownership.ready_count != sequence))
    {
        LatchFault(R2_W3_FAULT_MODEL);
        return;
    }
    t->free_after = ownership.free_count;
    t->ready_after = ownership.ready_count;
    t->mapping_epoch_after = mapping.mapping_epoch;
    ++g_r2_w3_result.rebind_count;
    if (t->commit_offset_cycles - nominal > g_r2_w3_result.max_nominal_to_commit_cycles)
    {
        g_r2_w3_result.max_nominal_to_commit_cycles = t->commit_offset_cycles - nominal;
    }
    if (t->final_window_cycles > g_r2_w3_result.max_final_window_cycles)
    {
        g_r2_w3_result.max_final_window_cycles = t->final_window_cycles;
    }
    if (sequence == R2_W3_EVENTS)
    {
        /* Stop before a ninth TC. No FREE-exhaustion/drop claim in W3. */
        g_r2_w3_result.phase = R2_W3_QUIESCING;
        CLEAR_BIT(TIM2->CR1, TIM_CR1_CEN);
        __DSB();
        g_r2_w3_result.bounded_stop_requested = 1U;
    }
}

static void M0Complete(DMA_HandleTypeDef *dma) { Completed(dma, 0U); }
static void M1Complete(DMA_HandleTypeDef *dma) { Completed(dma, 1U); }
static void DmaError(DMA_HandleTypeDef *dma)
{
    (void)dma;
    LatchFault(R2_W3_FAULT_DMA);
}

void R2_W3_IrqExit(void)
{
    uint32_t now = DWT->CYCCNT;
    uint32_t offset = now - g_r2_w3_result.epoch_before;
    if (trace_for_exit < R2_W3_EVENTS)
    {
        volatile R2_W3_Trace *t = &g_r2_w3_result.trace[trace_for_exit];
        t->irq_exit_offset_cycles = offset;
        if ((offset < t->nominal_offset_cycles) ||
            (offset - t->nominal_offset_cycles > R2_W3_EXIT_LIMIT_CYCLES))
        {
            LatchFault(R2_W3_FAULT_TIMING);
        }
        else if (offset - t->nominal_offset_cycles >
                 g_r2_w3_result.max_nominal_to_irq_exit_cycles)
        {
            g_r2_w3_result.max_nominal_to_irq_exit_cycles = offset - t->nominal_offset_cycles;
        }
    }
    /* Timestamp is before this hook's final comparison/return and exception
     * epilogue. It is a diagnostic exit marker, not full R4 accounting. */
}

static int ConfigurationValid(void)
{
    uint32_t id;
    if ((SystemCoreClock != 180000000U) ||
        (HAL_RCC_GetPCLK1Freq() != 45000000U) ||
        (HAL_RCC_GetPCLK2Freq() != 90000000U) ||
        (HAL_NVIC_GetPriorityGrouping() != NVIC_PRIORITYGROUP_4) ||
        (hadc1.Instance != ADC1) || (htim2.Instance != TIM2) ||
        (hdma_adc1.Instance != DMA2_Stream0) ||
        (hadc1.DMA_Handle != &hdma_adc1) ||
        (hdma_adc1.State != HAL_DMA_STATE_READY) ||
        ((DMA2_Stream0->CR & DMA_SxCR_EN) != 0U) ||
        ((TIM2->CR1 & TIM_CR1_CEN) != 0U) ||
        (TIM2->PSC != 0U) || (TIM2->ARR != 449U) ||
        ((TIM2->CR2 & TIM_CR2_MMS) != TIM_TRGO_UPDATE) ||
        (TIM2->DIER != 0U) ||
        (hadc1.Init.ClockPrescaler != ADC_CLOCK_SYNC_PCLK_DIV4) ||
        (hadc1.Init.Resolution != ADC_RESOLUTION_12B) ||
        (hadc1.Init.ContinuousConvMode != DISABLE) ||
        (hadc1.Init.DMAContinuousRequests != ENABLE) ||
        (hadc1.Init.ExternalTrigConv != ADC_EXTERNALTRIGCONV_T2_TRGO) ||
        (hadc1.Init.ExternalTrigConvEdge != ADC_EXTERNALTRIGCONVEDGE_RISING) ||
        ((ADC1->CR1 & (ADC_CR1_EOCIE | ADC_CR1_OVRIE)) != 0U) ||
        (ADC1->SQR1 != 0U) || (ADC1->SQR3 != 0U) ||
        ((ADC1->SMPR2 & ADC_SMPR2_SMP0) != ADC_SAMPLETIME_28CYCLES) ||
        (NVIC_GetPriority(DMA2_Stream0_IRQn) != 5U))
    {
        return 0;
    }
    for (id = 0U; id < R2_W3_BUFFERS; ++id)
    {
        uint32_t address = Address(id);
        if ((address & 3U) != 0U || address < 0x20000000U ||
            address > 0x20020000U - 2U * R2_W3_BLOCK_SAMPLES)
        {
            return 0;
        }
    }
    return 1;
}

static HAL_StatusTypeDef Start(void)
{
    HAL_StatusTypeDef status;
    uint32_t primask;
    (void)HAL_TIM_Base_Stop(&htim2);
    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);
    CLEAR_BIT(DMA2_Stream0->CR, DMA_SxCR_DBM | DMA_SxCR_CT);
    DisableDmaInterrupts();
    DMA2->LIFCR = W3_ALL_FLAGS;
    HAL_NVIC_ClearPendingIRQ(DMA2_Stream0_IRQn);
    hdma_adc1.XferCpltCallback = M0Complete;
    hdma_adc1.XferM1CpltCallback = M1Complete;
    hdma_adc1.XferHalfCpltCallback = NULL;
    hdma_adc1.XferM1HalfCpltCallback = NULL;
    hdma_adc1.XferErrorCallback = DmaError;
    hdma_adc1.XferAbortCallback = NULL;
    status = HAL_DMAEx_MultiBufferStart_IT(&hdma_adc1,
        (uint32_t)(uintptr_t)&ADC1->DR, Address(0U), Address(1U),
        R2_W3_BLOCK_SAMPLES);
    if (status != HAL_OK) { return status; }
    __HAL_DMA_DISABLE_IT(&hdma_adc1, DMA_IT_HT);
    __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_EOC | ADC_FLAG_OVR);
    status = HAL_ADC_Start(&hadc1);
    if (status != HAL_OK) { return status; }
    SET_BIT(ADC1->CR2, ADC_CR2_DMA);
    g_r2_w3_result.start_dma_cr = DMA2_Stream0->CR;
    g_r2_w3_result.start_dma_ndtr = DMA2_Stream0->NDTR;
    g_r2_w3_result.start_adc_cr2 = ADC1->CR2;
    g_r2_w3_result.start_tim2_cr1 = TIM2->CR1;
    if ((DMA2_Stream0->CR != W3_DMA_REQUIRED_CR) ||
        (DMA2_Stream0->NDTR != R2_W3_BLOCK_SAMPLES) ||
        (DMA2_Stream0->M0AR != Address(0U)) ||
        (DMA2_Stream0->M1AR != Address(1U)) ||
        (ADC1->CR2 != W3_ADC_REQUIRED_CR2) ||
        ((TIM2->CR1 & TIM_CR1_CEN) != 0U))
    {
        return HAL_ERROR;
    }
    g_r2_w3_result.phase = R2_W3_ARMED;
    primask = __get_PRIMASK();
    __disable_irq();
    g_r2_w3_result.phase = R2_W3_RUNNING;
    g_r2_w3_result.epoch_before = DWT->CYCCNT;
    status = HAL_TIM_Base_Start(&htim2);
    __DSB();
    g_r2_w3_result.epoch_after = DWT->CYCCNT;
    __set_PRIMASK(primask);
    return status;
}

static HAL_StatusTypeDef Stop(void)
{
    uint32_t begin;
    HAL_StatusTypeDef status;
    g_r2_w3_result.phase = R2_W3_QUIESCING;
    (void)HAL_TIM_Base_Stop(&htim2);
    DisableDmaInterrupts();
    begin = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - begin) < W3_SETTLE_CYCLES) { __NOP(); }
    g_r2_w3_result.stop_ct = CurrentCt();
    g_r2_w3_result.stop_ndtr = DMA2_Stream0->NDTR;
    g_r2_w3_result.stop_lisr_before_abort = DMA2->LISR;
    g_r2_w3_result.dma_error_flags_seen |= DMA2->LISR & W3_DMA_ERRORS;
    g_r2_w3_result.adc_ovr_seen |= ADC1->SR & ADC_SR_OVR;
    status = HAL_ADC_Stop_DMA(&hadc1);
    g_r2_w3_result.stop_lisr_after_abort = DMA2->LISR;
    g_r2_w3_result.stop_dma_cr = DMA2_Stream0->CR;
    g_r2_w3_result.stop_adc_cr2 = ADC1->CR2;
    g_r2_w3_result.stop_tim2_cr1 = TIM2->CR1;
    if ((DMA2_Stream0->CR & DMA_SxCR_EN) != 0U) { return HAL_ERROR; }
    DMA2->LIFCR = W3_ALL_FLAGS;
    HAL_NVIC_ClearPendingIRQ(DMA2_Stream0_IRQn);
    __DSB();
    return status;
}

static void Finalize(void)
{
    R2_BufferPoolSnapshot pool;
    R2_DmaSlotsSnapshot slots;
    uint32_t id;
    uint32_t j;
    if ((R2_BufferPool_GetSnapshot(&pool) != R2_BUFFER_POOL_OK) ||
        (R2_DmaSlots_GetSnapshot(&slots) != R2_DMA_SLOTS_OK))
    {
        LatchFault(R2_W3_FAULT_MODEL);
        return;
    }
    g_r2_w3_result.pool_at_stop = pool;
    g_r2_w3_result.slots_at_stop = slots;
    if ((pool.free_count != 0U) || (pool.ready_count != R2_W3_EVENTS) ||
        (pool.processing_count != 0U) || (pool.dma_owned_count != 2U) ||
        (pool.violation_count != 0U) || (slots.violation_count != 0U) ||
        (slots.m0_buffer != 8U) || (slots.m1_buffer != 9U) ||
        (slots.mapping_epoch != 9U))
    {
        LatchFault(R2_W3_FAULT_MODEL);
    }
    for (id = 0U; id < R2_W3_BUFFERS; ++id)
    {
        if ((r2_w3_buffers[id].guard_lo != W3_GUARD_LO) ||
            (r2_w3_buffers[id].guard_hi != W3_GUARD_HI))
        {
            ++g_r2_w3_result.canary_errors;
        }
        if (id < R2_W3_EVENTS)
        {
            uint32_t lo = UINT32_MAX;
            uint32_t hi = 0U;
            for (j = 0U; j < R2_W3_BLOCK_SAMPLES; ++j)
            {
                uint32_t value = r2_w3_buffers[id].samples[j];
                if (value > 4095U) { ++g_r2_w3_result.sample_errors; }
                else { ++g_r2_w3_result.full_sample_count; }
                if (value < lo) { lo = value; }
                if (value > hi) { hi = value; }
            }
            g_r2_w3_result.raw_min[id] = lo;
            g_r2_w3_result.raw_max[id] = hi;
        }
    }
    if ((g_r2_w3_result.canary_errors != 0U) ||
        (g_r2_w3_result.sample_errors != 0U)) { LatchFault(R2_W3_FAULT_SAMPLES); }
    if ((g_r2_w3_result.rebind_count != R2_W3_EVENTS) ||
        (g_r2_w3_result.full_tc_count != R2_W3_EVENTS) ||
        (g_r2_w3_result.bounded_stop_requested != 1U) ||
        (g_r2_w3_result.quiet_tc_count != R2_W3_EVENTS) ||
        (g_r2_w3_result.quiet_rebind_count != R2_W3_EVENTS) ||
        (g_r2_w3_result.dma_error_flags_seen != 0U) ||
        (g_r2_w3_result.adc_ovr_seen != 0U)) { LatchFault(R2_W3_FAULT_EVENT); }
}

static void Task(void *argument)
{
    uint32_t id;
    uint32_t j;
    TickType_t start_tick;
    HAL_StatusTypeDef status;
    R2_BufferPoolStatus pool_status;
    R2_DmaSlotsStatus slot_status;
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(100U));
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    g_r2_w3_result.system_core_clock = SystemCoreClock;
    g_r2_w3_result.aircr = SCB->AIRCR;
    for (id = 0U; id < R2_W3_BUFFERS; ++id)
    {
        r2_w3_buffers[id].guard_lo = W3_GUARD_LO;
        r2_w3_buffers[id].guard_hi = W3_GUARD_HI;
        for (j = 0U; j < R2_W3_BLOCK_SAMPLES; ++j) { r2_w3_buffers[id].samples[j] = W3_SENTINEL; }
        g_r2_w3_result.buffer_address[id] = Address(id);
    }
    R2_BufferPool_Reset();
    R2_DmaSlots_Reset();
    pool_status = R2_BufferPool_Activate(R2_W3_K);
    slot_status = R2_DmaSlots_Initialize((R2_BufferId)0U, (R2_BufferId)1U);
    if (!ConfigurationValid() || pool_status != R2_BUFFER_POOL_OK || slot_status != R2_DMA_SLOTS_OK)
    {
        LatchFault(R2_W3_FAULT_CONFIG);
        g_r2_w3_result.phase = R2_W3_FAILED;
    }
    else
    {
        status = Start();
        g_r2_w3_result.start_status = (uint32_t)status;
        if (status != HAL_OK) { LatchFault(R2_W3_FAULT_START); }
        start_tick = xTaskGetTickCount();
        while (g_r2_w3_result.phase == R2_W3_RUNNING)
        {
            if ((TickType_t)(xTaskGetTickCount() - start_tick) >= pdMS_TO_TICKS(W3_RUN_TIMEOUT_MS))
            {
                LatchFault(R2_W3_FAULT_TIMEOUT);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1U));
        }
        status = Stop();
        g_r2_w3_result.stop_status = (uint32_t)status;
        if ((status != HAL_OK) || ((DMA2_Stream0->CR & DMA_SxCR_EN) != 0U) ||
            ((TIM2->CR1 & TIM_CR1_CEN) != 0U) ||
            ((ADC1->CR2 & (ADC_CR2_ADON | ADC_CR2_DMA)) != 0U))
        {
            LatchFault(R2_W3_FAULT_STOP);
        }
        else
        {
            uint32_t count = g_r2_w3_result.irq_count;
            vTaskDelay(pdMS_TO_TICKS(2U));
            g_r2_w3_result.quiet_tc_count = g_r2_w3_result.full_tc_count;
            g_r2_w3_result.quiet_rebind_count = g_r2_w3_result.rebind_count;
            g_r2_w3_result.quiet_irq_count = g_r2_w3_result.irq_count;
            if (count != g_r2_w3_result.irq_count) { LatchFault(R2_W3_FAULT_EVENT); }
            /* No sample scan until stream EN=0 has been verified. */
            if (g_r2_w3_result.fault_bits == 0U) { Finalize(); }
        }
        g_r2_w3_result.test_pass = g_r2_w3_result.fault_bits == 0U ? 1U : 0U;
        g_r2_w3_result.phase = g_r2_w3_result.test_pass != 0U ? R2_W3_COMPLETE : R2_W3_FAILED;
    }
    for (;;) { vTaskDelay(pdMS_TO_TICKS(1000U)); }
}

void R2_W3_CreateTask(void)
{
    TaskHandle_t handle;
    g_r2_w3_result.magic = W3_MAGIC;
    g_r2_w3_result.start_status = UINT32_MAX;
    g_r2_w3_result.stop_status = UINT32_MAX;
    handle = xTaskCreateStatic(Task, "R2W3", W3_STACK_WORDS, NULL,
        tskIDLE_PRIORITY + 2U, task_stack, &task_control);
    g_r2_w3_result.task_created = handle != NULL ? 1U : 0U;
    g_r2_w3_result.phase = handle != NULL ? R2_W3_TASK_CREATED : R2_W3_FAILED;
    if (handle == NULL) { g_r2_w3_result.fault_bits = R2_W3_FAULT_START; }
}
