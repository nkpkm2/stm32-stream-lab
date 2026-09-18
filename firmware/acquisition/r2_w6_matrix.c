#include "r2_w6_matrix.h"

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "stm32f4xx_hal_dma_ex.h"

#include <stddef.h>
#include <stdint.h>

extern ADC_HandleTypeDef hadc1;
extern DMA_HandleTypeDef hdma_adc1;
extern TIM_HandleTypeDef htim2;

#define W6_MAGIC 0x52325736U
#define W6_CONTROL_STACK_WORDS 768U
#define W6_PROCESS_STACK_WORDS 768U
#define W6_DMA_ERRORS (DMA_LISR_TEIF0 | DMA_LISR_DMEIF0 | DMA_LISR_FEIF0)
#define W6_ALL_FLAGS (W6_DMA_ERRORS | DMA_LISR_TCIF0 | DMA_LISR_HTIF0)
#define W6_GUARD_LO 0x13579BDFU
#define W6_GUARD_HI 0x2468ACE0U
#define W6_SENTINEL 0xA55AU
#ifndef R2_W6_RUN_TIMEOUT_MS
#define R2_W6_RUN_TIMEOUT_MS 1000U
#endif
#define W6_DRAIN_TIMEOUT_MS 500U
#define W6_SETTLE_CYCLES 1200U
#define W6_DMA_REQUIRED_CR 0x00062D17U
#define W6_ADC_REQUIRED_CR2 0x16000701U
#define W6_WORK_BIT (1UL << 0)
#define W6_ALL_BUFFER_MASK ((1UL << R2_W6_BUFFERS) - 1UL)

typedef struct
{
    volatile uint32_t guard_lo;
    volatile uint16_t samples[R2_W6_BLOCK_SAMPLES];
    volatile uint32_t guard_hi;
} W6_Buffer;

typedef struct
{
    uint32_t sequence;
    R2_BufferId buffer_id;
    uint32_t completed_slot;
    uint32_t mapping_epoch;
} W6_BlockDescriptor;

typedef enum
{
    W6_FREE_SEND_NONE = 0,
    W6_FREE_SEND_INIT,
    W6_FREE_SEND_COMPLETE
} W6_FreeSendSource;

typedef struct
{
    volatile uint32_t active;
    volatile uint32_t source;
    volatile uint32_t sequence;
    volatile R2_BufferId buffer_id;
    volatile uint32_t hook_seen;
} W6_FreeSendContext;

static W6_Buffer buffers[R2_W6_BUFFERS] __attribute__((aligned(4)));
static StaticTask_t control_task_control;
static StaticTask_t processing_task_control;
static StackType_t control_task_stack[W6_CONTROL_STACK_WORDS];
static StackType_t processing_task_stack[W6_PROCESS_STACK_WORDS];
static TaskHandle_t processing_task_handle;

static StaticQueue_t free_queue_control;
static StaticQueue_t ready_queue_control;
static uint8_t free_queue_storage[R2_W6_K * sizeof(R2_BufferId)];
static uint8_t ready_queue_storage[R2_W6_K * sizeof(W6_BlockDescriptor)];
static QueueHandle_t free_queue;
static QueueHandle_t ready_queue;

static volatile uint32_t free_token_mask;
static volatile uint32_t ready_token_mask;
static volatile BaseType_t irq_should_yield;
static uint32_t irq_flags;
static uint32_t irq_ct;
static uint32_t irq_enter_cycle;
static uint32_t trace_for_exit;
static W6_FreeSendContext free_send_context;

volatile R2_W6_Result g_r2_w6_result;

static uint32_t Address(uint32_t id)
{
    return (uint32_t)(uintptr_t)&buffers[id].samples[0];
}

static uint32_t BitFor(R2_BufferId id)
{
    return 1UL << (uint32_t)id;
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
    g_r2_w6_result.fault_bits |= bits;
    g_r2_w6_result.test_pass = 0U;
    if (g_r2_w6_result.phase != R2_W6_COMPLETE)
    {
        g_r2_w6_result.phase = R2_W6_QUIESCING;
    }
    CLEAR_BIT(TIM2->CR1, TIM_CR1_CEN);
    DisableDmaInterrupts();
    __DSB();
}

static uint32_t HardwareHealthy(void)
{
    uint32_t errors = DMA2->LISR & W6_DMA_ERRORS;
    uint32_t ovr = ADC1->SR & ADC_SR_OVR;

    g_r2_w6_result.dma_error_flags_seen |= errors;
    g_r2_w6_result.adc_ovr_seen |= ovr;

    return (errors == 0U) &&
        (ovr == 0U) &&
        ((DMA2_Stream0->CR & ~DMA_SxCR_CT) == W6_DMA_REQUIRED_CR) &&
        ((TIM2->CR1 & TIM_CR1_CEN) != 0U) &&
        (ADC1->CR2 == W6_ADC_REQUIRED_CR2);
}

static void LedgerFault(void)
{
    ++g_r2_w6_result.token_ledger_errors;
    LatchFault(R2_W6_FAULT_TOKEN_LEDGER);
}

void R2_W6_TraceQueueSend(void *queue_handle)
{
    R2_BufferState state;
    uint32_t bit;
    R2_BufferId id;

    if (queue_handle != (void *)free_queue)
    {
        return;
    }

    if (free_send_context.active == 0U)
    {
        ++g_r2_w6_result.illegal_free_send_count;
        LatchFault(R2_W6_FAULT_QUEUE_SOURCE);
        return;
    }

    id = free_send_context.buffer_id;
    if ((uint32_t)id >= R2_W6_BUFFERS)
    {
        ++g_r2_w6_result.illegal_free_send_count;
        LatchFault(R2_W6_FAULT_QUEUE_SOURCE);
        return;
    }

    bit = BitFor(id);
    if ((free_token_mask & bit) != 0U)
    {
        LedgerFault();
        return;
    }

    if (free_send_context.source == W6_FREE_SEND_INIT)
    {
        if ((R2_BufferPool_GetState(id, &state) != R2_BUFFER_POOL_OK) ||
            (state != R2_BUFFER_STATE_FREE) ||
            ((uint32_t)id < 2U))
        {
            ++g_r2_w6_result.illegal_free_send_count;
            LatchFault(R2_W6_FAULT_QUEUE_SOURCE);
            return;
        }
        free_token_mask |= bit;
        ++g_r2_w6_result.init_hook_count;
    }
    else if (free_send_context.source == W6_FREE_SEND_COMPLETE)
    {
        if ((R2_BufferPool_GetState(id, &state) != R2_BUFFER_POOL_OK) ||
            (state != R2_BUFFER_STATE_PROCESSING) ||
            (R2_BufferPool_ReleaseProcessing(id) != R2_BUFFER_POOL_OK))
        {
            ++g_r2_w6_result.illegal_free_send_count;
            LatchFault(R2_W6_FAULT_RELEASE);
            return;
        }
        free_token_mask |= bit;
        ++g_r2_w6_result.completion_hook_count;
        if ((free_send_context.sequence >= 1U) &&
            (free_send_context.sequence <= R2_W6_EVENTS))
        {
            g_r2_w6_result.trace[free_send_context.sequence - 1U]
                .release_commit_offset_cycles =
                DWT->CYCCNT - g_r2_w6_result.epoch_before;
        }
    }
    else
    {
        ++g_r2_w6_result.illegal_free_send_count;
        LatchFault(R2_W6_FAULT_QUEUE_SOURCE);
        return;
    }

    free_send_context.hook_seen = 1U;
}

static BaseType_t SendFreeToken(
    W6_FreeSendSource source,
    R2_BufferId id,
    uint32_t sequence)
{
    BaseType_t result;

    if ((free_queue == NULL) ||
        (free_send_context.active != 0U))
    {
        LatchFault(R2_W6_FAULT_QUEUE_SOURCE);
        return pdFAIL;
    }

    free_send_context.active = 1U;
    free_send_context.source = (uint32_t)source;
    free_send_context.sequence = sequence;
    free_send_context.buffer_id = id;
    free_send_context.hook_seen = 0U;

    result = xQueueSend(free_queue, &id, 0U);

    if ((result != pdPASS) || (free_send_context.hook_seen == 0U))
    {
        if (source == W6_FREE_SEND_COMPLETE)
        {
            LatchFault(R2_W6_FAULT_RELEASE);
        }
        else
        {
            LatchFault(R2_W6_FAULT_QUEUE_SOURCE);
        }
    }

    free_send_context.active = 0U;
    free_send_context.source = W6_FREE_SEND_NONE;
    free_send_context.sequence = 0U;
    free_send_context.buffer_id = R2_BUFFER_POOL_INVALID_ID;
    free_send_context.hook_seen = 0U;

    return result;
}

void R2_W6_IrqEnter(uint32_t dma_lisr)
{
    irq_enter_cycle = DWT->CYCCNT;
    irq_flags = dma_lisr;
    irq_ct = CurrentCt();
    trace_for_exit = R2_W6_EVENTS;
    irq_should_yield = pdFALSE;
    ++g_r2_w6_result.irq_count;

    if (g_r2_w6_result.phase != R2_W6_RUNNING)
    {
        DisableDmaInterrupts();
        return;
    }

    g_r2_w6_result.dma_error_flags_seen |= dma_lisr & W6_DMA_ERRORS;
    g_r2_w6_result.adc_ovr_seen |= ADC1->SR & ADC_SR_OVR;

    if ((dma_lisr & W6_DMA_ERRORS) != 0U)
    {
        LatchFault(R2_W6_FAULT_DMA);
    }
    else if ((ADC1->SR & ADC_SR_OVR) != 0U)
    {
        LatchFault(R2_W6_FAULT_OVR);
    }
    else if ((dma_lisr & DMA_LISR_TCIF0) == 0U)
    {
        LatchFault(R2_W6_FAULT_EVENT);
    }
}

static void Completed(DMA_HandleTypeDef *dma, uint32_t completed_slot)
{
    uint32_t sequence;
    uint32_t bit;
    uint32_t active_address;
    uint32_t critical_start;
    uint32_t primask;
    uint32_t now;
    uint32_t elapsed;
    uint32_t nominal;
    uint32_t ct_after;
    uint32_t healthy;
    UBaseType_t free_depth;
    UBaseType_t ready_depth;
    BaseType_t hpw = pdFALSE;
    BaseType_t free_result;
    R2_BufferId replacement = R2_BUFFER_POOL_INVALID_ID;
    R2_BufferState replacement_owner;
    R2_BufferPoolSnapshot ownership_before;
    R2_BufferPoolSnapshot ownership_after;
    R2_DmaSlotsObservation observation;
    R2_DmaSlotsRebindPlan plan;
    R2_DmaSlotsSnapshot mapping_before;
    R2_DmaSlotsSnapshot mapping_after;
    W6_BlockDescriptor descriptor;
    volatile uint32_t *inactive_address_register;
    volatile uint32_t *active_address_register;
    volatile R2_W6_Trace *trace;

    if ((dma != &hdma_adc1) ||
        (g_r2_w6_result.phase != R2_W6_RUNNING))
    {
        LatchFault(R2_W6_FAULT_EVENT);
        return;
    }

    sequence = g_r2_w6_result.input_count + 1U;
    if ((sequence > R2_W6_EVENTS) ||
        ((irq_flags & DMA_LISR_TCIF0) == 0U) ||
        (irq_ct != (sequence & 1U)) ||
        (completed_slot != (irq_ct ^ 1U)))
    {
        LatchFault(R2_W6_FAULT_EVENT);
        return;
    }

    trace_for_exit = sequence - 1U;
    trace = &g_r2_w6_result.trace[trace_for_exit];
    trace->sequence = sequence;
    trace->ct_entry = irq_ct;
    trace->completed_slot = completed_slot;
    trace->replacement_id = R2_BUFFER_POOL_INVALID_ID;
    trace->m0_before = DMA2_Stream0->M0AR;
    trace->m1_before = DMA2_Stream0->M1AR;

    if ((R2_DmaSlots_ObserveCt(irq_ct, &observation) != R2_DMA_SLOTS_OK) ||
        ((uint32_t)observation.inactive_slot != completed_slot) ||
        (R2_DmaSlots_GetSnapshot(&mapping_before) != R2_DMA_SLOTS_OK) ||
        (R2_BufferPool_GetSnapshot(&ownership_before) != R2_BUFFER_POOL_OK))
    {
        LatchFault(R2_W6_FAULT_MODEL);
        return;
    }

    trace->completed_id = observation.completed_buffer;

    if (((uint32_t)observation.completed_buffer >= R2_W6_BUFFERS) ||
        ((uint32_t)observation.active_buffer >= R2_W6_BUFFERS) ||
        (DMA2_Stream0->M0AR != Address(mapping_before.m0_buffer)) ||
        (DMA2_Stream0->M1AR != Address(mapping_before.m1_buffer)) ||
        (ownership_before.states[observation.completed_buffer] !=
            R2_BUFFER_STATE_DMA_OWNED) ||
        (ownership_before.states[observation.active_buffer] !=
            R2_BUFFER_STATE_DMA_OWNED) ||
        (ownership_before.violation_count != 0U) ||
        (mapping_before.violation_count != 0U))
    {
        LatchFault(R2_W6_FAULT_MODEL);
        return;
    }

    inactive_address_register = completed_slot == 0U ?
        &DMA2_Stream0->M0AR : &DMA2_Stream0->M1AR;
    active_address_register = completed_slot == 0U ?
        &DMA2_Stream0->M1AR : &DMA2_Stream0->M0AR;
    active_address = Address(observation.active_buffer);
    nominal = sequence * R2_W6_BLOCK_CYCLES;

    free_result = xQueueReceiveFromISR(free_queue, &replacement, &hpw);
    if (hpw != pdFALSE)
    {
        irq_should_yield = pdTRUE;
    }

    if (free_result != pdPASS)
    {
        /* Controlled capacity drop: no MxAR write, no ownership transition,
         * no READY publication. The completed physical buffer remains bound
         * to the inactive hardware slot and remains DMA_OWNED. */
        trace->decision = R2_W6_DECISION_DROP;
        trace->free_depth_after_take = 0U;
        trace->ready_depth_after_publish =
            (uint32_t)uxQueueMessagesWaitingFromISR(ready_queue);

        if ((uxQueueMessagesWaitingFromISR(free_queue) != 0U) ||
            (free_token_mask != 0U))
        {
            LatchFault(R2_W6_FAULT_DROP);
            return;
        }

        primask = __get_PRIMASK();
        __disable_irq();
        __DSB();
        critical_start = DWT->CYCCNT;
        trace->ndtr_guard = DMA2_Stream0->NDTR;
        healthy = HardwareHealthy();

        if ((g_r2_w6_result.phase != R2_W6_RUNNING) ||
            (CurrentCt() != irq_ct) ||
            (*active_address_register != active_address) ||
            (*inactive_address_register != Address(observation.completed_buffer)))
        {
            LatchFault(R2_W6_FAULT_PRECHECK);
            __set_PRIMASK(primask);
            return;
        }

        now = DWT->CYCCNT;
        elapsed = now - g_r2_w6_result.epoch_before;
        if (R2_W6_CheckWindow(sequence, irq_ct, CurrentCt(), elapsed,
                trace->ndtr_guard, healthy) != R2_W6_GUARD_OK)
        {
            LatchFault(R2_W6_FAULT_PRECHECK);
            __set_PRIMASK(primask);
            return;
        }

        __DSB();
        ct_after = CurrentCt();
        now = DWT->CYCCNT;
        trace->final_window_cycles = now - critical_start;
        trace->nominal_to_decision_cycles =
            now - g_r2_w6_result.epoch_before - nominal;
        trace->m0_after = DMA2_Stream0->M0AR;
        trace->m1_after = DMA2_Stream0->M1AR;

        if ((ct_after != irq_ct) ||
            (trace->m0_after != trace->m0_before) ||
            (trace->m1_after != trace->m1_before) ||
            (*active_address_register != active_address) ||
            (*inactive_address_register != Address(observation.completed_buffer)) ||
            (HardwareHealthy() == 0U))
        {
            LatchFault(R2_W6_FAULT_DROP);
        }
        if ((trace->final_window_cycles > R2_W6_FINAL_RESERVE_CYCLES) ||
            (trace->nominal_to_decision_cycles > R2_W6_WRITE_LIMIT_CYCLES))
        {
            LatchFault(R2_W6_FAULT_TIMING);
        }

        __set_PRIMASK(primask);
        if (g_r2_w6_result.fault_bits != 0U)
        {
            return;
        }

        if ((R2_DmaSlots_GetSnapshot(&mapping_after) != R2_DMA_SLOTS_OK) ||
            (R2_BufferPool_GetSnapshot(&ownership_after) != R2_BUFFER_POOL_OK) ||
            (mapping_after.mapping_epoch != mapping_before.mapping_epoch) ||
            (mapping_after.m0_buffer != mapping_before.m0_buffer) ||
            (mapping_after.m1_buffer != mapping_before.m1_buffer) ||
            (ownership_after.states[observation.completed_buffer] !=
                R2_BUFFER_STATE_DMA_OWNED) ||
            (ownership_after.states[observation.active_buffer] !=
                R2_BUFFER_STATE_DMA_OWNED) ||
            (ownership_after.violation_count != ownership_before.violation_count) ||
            (mapping_after.violation_count != mapping_before.violation_count))
        {
            LatchFault(R2_W6_FAULT_DROP);
            return;
        }

        trace->mapping_epoch_after = mapping_after.mapping_epoch;
        ++g_r2_w6_result.input_count;
        ++g_r2_w6_result.capacity_drop_count;
        ++g_r2_w6_result.dropped_by_buffer[observation.completed_buffer];
        ++g_r2_w6_result.current_drop_streak;
        if (g_r2_w6_result.current_drop_streak >
            g_r2_w6_result.max_drop_streak)
        {
            g_r2_w6_result.max_drop_streak =
                g_r2_w6_result.current_drop_streak;
        }

        if (trace->nominal_to_decision_cycles >
            g_r2_w6_result.max_nominal_to_decision_cycles)
        {
            g_r2_w6_result.max_nominal_to_decision_cycles =
                trace->nominal_to_decision_cycles;
        }
        if (trace->final_window_cycles >
            g_r2_w6_result.max_final_window_cycles)
        {
            g_r2_w6_result.max_final_window_cycles =
                trace->final_window_cycles;
        }

        if (sequence == R2_W6_EVENTS)
        {
            g_r2_w6_result.phase = R2_W6_QUIESCING;
            CLEAR_BIT(TIM2->CR1, TIM_CR1_CEN);
            __DSB();
        }
        return;
    }

    /* Admission path: consume one FREE token, write only the inactive MxAR,
     * then commit the software mapping and ownership transaction. */
    bit = BitFor(replacement);
    if ((free_token_mask & bit) == 0U)
    {
        LedgerFault();
        return;
    }
    free_token_mask &= ~bit;

    free_depth = uxQueueMessagesWaitingFromISR(free_queue);
    trace->free_depth_after_take = (uint32_t)free_depth;
    if ((uint32_t)free_depth < g_r2_w6_result.min_free_depth)
    {
        g_r2_w6_result.min_free_depth = (uint32_t)free_depth;
    }

    if ((R2_BufferPool_GetState(replacement, &replacement_owner) !=
            R2_BUFFER_POOL_OK) ||
        (replacement_owner != R2_BUFFER_STATE_FREE) ||
        (R2_DmaSlots_PrepareInactiveRebind(
            irq_ct, replacement, &plan) != R2_DMA_SLOTS_OK))
    {
        LatchFault(R2_W6_FAULT_MODEL);
        return;
    }

    trace->decision = R2_W6_DECISION_ADMIT;
    trace->replacement_id = replacement;

    primask = __get_PRIMASK();
    __disable_irq();
    __DSB();
    critical_start = DWT->CYCCNT;
    trace->ndtr_guard = DMA2_Stream0->NDTR;
    healthy = HardwareHealthy();

    if ((g_r2_w6_result.phase != R2_W6_RUNNING) ||
        (R2_DmaSlots_CheckPlanCt(&plan, CurrentCt()) != R2_DMA_SLOTS_OK) ||
        (*active_address_register != active_address) ||
        (*inactive_address_register != Address(observation.completed_buffer)))
    {
        LatchFault(R2_W6_FAULT_PRECHECK);
        __set_PRIMASK(primask);
        return;
    }

    now = DWT->CYCCNT;
    elapsed = now - g_r2_w6_result.epoch_before;
    if (R2_W6_CheckWindow(sequence, irq_ct, CurrentCt(), elapsed,
            trace->ndtr_guard, healthy) != R2_W6_GUARD_OK)
    {
        LatchFault(R2_W6_FAULT_PRECHECK);
        __set_PRIMASK(primask);
        return;
    }

    *inactive_address_register = Address(replacement);
    __DSB();
    ct_after = CurrentCt();
    now = DWT->CYCCNT;
    trace->final_window_cycles = now - critical_start;
    trace->nominal_to_decision_cycles =
        now - g_r2_w6_result.epoch_before - nominal;
    trace->m0_after = DMA2_Stream0->M0AR;
    trace->m1_after = DMA2_Stream0->M1AR;

    if ((*inactive_address_register != Address(replacement)) ||
        (*active_address_register != active_address) ||
        (ct_after != irq_ct) ||
        (HardwareHealthy() == 0U))
    {
        LatchFault(R2_W6_FAULT_ADDRESS);
    }
    if ((trace->final_window_cycles > R2_W6_FINAL_RESERVE_CYCLES) ||
        (trace->nominal_to_decision_cycles > R2_W6_WRITE_LIMIT_CYCLES))
    {
        LatchFault(R2_W6_FAULT_TIMING);
    }

    __set_PRIMASK(primask);
    if (g_r2_w6_result.fault_bits != 0U)
    {
        return;
    }

    if ((R2_DmaSlots_CommitPreparedRebind(&plan) != R2_DMA_SLOTS_OK) ||
        (R2_BufferPool_CommitDmaRotation(
            observation.completed_buffer, replacement) != R2_BUFFER_POOL_OK))
    {
        LatchFault(R2_W6_FAULT_MODEL);
        return;
    }

    descriptor.sequence = sequence;
    descriptor.buffer_id = observation.completed_buffer;
    descriptor.completed_slot = completed_slot;

    if (R2_DmaSlots_GetSnapshot(&mapping_after) != R2_DMA_SLOTS_OK)
    {
        LatchFault(R2_W6_FAULT_MODEL);
        return;
    }

    descriptor.mapping_epoch = mapping_after.mapping_epoch;
    trace->mapping_epoch_after = mapping_after.mapping_epoch;

    bit = BitFor(descriptor.buffer_id);
    if ((ready_token_mask & bit) != 0U)
    {
        LedgerFault();
        return;
    }

    hpw = pdFALSE;
    if (xQueueSendFromISR(ready_queue, &descriptor, &hpw) != pdPASS)
    {
        ++g_r2_w6_result.ready_send_fail_count;
        LatchFault(R2_W6_FAULT_READY_FULL);
        return;
    }
    ready_token_mask |= bit;
    if (hpw != pdFALSE)
    {
        irq_should_yield = pdTRUE;
    }

    ready_depth = uxQueueMessagesWaitingFromISR(ready_queue);
    trace->ready_depth_after_publish = (uint32_t)ready_depth;
    if ((uint32_t)ready_depth > g_r2_w6_result.max_ready_depth)
    {
        g_r2_w6_result.max_ready_depth = (uint32_t)ready_depth;
    }

    hpw = pdFALSE;
    if (xTaskNotifyFromISR(processing_task_handle, W6_WORK_BIT,
            eSetBits, &hpw) != pdPASS)
    {
        ++g_r2_w6_result.notification_fail_count;
        LatchFault(R2_W6_FAULT_EVENT);
        return;
    }
    if (hpw != pdFALSE)
    {
        irq_should_yield = pdTRUE;
    }

    ++g_r2_w6_result.input_count;
    ++g_r2_w6_result.admitted_count;
    ++g_r2_w6_result.admitted_by_buffer[descriptor.buffer_id];

    if (g_r2_w6_result.current_drop_streak != 0U)
    {
        ++g_r2_w6_result.recovered_admission_after_drop_count;
        g_r2_w6_result.current_drop_streak = 0U;
    }

    if (trace->nominal_to_decision_cycles >
        g_r2_w6_result.max_nominal_to_decision_cycles)
    {
        g_r2_w6_result.max_nominal_to_decision_cycles =
            trace->nominal_to_decision_cycles;
    }
    if (trace->final_window_cycles >
        g_r2_w6_result.max_final_window_cycles)
    {
        g_r2_w6_result.max_final_window_cycles =
            trace->final_window_cycles;
    }

    if ((R2_BufferPool_GetSnapshot(&ownership_after) != R2_BUFFER_POOL_OK) ||
        (ownership_after.states[mapping_after.m0_buffer] !=
            R2_BUFFER_STATE_DMA_OWNED) ||
        (ownership_after.states[mapping_after.m1_buffer] !=
            R2_BUFFER_STATE_DMA_OWNED) ||
        (ownership_after.violation_count != 0U) ||
        (mapping_after.violation_count != 0U))
    {
        LatchFault(R2_W6_FAULT_MODEL);
        return;
    }

    if (sequence == R2_W6_EVENTS)
    {
        g_r2_w6_result.phase = R2_W6_QUIESCING;
        CLEAR_BIT(TIM2->CR1, TIM_CR1_CEN);
        __DSB();
    }
}

static void M0Complete(DMA_HandleTypeDef *dma)
{
    Completed(dma, 0U);
}

static void M1Complete(DMA_HandleTypeDef *dma)
{
    Completed(dma, 1U);
}

static void DmaError(DMA_HandleTypeDef *dma)
{
    (void)dma;
    LatchFault(R2_W6_FAULT_DMA);
}

void R2_W6_IrqExit(void)
{
    uint32_t now = DWT->CYCCNT;
    uint32_t offset = now - g_r2_w6_result.epoch_before;

    if (trace_for_exit < R2_W6_EVENTS)
    {
        volatile R2_W6_Trace *trace = &g_r2_w6_result.trace[trace_for_exit];
        uint32_t nominal = trace->sequence * R2_W6_BLOCK_CYCLES;

        if (offset < nominal)
        {
            LatchFault(R2_W6_FAULT_TIMING);
        }
        else
        {
            trace->nominal_to_irq_exit_cycles = offset - nominal;
            if (trace->nominal_to_irq_exit_cycles > R2_W6_EXIT_LIMIT_CYCLES)
            {
                LatchFault(R2_W6_FAULT_TIMING);
            }
            if (trace->nominal_to_irq_exit_cycles >
                g_r2_w6_result.max_nominal_to_irq_exit_cycles)
            {
                g_r2_w6_result.max_nominal_to_irq_exit_cycles =
                    trace->nominal_to_irq_exit_cycles;
            }
        }
    }

    if (irq_should_yield != pdFALSE)
    {
        portYIELD_FROM_ISR(pdTRUE);
    }
}

static int ConfigurationValid(void)
{
    uint32_t id;

    if ((SystemCoreClock != 180000000U) ||
        (HAL_RCC_GetPCLK1Freq() != 45000000U) ||
        (HAL_RCC_GetPCLK2Freq() != 90000000U) ||
        (HAL_NVIC_GetPriorityGrouping() != NVIC_PRIORITYGROUP_4) ||
        (hadc1.Instance != ADC1) ||
        (htim2.Instance != TIM2) ||
        (hdma_adc1.Instance != DMA2_Stream0) ||
        (hadc1.DMA_Handle != &hdma_adc1) ||
        (hdma_adc1.State != HAL_DMA_STATE_READY) ||
        ((DMA2_Stream0->CR & DMA_SxCR_EN) != 0U) ||
        ((TIM2->CR1 & TIM_CR1_CEN) != 0U) ||
        (TIM2->PSC != 0U) ||
        (TIM2->ARR != 449U) ||
        ((TIM2->CR2 & TIM_CR2_MMS) != TIM_TRGO_UPDATE) ||
        (TIM2->DIER != 0U) ||
        (hadc1.Init.ClockPrescaler != ADC_CLOCK_SYNC_PCLK_DIV4) ||
        (hadc1.Init.Resolution != ADC_RESOLUTION_12B) ||
        (hadc1.Init.ContinuousConvMode != DISABLE) ||
        (hadc1.Init.DMAContinuousRequests != ENABLE) ||
        (hadc1.Init.ExternalTrigConv != ADC_EXTERNALTRIGCONV_T2_TRGO) ||
        (hadc1.Init.ExternalTrigConvEdge != ADC_EXTERNALTRIGCONVEDGE_RISING) ||
        ((ADC1->CR1 & (ADC_CR1_EOCIE | ADC_CR1_OVRIE)) != 0U) ||
        (ADC1->SQR1 != 0U) ||
        (ADC1->SQR3 != 0U) ||
        ((ADC1->SMPR2 & ADC_SMPR2_SMP0) != ADC_SAMPLETIME_28CYCLES) ||
        (NVIC_GetPriority(DMA2_Stream0_IRQn) != 5U))
    {
        return 0;
    }

    for (id = 0U; id < R2_W6_BUFFERS; ++id)
    {
        uint32_t address = Address(id);
        if (((address & 3U) != 0U) ||
            (address < 0x20000000U) ||
            (address > 0x20020000U - 2U * R2_W6_BLOCK_SAMPLES))
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
    DMA2->LIFCR = W6_ALL_FLAGS;
    HAL_NVIC_ClearPendingIRQ(DMA2_Stream0_IRQn);

    hdma_adc1.XferCpltCallback = M0Complete;
    hdma_adc1.XferM1CpltCallback = M1Complete;
    hdma_adc1.XferHalfCpltCallback = NULL;
    hdma_adc1.XferM1HalfCpltCallback = NULL;
    hdma_adc1.XferErrorCallback = DmaError;
    hdma_adc1.XferAbortCallback = NULL;

    status = HAL_DMAEx_MultiBufferStart_IT(
        &hdma_adc1,
        (uint32_t)(uintptr_t)&ADC1->DR,
        Address(0U),
        Address(1U),
        R2_W6_BLOCK_SAMPLES);
    if (status != HAL_OK)
    {
        return status;
    }

    __HAL_DMA_DISABLE_IT(&hdma_adc1, DMA_IT_HT);
    __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_EOC | ADC_FLAG_OVR);

    status = HAL_ADC_Start(&hadc1);
    if (status != HAL_OK)
    {
        return status;
    }

    SET_BIT(ADC1->CR2, ADC_CR2_DMA);

    g_r2_w6_result.start_dma_cr = DMA2_Stream0->CR;
    g_r2_w6_result.start_dma_ndtr = DMA2_Stream0->NDTR;
    g_r2_w6_result.start_adc_cr2 = ADC1->CR2;
    g_r2_w6_result.start_tim2_cr1 = TIM2->CR1;

    if ((DMA2_Stream0->CR != W6_DMA_REQUIRED_CR) ||
        (DMA2_Stream0->NDTR != R2_W6_BLOCK_SAMPLES) ||
        (DMA2_Stream0->M0AR != Address(0U)) ||
        (DMA2_Stream0->M1AR != Address(1U)) ||
        (ADC1->CR2 != W6_ADC_REQUIRED_CR2) ||
        ((TIM2->CR1 & TIM_CR1_CEN) != 0U))
    {
        return HAL_ERROR;
    }

    g_r2_w6_result.phase = R2_W6_ARMED;
    primask = __get_PRIMASK();
    __disable_irq();
    g_r2_w6_result.phase = R2_W6_RUNNING;
    g_r2_w6_result.epoch_before = DWT->CYCCNT;
    status = HAL_TIM_Base_Start(&htim2);
    __DSB();
    g_r2_w6_result.epoch_after = DWT->CYCCNT;
    __set_PRIMASK(primask);

    return status;
}

static HAL_StatusTypeDef Stop(void)
{
    uint32_t begin;
    HAL_StatusTypeDef status;

    g_r2_w6_result.phase = R2_W6_QUIESCING;
    (void)HAL_TIM_Base_Stop(&htim2);
    DisableDmaInterrupts();

    begin = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - begin) < W6_SETTLE_CYCLES)
    {
        __NOP();
    }

    g_r2_w6_result.dma_error_flags_seen |= DMA2->LISR & W6_DMA_ERRORS;
    g_r2_w6_result.adc_ovr_seen |= ADC1->SR & ADC_SR_OVR;

    status = HAL_ADC_Stop_DMA(&hadc1);
    DMA2->LIFCR = W6_ALL_FLAGS;
    HAL_NVIC_ClearPendingIRQ(DMA2_Stream0_IRQn);
    __DSB();

    if ((DMA2_Stream0->CR & DMA_SxCR_EN) != 0U)
    {
        return HAL_ERROR;
    }

    return status;
}

static void ProcessDescriptor(const W6_BlockDescriptor *descriptor)
{
    uint32_t id = descriptor->buffer_id;
    uint32_t sequence = descriptor->sequence;
    uint32_t bit;
#if R2_W6_DROP_MODE != 0U
    uint32_t begin;
#endif
    uint32_t min_value = UINT32_MAX;
    uint32_t max_value = 0U;
    uint32_t j;
    R2_BufferState state;
    volatile R2_W6_Trace *trace;

    if ((sequence == 0U) ||
        (sequence > R2_W6_EVENTS) ||
        (id >= R2_W6_BUFFERS))
    {
        LatchFault(R2_W6_FAULT_PROCESSING);
        return;
    }

    trace = &g_r2_w6_result.trace[sequence - 1U];
    if ((trace->decision != R2_W6_DECISION_ADMIT) ||
        (trace->completed_id != id) ||
        (descriptor->mapping_epoch != trace->mapping_epoch_after))
    {
        LatchFault(R2_W6_FAULT_PROCESSING);
        return;
    }

    bit = BitFor((R2_BufferId)id);

    taskENTER_CRITICAL();
    if (((ready_token_mask & bit) == 0U) ||
        (R2_BufferPool_GetState((R2_BufferId)id, &state) !=
            R2_BUFFER_POOL_OK) ||
        (state != R2_BUFFER_STATE_READY) ||
        (R2_BufferPool_ClaimReady((R2_BufferId)id) != R2_BUFFER_POOL_OK))
    {
        taskEXIT_CRITICAL();
        LatchFault(R2_W6_FAULT_PROCESSING);
        return;
    }
    ready_token_mask &= ~bit;
    taskEXIT_CRITICAL();

    trace->processing_begin_offset_cycles =
        DWT->CYCCNT - g_r2_w6_result.epoch_before;

    /* NORMAL mode performs no deliberate hold. DROP mode holds the current
     * PROCESSING buffer for K+5 block periods so all K FREE tokens are
     * consumed and several consecutive capacity drops are forced. The task
     * remains preemptible by DMA and kernel interrupts in both modes. */
#if R2_W6_DROP_MODE != 0U
    begin = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - begin) < R2_W6_PROCESS_HOLD_CYCLES)
    {
        __NOP();
    }
#endif

    if ((buffers[id].guard_lo != W6_GUARD_LO) ||
        (buffers[id].guard_hi != W6_GUARD_HI))
    {
        ++g_r2_w6_result.canary_errors;
        LatchFault(R2_W6_FAULT_SAMPLES);
        return;
    }

    for (j = 0U; j < R2_W6_BLOCK_SAMPLES; ++j)
    {
        uint32_t value = buffers[id].samples[j];
        if (value > 4095U)
        {
            ++g_r2_w6_result.sample_errors;
        }
        else
        {
            ++g_r2_w6_result.full_sample_count;
        }
        if (value < min_value)
        {
            min_value = value;
        }
        if (value > max_value)
        {
            max_value = value;
        }
    }

    trace->raw_min = min_value;
    trace->raw_max = max_value;
    trace->processing_end_offset_cycles =
        DWT->CYCCNT - g_r2_w6_result.epoch_before;

    if (g_r2_w6_result.sample_errors != 0U)
    {
        LatchFault(R2_W6_FAULT_SAMPLES);
        return;
    }

    if (SendFreeToken(W6_FREE_SEND_COMPLETE,
            (R2_BufferId)id, sequence) != pdPASS)
    {
        LatchFault(R2_W6_FAULT_RELEASE);
        return;
    }

    ++g_r2_w6_result.processed_count;
    ++g_r2_w6_result.released_count;
    ++g_r2_w6_result.processed_by_buffer[id];
    ++g_r2_w6_result.released_by_buffer[id];
    trace->processed_ok = 1U;
}

static void ProcessingTask(void *argument)
{
    uint32_t notification_value;
    W6_BlockDescriptor descriptor;
    (void)argument;

    for (;;)
    {
        notification_value = 0U;
        (void)xTaskNotifyWait(0U, UINT32_MAX,
            &notification_value, portMAX_DELAY);

        if ((notification_value & W6_WORK_BIT) != 0U)
        {
            while (xQueueReceive(ready_queue, &descriptor, 0U) == pdPASS)
            {
                ProcessDescriptor(&descriptor);
                if (g_r2_w6_result.fault_bits != 0U)
                {
                    break;
                }
            }
        }
    }
}

static void Finalize(void)
{
    R2_BufferPoolSnapshot pool;
    R2_DmaSlotsSnapshot slots;
    uint32_t expected_free_mask = 0U;
    uint32_t dropped_sum = 0U;
    uint32_t id;

    if ((R2_BufferPool_GetSnapshot(&pool) != R2_BUFFER_POOL_OK) ||
        (R2_DmaSlots_GetSnapshot(&slots) != R2_DMA_SLOTS_OK))
    {
        LatchFault(R2_W6_FAULT_MODEL);
        return;
    }

    g_r2_w6_result.pool_at_stop = pool;
    g_r2_w6_result.slots_at_stop = slots;
    g_r2_w6_result.free_queue_depth_final =
        (uint32_t)uxQueueMessagesWaiting(free_queue);
    g_r2_w6_result.ready_queue_depth_final =
        (uint32_t)uxQueueMessagesWaiting(ready_queue);
    g_r2_w6_result.free_token_mask_final = free_token_mask;
    g_r2_w6_result.ready_token_mask_final = ready_token_mask;

    for (id = 0U; id < R2_W6_BUFFERS; ++id)
    {
        if ((buffers[id].guard_lo != W6_GUARD_LO) ||
            (buffers[id].guard_hi != W6_GUARD_HI))
        {
            ++g_r2_w6_result.canary_errors;
        }

        if (pool.states[id] == R2_BUFFER_STATE_FREE)
        {
            expected_free_mask |= 1UL << id;
        }

        if ((g_r2_w6_result.admitted_by_buffer[id] !=
                g_r2_w6_result.processed_by_buffer[id]) ||
            (g_r2_w6_result.admitted_by_buffer[id] !=
                g_r2_w6_result.released_by_buffer[id]))
        {
            LatchFault(R2_W6_FAULT_PROCESSING);
        }

        dropped_sum += g_r2_w6_result.dropped_by_buffer[id];
    }

    if ((pool.k != R2_W6_K) ||
        (pool.active_count != R2_W6_BUFFERS) ||
        (pool.free_count != R2_W6_K) ||
        (pool.ready_count != 0U) ||
        (pool.processing_count != 0U) ||
        (pool.dma_owned_count != 2U) ||
        (pool.violation_count != 0U) ||
        (slots.violation_count != 0U) ||
        (slots.m0_buffer == slots.m1_buffer) ||
        (pool.states[slots.m0_buffer] != R2_BUFFER_STATE_DMA_OWNED) ||
        (pool.states[slots.m1_buffer] != R2_BUFFER_STATE_DMA_OWNED) ||
        (g_r2_w6_result.free_queue_depth_final != R2_W6_K) ||
        (g_r2_w6_result.ready_queue_depth_final != 0U) ||
        (ready_token_mask != 0U) ||
        (free_token_mask != expected_free_mask) ||
        ((free_token_mask | BitFor(slots.m0_buffer) |
          BitFor(slots.m1_buffer)) != W6_ALL_BUFFER_MASK))
    {
        LatchFault(R2_W6_FAULT_MODEL);
    }

    if ((g_r2_w6_result.input_count != R2_W6_EVENTS) ||
        ((g_r2_w6_result.admitted_count +
          g_r2_w6_result.capacity_drop_count) != R2_W6_EVENTS) ||
        (g_r2_w6_result.processed_count != g_r2_w6_result.admitted_count) ||
        (g_r2_w6_result.released_count != g_r2_w6_result.admitted_count) ||
        (g_r2_w6_result.completion_hook_count != g_r2_w6_result.admitted_count) ||
        (g_r2_w6_result.init_hook_count != R2_W6_K) ||
        (g_r2_w6_result.illegal_free_send_count != 0U) ||
        (g_r2_w6_result.ready_send_fail_count != 0U) ||
        (g_r2_w6_result.notification_fail_count != 0U) ||
        (g_r2_w6_result.token_ledger_errors != 0U) ||
        (g_r2_w6_result.dma_error_flags_seen != 0U) ||
        (g_r2_w6_result.adc_ovr_seen != 0U) ||
        (g_r2_w6_result.full_sample_count !=
            g_r2_w6_result.admitted_count * R2_W6_BLOCK_SAMPLES) ||
        (g_r2_w6_result.sample_errors != 0U) ||
        (g_r2_w6_result.canary_errors != 0U) ||
        (dropped_sum != g_r2_w6_result.capacity_drop_count))
    {
        LatchFault(R2_W6_FAULT_EVENT);
    }

    if (R2_W6_DROP_MODE == 0U)
    {
        if ((g_r2_w6_result.admitted_count != R2_W6_EVENTS) ||
            (g_r2_w6_result.capacity_drop_count != 0U) ||
            (g_r2_w6_result.max_drop_streak != 0U) ||
            (g_r2_w6_result.recovered_admission_after_drop_count != 0U))
        {
            LatchFault(R2_W6_FAULT_EVENT);
        }
    }
    else
    {
        if ((g_r2_w6_result.admitted_count < R2_W6_MIN_ADMISSIONS) ||
            (g_r2_w6_result.capacity_drop_count < R2_W6_MIN_CAPACITY_DROPS) ||
            (g_r2_w6_result.max_drop_streak < R2_W6_MIN_DROP_STREAK) ||
            (g_r2_w6_result.recovered_admission_after_drop_count <
                R2_W6_MIN_RECOVERIES))
        {
            LatchFault(R2_W6_FAULT_EVENT);
        }
    }
}

static void ControlTask(void *argument)
{
    uint32_t id;
    uint32_t j;
    TickType_t start_tick;
    TickType_t drain_tick;
    HAL_StatusTypeDef status;
    (void)argument;

    vTaskDelay(pdMS_TO_TICKS(100U));

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    g_r2_w6_result.system_core_clock = SystemCoreClock;
    g_r2_w6_result.configured_k = R2_W6_K;
    g_r2_w6_result.drop_mode = R2_W6_DROP_MODE;
    g_r2_w6_result.process_hold_blocks = R2_W6_PROCESS_HOLD_BLOCKS;
    g_r2_w6_result.aircr = SCB->AIRCR;
    g_r2_w6_result.min_free_depth = R2_W6_K;

    for (id = 0U; id < R2_W6_BUFFERS; ++id)
    {
        buffers[id].guard_lo = W6_GUARD_LO;
        buffers[id].guard_hi = W6_GUARD_HI;
        for (j = 0U; j < R2_W6_BLOCK_SAMPLES; ++j)
        {
            buffers[id].samples[j] = W6_SENTINEL;
        }
        g_r2_w6_result.buffer_address[id] = Address(id);
    }

    if (!ConfigurationValid())
    {
        LatchFault(R2_W6_FAULT_CONFIG);
        g_r2_w6_result.phase = R2_W6_FAILED;
    }
    else
    {
        status = Start();
        g_r2_w6_result.start_status = (uint32_t)status;
        if (status != HAL_OK)
        {
            LatchFault(R2_W6_FAULT_START);
        }

        start_tick = xTaskGetTickCount();
        while (g_r2_w6_result.phase == R2_W6_RUNNING)
        {
            if ((TickType_t)(xTaskGetTickCount() - start_tick) >=
                pdMS_TO_TICKS(R2_W6_RUN_TIMEOUT_MS))
            {
                LatchFault(R2_W6_FAULT_TIMEOUT);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1U));
        }

        status = Stop();
        g_r2_w6_result.stop_status = (uint32_t)status;
        if ((status != HAL_OK) ||
            ((DMA2_Stream0->CR & DMA_SxCR_EN) != 0U) ||
            ((TIM2->CR1 & TIM_CR1_CEN) != 0U) ||
            ((ADC1->CR2 & (ADC_CR2_ADON | ADC_CR2_DMA)) != 0U))
        {
            LatchFault(R2_W6_FAULT_STOP);
        }

        drain_tick = xTaskGetTickCount();
        while ((g_r2_w6_result.processed_count !=
                g_r2_w6_result.admitted_count) &&
               (g_r2_w6_result.fault_bits == 0U))
        {
            if ((TickType_t)(xTaskGetTickCount() - drain_tick) >=
                pdMS_TO_TICKS(W6_DRAIN_TIMEOUT_MS))
            {
                LatchFault(R2_W6_FAULT_TIMEOUT);
                break;
            }
            (void)xTaskNotify(processing_task_handle,
                W6_WORK_BIT, eSetBits);
            vTaskDelay(pdMS_TO_TICKS(1U));
        }

        if (g_r2_w6_result.fault_bits == 0U)
        {
            uint32_t irq_before = g_r2_w6_result.irq_count;
            uint32_t input_before = g_r2_w6_result.input_count;
            uint32_t admitted_before = g_r2_w6_result.admitted_count;
            uint32_t drop_before = g_r2_w6_result.capacity_drop_count;
            uint32_t processed_before = g_r2_w6_result.processed_count;

            vTaskDelay(pdMS_TO_TICKS(2U));

            g_r2_w6_result.quiet_irq_count = g_r2_w6_result.irq_count;
            g_r2_w6_result.quiet_input_count = g_r2_w6_result.input_count;
            g_r2_w6_result.quiet_admitted_count =
                g_r2_w6_result.admitted_count;
            g_r2_w6_result.quiet_drop_count =
                g_r2_w6_result.capacity_drop_count;
            g_r2_w6_result.quiet_processed_count =
                g_r2_w6_result.processed_count;

            if ((irq_before != g_r2_w6_result.irq_count) ||
                (input_before != g_r2_w6_result.input_count) ||
                (admitted_before != g_r2_w6_result.admitted_count) ||
                (drop_before != g_r2_w6_result.capacity_drop_count) ||
                (processed_before != g_r2_w6_result.processed_count))
            {
                LatchFault(R2_W6_FAULT_EVENT);
            }
        }

        if (g_r2_w6_result.fault_bits == 0U)
        {
            Finalize();
        }

        g_r2_w6_result.test_pass =
            g_r2_w6_result.fault_bits == 0U ? 1U : 0U;
        g_r2_w6_result.phase =
            g_r2_w6_result.test_pass != 0U ?
            R2_W6_COMPLETE : R2_W6_FAILED;
    }

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

void R2_W6_CreateTasks(void)
{
    TaskHandle_t control_handle;
    uint32_t id;

    g_r2_w6_result.magic = W6_MAGIC;
    g_r2_w6_result.configured_k = R2_W6_K;
    g_r2_w6_result.drop_mode = R2_W6_DROP_MODE;
    g_r2_w6_result.process_hold_blocks = R2_W6_PROCESS_HOLD_BLOCKS;
    g_r2_w6_result.start_status = UINT32_MAX;
    g_r2_w6_result.stop_status = UINT32_MAX;
    g_r2_w6_result.phase = R2_W6_RESET;

    free_queue = xQueueCreateStatic(
        R2_W6_K,
        sizeof(R2_BufferId),
        free_queue_storage,
        &free_queue_control);

    ready_queue = xQueueCreateStatic(
        R2_W6_K,
        sizeof(W6_BlockDescriptor),
        ready_queue_storage,
        &ready_queue_control);

    if ((free_queue == NULL) || (ready_queue == NULL))
    {
        g_r2_w6_result.fault_bits = R2_W6_FAULT_CONFIG;
        g_r2_w6_result.phase = R2_W6_FAILED;
        return;
    }

    vQueueAddToRegistry(free_queue, "R2Free");
    vQueueAddToRegistry(ready_queue, "R2Ready");

    R2_BufferPool_Reset();
    R2_DmaSlots_Reset();

    if ((R2_BufferPool_Activate(R2_W6_K) != R2_BUFFER_POOL_OK) ||
        (R2_DmaSlots_Initialize((R2_BufferId)0U,
            (R2_BufferId)1U) != R2_DMA_SLOTS_OK))
    {
        g_r2_w6_result.fault_bits = R2_W6_FAULT_CONFIG;
        g_r2_w6_result.phase = R2_W6_FAILED;
        return;
    }

    free_token_mask = 0U;
    ready_token_mask = 0U;
    free_send_context.active = 0U;
    free_send_context.source = W6_FREE_SEND_NONE;
    free_send_context.buffer_id = R2_BUFFER_POOL_INVALID_ID;

    for (id = 2U; id < R2_W6_BUFFERS; ++id)
    {
        if (SendFreeToken(W6_FREE_SEND_INIT, (R2_BufferId)id, 0U) != pdPASS)
        {
            g_r2_w6_result.phase = R2_W6_FAILED;
            return;
        }
    }

    if ((uxQueueMessagesWaiting(free_queue) != R2_W6_K) ||
        (free_token_mask != (W6_ALL_BUFFER_MASK & ~0x3UL)))
    {
        g_r2_w6_result.fault_bits |= R2_W6_FAULT_TOKEN_LEDGER;
        g_r2_w6_result.phase = R2_W6_FAILED;
        return;
    }

    processing_task_handle = xTaskCreateStatic(
        ProcessingTask,
        "R2W6Proc",
        W6_PROCESS_STACK_WORDS,
        NULL,
        tskIDLE_PRIORITY + 3U,
        processing_task_stack,
        &processing_task_control);

    control_handle = xTaskCreateStatic(
        ControlTask,
        "R2W6Ctl",
        W6_CONTROL_STACK_WORDS,
        NULL,
        tskIDLE_PRIORITY + 2U,
        control_task_stack,
        &control_task_control);

    g_r2_w6_result.processing_task_created =
        processing_task_handle != NULL ? 1U : 0U;
    g_r2_w6_result.control_task_created =
        control_handle != NULL ? 1U : 0U;

    if ((processing_task_handle == NULL) || (control_handle == NULL))
    {
        g_r2_w6_result.fault_bits |= R2_W6_FAULT_START;
        g_r2_w6_result.phase = R2_W6_FAILED;
        return;
    }

    g_r2_w6_result.phase = R2_W6_TASKS_CREATED;
}
