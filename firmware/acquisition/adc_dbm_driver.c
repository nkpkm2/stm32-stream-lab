#include "adc_dbm_driver.h"

#include "main.h"
#include "stm32f4xx_hal_dma_ex.h"

#include <stddef.h>

extern ADC_HandleTypeDef hadc1;
extern DMA_HandleTypeDef hdma_adc1;
extern TIM_HandleTypeDef htim2;

#define ADC_DBM_STOP_SETTLE_CYCLES 1200U
#define ADC_DBM_SRAM_BASE 0x20000000U
#define ADC_DBM_SRAM_END  0x2001FFFFU
#define ADC_DBM_DMA_ERRORS \
    (DMA_LISR_TEIF0 | DMA_LISR_DMEIF0 | DMA_LISR_FEIF0)

/*
 * Frozen R2 compatibility profile. These are the same armed-state register
 * values proven by the W3-W6 hardware profiles. CT is masked while acquisition
 * runs because hardware toggles that bit between M0 and M1.
 */
#define ADC_DBM_R2_REQUIRED_DMA_CR  0x00062D17U
#define ADC_DBM_R2_REQUIRED_ADC_CR2 0x16000701U

typedef struct
{
    volatile AdcDbmDriverState state;
    volatile uint32_t hardware_owned;
    volatile uint32_t address_map_trusted;
    AdcDbmDriverArmConfig arm;
    AdcDbmRebindGuardPolicy rebind_policy;
    volatile uint32_t start_epoch_cycle;
    volatile uint32_t completion_count;
    volatile uint32_t callback_active;
    volatile uint32_t callback_sequence;
    volatile uint32_t callback_completed_slot;
    volatile uint32_t callback_action_applied;
    volatile uint32_t bound_address[2];
    volatile uint32_t suppressed_completion_count;
    volatile uint32_t dma_error_count;
    volatile uint32_t inactive_keep_success_count;
    volatile uint32_t inactive_rebind_success_count;
    volatile uint32_t inactive_failure_count;
} AdcDbmDriverStorage;

static AdcDbmDriverStorage driver =
{
    ADC_DBM_DRIVER_STATE_UNINITIALIZED,
    0U,
    0U,
    {0U, 0U, 0U, NULL, NULL, NULL},
    {0U, 0U, 0U, 0U, 0U},
    0U,
    0U,
    0U,
    0U,
    0U,
    0U,
    {0U, 0U},
    0U,
    0U,
    0U,
    0U,
    0U
};

static uint32_t EnterCritical(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    __DSB();
    __ISB();
    return primask;
}

static void ExitCritical(uint32_t primask)
{
    __DSB();
    __ISB();
    __set_PRIMASK(primask);
}

static void EnsureDwtEnabled(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static uint32_t DwtNow(void)
{
    return DWT->CYCCNT;
}

static void DelayCycles(uint32_t cycles)
{
    uint32_t begin = DwtNow();

    while ((uint32_t)(DwtNow() - begin) < cycles)
    {
        __NOP();
    }
}

static uint32_t CurrentCt(void)
{
    return ((DMA2_Stream0->CR & DMA_SxCR_CT) != 0U) ? 1U : 0U;
}

static volatile uint32_t *AddressRegister(AdcDbmDriverSlot slot)
{
    return (slot == ADC_DBM_DRIVER_SLOT_M0) ?
        &DMA2_Stream0->M0AR : &DMA2_Stream0->M1AR;
}

static AdcDbmDriverSlot OtherSlot(AdcDbmDriverSlot slot)
{
    return (slot == ADC_DBM_DRIVER_SLOT_M0) ?
        ADC_DBM_DRIVER_SLOT_M1 : ADC_DBM_DRIVER_SLOT_M0;
}

static void DisableDmaInterrupts(void)
{
    CLEAR_BIT(
        DMA2_Stream0->CR,
        DMA_SxCR_TCIE |
        DMA_SxCR_HTIE |
        DMA_SxCR_TEIE |
        DMA_SxCR_DMEIE);
    CLEAR_BIT(DMA2_Stream0->FCR, DMA_SxFCR_FEIE);
}

static void ClearDmaFlags(void)
{
    __HAL_DMA_CLEAR_FLAG(
        &hdma_adc1,
        __HAL_DMA_GET_TC_FLAG_INDEX(&hdma_adc1));
    __HAL_DMA_CLEAR_FLAG(
        &hdma_adc1,
        __HAL_DMA_GET_HT_FLAG_INDEX(&hdma_adc1));
    __HAL_DMA_CLEAR_FLAG(
        &hdma_adc1,
        __HAL_DMA_GET_TE_FLAG_INDEX(&hdma_adc1));
    __HAL_DMA_CLEAR_FLAG(
        &hdma_adc1,
        __HAL_DMA_GET_DME_FLAG_INDEX(&hdma_adc1));
    __HAL_DMA_CLEAR_FLAG(
        &hdma_adc1,
        __HAL_DMA_GET_FE_FLAG_INDEX(&hdma_adc1));
}

static int HardwareQuiescent(void)
{
    return ((DMA2_Stream0->CR & DMA_SxCR_EN) == 0U) &&
        ((TIM2->CR1 & TIM_CR1_CEN) == 0U) &&
        ((ADC1->CR2 & (ADC_CR2_ADON | ADC_CR2_DMA)) == 0U);
}

static int HardwareHealthyRunning(void)
{
    return ((DMA2->LISR & ADC_DBM_DMA_ERRORS) == 0U) &&
        ((ADC1->SR & ADC_SR_OVR) == 0U) &&
        ((DMA2_Stream0->CR & ~DMA_SxCR_CT) ==
            ADC_DBM_R2_REQUIRED_DMA_CR) &&
        ((TIM2->CR1 & TIM_CR1_CEN) != 0U) &&
        (ADC1->CR2 == ADC_DBM_R2_REQUIRED_ADC_CR2);
}

static int SpanValid(uint32_t address, uint32_t block_samples)
{
    uint32_t bytes;
    uint32_t last;

    if ((block_samples == 0U) ||
        (block_samples > 65535U) ||
        ((address & 3U) != 0U) ||
        (address < ADC_DBM_SRAM_BASE))
    {
        return 0;
    }

    bytes = block_samples * 2U;
    last = address + bytes - 1U;

    if ((last < address) || (last > ADC_DBM_SRAM_END))
    {
        return 0;
    }

    return 1;
}

static int SpansOverlap(
    uint32_t a,
    uint32_t b,
    uint32_t block_samples)
{
    uint32_t bytes = block_samples * 2U;
    uint32_t a_last = a + bytes - 1U;
    uint32_t b_last = b + bytes - 1U;

    return !((a_last < b) || (b_last < a));
}

static void LatchRealtimeHardwareError(void)
{
    CLEAR_BIT(TIM2->CR1, TIM_CR1_CEN);
    DisableDmaInterrupts();
    __DSB();
    driver.state = ADC_DBM_DRIVER_STATE_ERROR;
}

static AdcDbmDriverStatus ConfigurationValid(void)
{
    if ((SystemCoreClock != 180000000U) ||
        (HAL_RCC_GetPCLK1Freq() != 45000000U) ||
        (HAL_RCC_GetPCLK2Freq() != 90000000U) ||
        (HAL_NVIC_GetPriorityGrouping() != NVIC_PRIORITYGROUP_4))
    {
        return ADC_DBM_DRIVER_CONFIG_ERROR;
    }

    if ((hadc1.Instance != ADC1) ||
        (htim2.Instance != TIM2) ||
        (hdma_adc1.Instance != DMA2_Stream0) ||
        (hadc1.DMA_Handle != &hdma_adc1))
    {
        return ADC_DBM_DRIVER_CONFIG_ERROR;
    }

    if ((hdma_adc1.Init.Channel != DMA_CHANNEL_0) ||
        (hdma_adc1.Init.Direction != DMA_PERIPH_TO_MEMORY) ||
        (hdma_adc1.Init.PeriphInc != DMA_PINC_DISABLE) ||
        (hdma_adc1.Init.MemInc != DMA_MINC_ENABLE) ||
        (hdma_adc1.Init.PeriphDataAlignment != DMA_PDATAALIGN_HALFWORD) ||
        (hdma_adc1.Init.MemDataAlignment != DMA_MDATAALIGN_HALFWORD) ||
        (hdma_adc1.Init.Mode != DMA_CIRCULAR) ||
        (hdma_adc1.Init.Priority != DMA_PRIORITY_HIGH) ||
        (hdma_adc1.Init.FIFOMode != DMA_FIFOMODE_DISABLE))
    {
        return ADC_DBM_DRIVER_CONFIG_ERROR;
    }

    if ((TIM2->PSC != 0U) ||
        (TIM2->ARR != 449U) ||
        ((TIM2->CR2 & TIM_CR2_MMS) != TIM_TRGO_UPDATE) ||
        (TIM2->DIER != 0U))
    {
        return ADC_DBM_DRIVER_CONFIG_ERROR;
    }

    if ((hadc1.Init.ClockPrescaler != ADC_CLOCK_SYNC_PCLK_DIV4) ||
        (hadc1.Init.Resolution != ADC_RESOLUTION_12B) ||
        (hadc1.Init.ContinuousConvMode != DISABLE) ||
        (hadc1.Init.DMAContinuousRequests != ENABLE) ||
        (hadc1.Init.ExternalTrigConv != ADC_EXTERNALTRIGCONV_T2_TRGO) ||
        (hadc1.Init.ExternalTrigConvEdge !=
            ADC_EXTERNALTRIGCONVEDGE_RISING) ||
        ((ADC1->CR1 & (ADC_CR1_EOCIE | ADC_CR1_OVRIE)) != 0U) ||
        (ADC1->SQR1 != 0U) ||
        (ADC1->SQR3 != 0U) ||
        ((ADC1->SMPR2 & ADC_SMPR2_SMP0) != ADC_SAMPLETIME_28CYCLES) ||
        (NVIC_GetPriority(DMA2_Stream0_IRQn) != 5U))
    {
        return ADC_DBM_DRIVER_CONFIG_ERROR;
    }

    if (!HardwareQuiescent() ||
        (hdma_adc1.State != HAL_DMA_STATE_READY))
    {
        return ADC_DBM_DRIVER_BUSY;
    }

    return ADC_DBM_DRIVER_OK;
}

static AdcDbmDriverStatus QuiesceOwnedHardware(
    AdcDbmDriverStopReport *report,
    uint32_t settle)
{
    HAL_StatusTypeDef hal_status;
    AdcDbmDriverStopReport local_report;
    uint32_t remaining;

    (void)HAL_TIM_Base_Stop(&htim2);
    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);

    DisableDmaInterrupts();

    if (settle != 0U)
    {
        DelayCycles(ADC_DBM_STOP_SETTLE_CYCLES);
    }

    remaining = DMA2_Stream0->NDTR;
    local_report.remaining_samples = remaining;
    local_report.captured_samples =
        (remaining <= driver.arm.block_samples) ?
        (driver.arm.block_samples - remaining) : 0U;
    local_report.active_slot = CurrentCt();
    local_report.dma_lisr_before_stop = DMA2->LISR;
    local_report.adc_sr_before_stop = ADC1->SR;

    hal_status = HAL_ADC_Stop_DMA(&hadc1);

    local_report.dma_lisr_after_stop = DMA2->LISR;
    local_report.dma_cr_after_stop = DMA2_Stream0->CR;
    local_report.adc_cr2_after_stop = ADC1->CR2;
    local_report.tim2_cr1_after_stop = TIM2->CR1;

    ClearDmaFlags();
    HAL_NVIC_ClearPendingIRQ(DMA2_Stream0_IRQn);
    __DSB();

    if (HardwareQuiescent())
    {
        CLEAR_BIT(DMA2_Stream0->CR, DMA_SxCR_DBM | DMA_SxCR_CT);
        driver.hardware_owned = 0U;
        driver.callback_active = 0U;
    }

    if (report != NULL)
    {
        *report = local_report;
    }

    if (!HardwareQuiescent())
    {
        return ADC_DBM_DRIVER_HARDWARE_ERROR;
    }

    if (hal_status != HAL_OK)
    {
        return ADC_DBM_DRIVER_HAL_ERROR;
    }

    return ADC_DBM_DRIVER_OK;
}

static AdcDbmDriverStatus AbortFailedArm(void)
{
    HAL_StatusTypeDef dma_status;

    (void)HAL_TIM_Base_Stop(&htim2);
    DisableDmaInterrupts();

    dma_status = HAL_DMA_Abort(&hdma_adc1);
    (void)HAL_ADC_Stop(&hadc1);

    ClearDmaFlags();
    HAL_NVIC_ClearPendingIRQ(DMA2_Stream0_IRQn);
    __DSB();

    if (HardwareQuiescent())
    {
        CLEAR_BIT(DMA2_Stream0->CR, DMA_SxCR_DBM | DMA_SxCR_CT);
        driver.hardware_owned = 0U;
        driver.callback_active = 0U;
    }

    if (!HardwareQuiescent())
    {
        return ADC_DBM_DRIVER_HARDWARE_ERROR;
    }

    return (dma_status == HAL_OK) ?
        ADC_DBM_DRIVER_OK : ADC_DBM_DRIVER_HAL_ERROR;
}

static void DispatchCompletion(
    DMA_HandleTypeDef *hdma,
    AdcDbmDriverSlot completed_slot)
{
    AdcDbmDriverCompleteCallback callback;
    AdcDbmDriverCompletionEvent event;
    void *context;
    uint32_t next_sequence;

    if (hdma != &hdma_adc1)
    {
        return;
    }

    if ((driver.state != ADC_DBM_DRIVER_STATE_RUNNING) ||
        (driver.hardware_owned == 0U) ||
        (driver.address_map_trusted == 0U))
    {
        ++driver.suppressed_completion_count;
        return;
    }

    next_sequence = driver.completion_count + 1U;
    if (next_sequence == 0U)
    {
        ++driver.suppressed_completion_count;
        LatchRealtimeHardwareError();
        return;
    }

    driver.completion_count = next_sequence;
    driver.callback_active = 1U;
    driver.callback_sequence = next_sequence;
    driver.callback_completed_slot = (uint32_t)completed_slot;
    driver.callback_action_applied = 0U;

    event.sequence = next_sequence;
    event.completed_slot = completed_slot;
    event.callback_cycle = DwtNow();

    callback = driver.arm.complete_callback;
    context = driver.arm.callback_context;

    if (callback != NULL)
    {
        callback(&event, context);
    }

    if ((driver.state == ADC_DBM_DRIVER_STATE_RUNNING) &&
        (driver.callback_action_applied == 0U))
    {
        ++driver.inactive_failure_count;
        LatchRealtimeHardwareError();
    }

    driver.callback_active = 0U;
}

static void DmaM0Complete(DMA_HandleTypeDef *hdma)
{
    DispatchCompletion(hdma, ADC_DBM_DRIVER_SLOT_M0);
}

static void DmaM1Complete(DMA_HandleTypeDef *hdma)
{
    DispatchCompletion(hdma, ADC_DBM_DRIVER_SLOT_M1);
}

static void DmaError(DMA_HandleTypeDef *hdma)
{
    AdcDbmDriverErrorCallback callback;
    void *context;

    if (hdma != &hdma_adc1)
    {
        return;
    }

    ++driver.dma_error_count;
    LatchRealtimeHardwareError();

    callback = driver.arm.error_callback;
    context = driver.arm.callback_context;

    if (callback != NULL)
    {
        callback(hdma->ErrorCode, ADC1->SR, context);
    }
}

void AdcDbmDriver_Init(void)
{
    uint32_t primask;

    EnsureDwtEnabled();
    primask = EnterCritical();

    driver.arm.m0_address = 0U;
    driver.arm.m1_address = 0U;
    driver.arm.block_samples = 0U;
    driver.arm.complete_callback = NULL;
    driver.arm.error_callback = NULL;
    driver.arm.callback_context = NULL;
    driver.rebind_policy.block_samples = 0U;
    driver.rebind_policy.block_cycles = 0U;
    driver.rebind_policy.write_limit_cycles = 0U;
    driver.rebind_policy.final_reserve_cycles = 0U;
    driver.rebind_policy.min_remaining_samples = 0U;
    driver.hardware_owned = 0U;
    driver.address_map_trusted = 0U;
    driver.start_epoch_cycle = 0U;
    driver.completion_count = 0U;
    driver.callback_active = 0U;
    driver.callback_sequence = 0U;
    driver.callback_completed_slot = 0U;
    driver.callback_action_applied = 0U;
    driver.bound_address[ADC_DBM_DRIVER_SLOT_M0] = 0U;
    driver.bound_address[ADC_DBM_DRIVER_SLOT_M1] = 0U;
    driver.suppressed_completion_count = 0U;
    driver.dma_error_count = 0U;
    driver.inactive_keep_success_count = 0U;
    driver.inactive_rebind_success_count = 0U;
    driver.inactive_failure_count = 0U;

    if ((hadc1.Instance != ADC1) ||
        (htim2.Instance != TIM2) ||
        (hdma_adc1.Instance != DMA2_Stream0) ||
        !HardwareQuiescent())
    {
        driver.state = ADC_DBM_DRIVER_STATE_ERROR;
    }
    else
    {
        driver.state = ADC_DBM_DRIVER_STATE_STOPPED;
    }

    ExitCritical(primask);
}

AdcDbmDriverStatus AdcDbmDriver_Arm(
    const AdcDbmDriverArmConfig *config)
{
    HAL_StatusTypeDef hal_status;
    AdcDbmDriverStatus config_status;
    AdcDbmDriverStatus cleanup_status;
    AdcDbmRebindGuardPolicy policy;

    if ((config == NULL) ||
        !SpanValid(config->m0_address, config->block_samples) ||
        !SpanValid(config->m1_address, config->block_samples) ||
        SpansOverlap(
            config->m0_address,
            config->m1_address,
            config->block_samples) ||
        (config->complete_callback == NULL) ||
        (AdcDbmRebindGuard_DerivePolicy(
            config->block_samples, &policy) != ADC_DBM_REBIND_GUARD_OK))
    {
        return ADC_DBM_DRIVER_INVALID_ARGUMENT;
    }

    if (driver.state == ADC_DBM_DRIVER_STATE_UNINITIALIZED)
    {
        return ADC_DBM_DRIVER_INVALID_STATE;
    }
    if ((driver.state != ADC_DBM_DRIVER_STATE_STOPPED) ||
        (driver.hardware_owned != 0U))
    {
        return ADC_DBM_DRIVER_BUSY;
    }

    config_status = ConfigurationValid();
    if (config_status != ADC_DBM_DRIVER_OK)
    {
        return config_status;
    }

    EnsureDwtEnabled();
    driver.arm = *config;
    driver.rebind_policy = policy;
    driver.address_map_trusted = 0U;
    driver.start_epoch_cycle = 0U;
    driver.completion_count = 0U;
    driver.callback_active = 0U;
    driver.callback_sequence = 0U;
    driver.callback_completed_slot = 0U;
    driver.callback_action_applied = 0U;
    driver.suppressed_completion_count = 0U;
    driver.dma_error_count = 0U;
    driver.inactive_keep_success_count = 0U;
    driver.inactive_rebind_success_count = 0U;
    driver.inactive_failure_count = 0U;

    (void)HAL_TIM_Base_Stop(&htim2);
    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);

    CLEAR_BIT(DMA2_Stream0->CR, DMA_SxCR_DBM | DMA_SxCR_CT);
    DisableDmaInterrupts();
    ClearDmaFlags();
    HAL_NVIC_ClearPendingIRQ(DMA2_Stream0_IRQn);

    hdma_adc1.XferCpltCallback = DmaM0Complete;
    hdma_adc1.XferM1CpltCallback = DmaM1Complete;
    hdma_adc1.XferHalfCpltCallback = NULL;
    hdma_adc1.XferM1HalfCpltCallback = NULL;
    hdma_adc1.XferErrorCallback = DmaError;
    hdma_adc1.XferAbortCallback = NULL;

    hal_status = HAL_DMAEx_MultiBufferStart_IT(
        &hdma_adc1,
        (uint32_t)(uintptr_t)&ADC1->DR,
        config->m0_address,
        config->m1_address,
        config->block_samples);

    if (hal_status != HAL_OK)
    {
        driver.state = ADC_DBM_DRIVER_STATE_ERROR;
        return ADC_DBM_DRIVER_HAL_ERROR;
    }

    driver.hardware_owned = 1U;

    __HAL_DMA_DISABLE_IT(&hdma_adc1, DMA_IT_HT);
    __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_EOC | ADC_FLAG_OVR);

    hal_status = HAL_ADC_Start(&hadc1);
    if (hal_status != HAL_OK)
    {
        cleanup_status = AbortFailedArm();
        driver.state = ADC_DBM_DRIVER_STATE_ERROR;
        return (cleanup_status == ADC_DBM_DRIVER_OK) ?
            ADC_DBM_DRIVER_HAL_ERROR : cleanup_status;
    }

    SET_BIT(ADC1->CR2, ADC_CR2_DMA);

    if ((ADC1->CR2 & ADC_CR2_DDS) == 0U)
    {
        cleanup_status = QuiesceOwnedHardware(NULL, 0U);
        driver.state = ADC_DBM_DRIVER_STATE_ERROR;
        return (cleanup_status == ADC_DBM_DRIVER_OK) ?
            ADC_DBM_DRIVER_CONFIG_ERROR : cleanup_status;
    }

    if ((DMA2_Stream0->CR != ADC_DBM_R2_REQUIRED_DMA_CR) ||
        (DMA2_Stream0->NDTR != config->block_samples) ||
        (DMA2_Stream0->M0AR != config->m0_address) ||
        (DMA2_Stream0->M1AR != config->m1_address) ||
        (ADC1->CR2 != ADC_DBM_R2_REQUIRED_ADC_CR2) ||
        ((TIM2->CR1 & TIM_CR1_CEN) != 0U))
    {
        cleanup_status = QuiesceOwnedHardware(NULL, 0U);
        driver.state = ADC_DBM_DRIVER_STATE_ERROR;
        return (cleanup_status == ADC_DBM_DRIVER_OK) ?
            ADC_DBM_DRIVER_HARDWARE_ERROR : cleanup_status;
    }

    driver.bound_address[ADC_DBM_DRIVER_SLOT_M0] = config->m0_address;
    driver.bound_address[ADC_DBM_DRIVER_SLOT_M1] = config->m1_address;
    driver.address_map_trusted = 1U;
    driver.state = ADC_DBM_DRIVER_STATE_ARMED;
    return ADC_DBM_DRIVER_OK;
}

AdcDbmDriverStatus AdcDbmDriver_CommitStart(void)
{
    HAL_StatusTypeDef hal_status;
    AdcDbmDriverStatus cleanup_status;
    uint32_t primask;

    if ((driver.state != ADC_DBM_DRIVER_STATE_ARMED) ||
        (driver.hardware_owned == 0U) ||
        (driver.address_map_trusted == 0U))
    {
        return ADC_DBM_DRIVER_INVALID_STATE;
    }

    primask = EnterCritical();
    driver.state = ADC_DBM_DRIVER_STATE_RUNNING;
    driver.start_epoch_cycle = DwtNow();
    hal_status = HAL_TIM_Base_Start(&htim2);
    __DSB();

    if (hal_status == HAL_OK)
    {
        ExitCritical(primask);
        return ADC_DBM_DRIVER_OK;
    }

    driver.state = ADC_DBM_DRIVER_STATE_STOPPING;
    ExitCritical(primask);

    cleanup_status = QuiesceOwnedHardware(NULL, 0U);
    driver.state = ADC_DBM_DRIVER_STATE_ERROR;

    return (cleanup_status == ADC_DBM_DRIVER_OK) ?
        ADC_DBM_DRIVER_HAL_ERROR : cleanup_status;
}

AdcDbmDriverStatus AdcDbmDriver_ApplyInactiveAction(
    const AdcDbmDriverCompletionEvent *event,
    const AdcDbmDriverInactiveRequest *request,
    AdcDbmDriverInactiveReport *report)
{
    AdcDbmDriverInactiveReport local_report;
    AdcDbmDriverSlot active_slot;
    volatile uint32_t *inactive_register;
    volatile uint32_t *active_register;
    AdcDbmRebindGuardStatus guard_status;
    uint32_t primask;
    uint32_t critical_start;
    uint32_t now;
    uint32_t hardware_ok;
    uint32_t elapsed;
    uint32_t nominal;
    uint32_t expected_inactive_after;
    uint32_t is_rebind;

    if ((driver.state != ADC_DBM_DRIVER_STATE_RUNNING) ||
        (driver.hardware_owned == 0U) ||
        (driver.address_map_trusted == 0U) ||
        (driver.callback_active == 0U))
    {
        return ADC_DBM_DRIVER_INVALID_STATE;
    }

    if (driver.callback_action_applied != 0U)
    {
        ++driver.inactive_failure_count;
        LatchRealtimeHardwareError();
        return ADC_DBM_DRIVER_COMPLETION_CONTEXT_ERROR;
    }

    if ((event == NULL) || (request == NULL))
    {
        ++driver.inactive_failure_count;
        LatchRealtimeHardwareError();
        return ADC_DBM_DRIVER_INVALID_ARGUMENT;
    }

    if (((request->completed_slot != ADC_DBM_DRIVER_SLOT_M0) &&
         (request->completed_slot != ADC_DBM_DRIVER_SLOT_M1)) ||
        ((request->action != ADC_DBM_DRIVER_INACTIVE_KEEP) &&
         (request->action != ADC_DBM_DRIVER_INACTIVE_REBIND)))
    {
        ++driver.inactive_failure_count;
        LatchRealtimeHardwareError();
        return ADC_DBM_DRIVER_INVALID_ARGUMENT;
    }

    is_rebind = (request->action == ADC_DBM_DRIVER_INACTIVE_REBIND) ? 1U : 0U;

    if (!SpanValid(
            request->expected_completed_address,
            driver.arm.block_samples) ||
        !SpanValid(
            request->expected_active_address,
            driver.arm.block_samples) ||
        SpansOverlap(
            request->expected_completed_address,
            request->expected_active_address,
            driver.arm.block_samples) ||
        ((is_rebind == 0U) && (request->replacement_address != 0U)) ||
        ((is_rebind != 0U) &&
         (!SpanValid(
             request->replacement_address,
             driver.arm.block_samples) ||
          SpansOverlap(
             request->replacement_address,
             request->expected_active_address,
             driver.arm.block_samples) ||
          SpansOverlap(
             request->replacement_address,
             request->expected_completed_address,
             driver.arm.block_samples))))
    {
        ++driver.inactive_failure_count;
        LatchRealtimeHardwareError();
        return ADC_DBM_DRIVER_INVALID_ARGUMENT;
    }

    if ((event->sequence != driver.callback_sequence) ||
        (event->sequence != request->sequence) ||
        ((uint32_t)event->completed_slot !=
            driver.callback_completed_slot) ||
        (event->completed_slot != request->completed_slot))
    {
        ++driver.inactive_failure_count;
        LatchRealtimeHardwareError();
        return ADC_DBM_DRIVER_COMPLETION_CONTEXT_ERROR;
    }

    active_slot = OtherSlot(request->completed_slot);
    inactive_register = AddressRegister(request->completed_slot);
    active_register = AddressRegister(active_slot);

    if ((driver.bound_address[request->completed_slot] !=
            request->expected_completed_address) ||
        (driver.bound_address[active_slot] !=
            request->expected_active_address))
    {
        ++driver.inactive_failure_count;
        driver.address_map_trusted = 0U;
        LatchRealtimeHardwareError();
        return ADC_DBM_DRIVER_COMPLETION_ADDRESS_ERROR;
    }

    local_report.sequence = request->sequence;
    local_report.completed_slot = (uint32_t)request->completed_slot;
    local_report.action = (uint32_t)request->action;
    local_report.guard_status = (uint32_t)ADC_DBM_REBIND_GUARD_OK;
    local_report.ct_prewrite = UINT32_MAX;
    local_report.ct_after = UINT32_MAX;
    local_report.ndtr_prewrite = UINT32_MAX;
    local_report.active_address_before = *active_register;
    local_report.inactive_address_before = *inactive_register;
    local_report.active_address_after = local_report.active_address_before;
    local_report.inactive_address_after = local_report.inactive_address_before;
    local_report.replacement_address = request->replacement_address;
    local_report.critical_window_cycles = 0U;
    local_report.nominal_to_decision_cycles = 0U;

    primask = EnterCritical();
    critical_start = DwtNow();

    local_report.ct_prewrite = CurrentCt();
    local_report.ndtr_prewrite = DMA2_Stream0->NDTR;
    local_report.active_address_before = *active_register;
    local_report.inactive_address_before = *inactive_register;
    hardware_ok = HardwareHealthyRunning() ? 1U : 0U;
    now = DwtNow();
    elapsed = (uint32_t)(now - driver.start_epoch_cycle);

    guard_status = AdcDbmRebindGuard_Check(
        &driver.rebind_policy,
        request->sequence,
        (uint32_t)request->completed_slot,
        local_report.ct_prewrite,
        elapsed,
        local_report.ndtr_prewrite,
        hardware_ok);

    local_report.guard_status = (uint32_t)guard_status;

    if ((guard_status != ADC_DBM_REBIND_GUARD_OK) ||
        (local_report.active_address_before !=
            request->expected_active_address) ||
        (local_report.inactive_address_before !=
            request->expected_completed_address))
    {
        ++driver.inactive_failure_count;
        if ((local_report.active_address_before !=
                request->expected_active_address) ||
            (local_report.inactive_address_before !=
                request->expected_completed_address))
        {
            driver.address_map_trusted = 0U;
        }
        LatchRealtimeHardwareError();
        local_report.critical_window_cycles =
            (uint32_t)(DwtNow() - critical_start);
        ExitCritical(primask);
        if (report != NULL)
        {
            *report = local_report;
        }
        return (guard_status == ADC_DBM_REBIND_GUARD_OK) ?
            ADC_DBM_DRIVER_COMPLETION_ADDRESS_ERROR :
            ADC_DBM_DRIVER_COMPLETION_GUARD_ERROR;
    }

    if (is_rebind != 0U)
    {
        *inactive_register = request->replacement_address;
        __DSB();
        expected_inactive_after = request->replacement_address;
    }
    else
    {
        __DSB();
        expected_inactive_after = request->expected_completed_address;
    }

    local_report.ct_after = CurrentCt();
    local_report.active_address_after = *active_register;
    local_report.inactive_address_after = *inactive_register;
    now = DwtNow();
    local_report.critical_window_cycles =
        (uint32_t)(now - critical_start);

    nominal = request->sequence * driver.rebind_policy.block_cycles;
    local_report.nominal_to_decision_cycles =
        (uint32_t)(now - driver.start_epoch_cycle) - nominal;

    if ((local_report.ct_after != local_report.ct_prewrite) ||
        (local_report.active_address_after !=
            request->expected_active_address) ||
        (local_report.inactive_address_after != expected_inactive_after) ||
        !HardwareHealthyRunning() ||
        (local_report.critical_window_cycles >
            driver.rebind_policy.final_reserve_cycles) ||
        (local_report.nominal_to_decision_cycles >
            driver.rebind_policy.write_limit_cycles))
    {
        ++driver.inactive_failure_count;
        driver.address_map_trusted = 0U;
        LatchRealtimeHardwareError();
        ExitCritical(primask);
        if (report != NULL)
        {
            *report = local_report;
        }
        return ADC_DBM_DRIVER_COMPLETION_VERIFY_ERROR;
    }

    if (is_rebind != 0U)
    {
        driver.bound_address[request->completed_slot] =
            request->replacement_address;
        ++driver.inactive_rebind_success_count;
    }
    else
    {
        ++driver.inactive_keep_success_count;
    }

    driver.callback_action_applied = 1U;
    ExitCritical(primask);

    if (report != NULL)
    {
        *report = local_report;
    }

    return ADC_DBM_DRIVER_OK;
}

AdcDbmDriverStatus AdcDbmDriver_FailActiveCompletion(
    const AdcDbmDriverCompletionEvent *event)
{
    if ((driver.state != ADC_DBM_DRIVER_STATE_RUNNING) ||
        (driver.hardware_owned == 0U) ||
        (driver.callback_active == 0U))
    {
        return ADC_DBM_DRIVER_INVALID_STATE;
    }

    if (event == NULL)
    {
        ++driver.inactive_failure_count;
        LatchRealtimeHardwareError();
        return ADC_DBM_DRIVER_INVALID_ARGUMENT;
    }

    if ((event->sequence != driver.callback_sequence) ||
        ((uint32_t)event->completed_slot !=
            driver.callback_completed_slot))
    {
        ++driver.inactive_failure_count;
        LatchRealtimeHardwareError();
        return ADC_DBM_DRIVER_COMPLETION_CONTEXT_ERROR;
    }

    ++driver.inactive_failure_count;
    LatchRealtimeHardwareError();
    return ADC_DBM_DRIVER_OK;
}

AdcDbmDriverStatus AdcDbmDriver_Stop(
    AdcDbmDriverStopReport *report)
{
    AdcDbmDriverStatus stop_status;

    if ((driver.state != ADC_DBM_DRIVER_STATE_RUNNING) &&
        (driver.state != ADC_DBM_DRIVER_STATE_ARMED) &&
        (driver.state != ADC_DBM_DRIVER_STATE_ERROR))
    {
        return ADC_DBM_DRIVER_INVALID_STATE;
    }

    if (driver.hardware_owned == 0U)
    {
        return ADC_DBM_DRIVER_INVALID_STATE;
    }

    driver.state = ADC_DBM_DRIVER_STATE_STOPPING;
    stop_status = QuiesceOwnedHardware(report, 1U);

    driver.state = (stop_status == ADC_DBM_DRIVER_OK) ?
        ADC_DBM_DRIVER_STATE_STOPPED :
        ADC_DBM_DRIVER_STATE_ERROR;

    return stop_status;
}

AdcDbmDriverStatus AdcDbmDriver_GetSnapshot(
    AdcDbmDriverSnapshot *out)
{
    uint32_t primask;

    if (out == NULL)
    {
        return ADC_DBM_DRIVER_INVALID_ARGUMENT;
    }

    primask = EnterCritical();
    out->state = driver.state;
    out->hardware_owned = driver.hardware_owned;
    out->address_map_trusted = driver.address_map_trusted;
    out->block_samples = driver.arm.block_samples;
    out->start_epoch_cycle = driver.start_epoch_cycle;
    out->completion_count = driver.completion_count;
    out->bound_m0_address =
        driver.bound_address[ADC_DBM_DRIVER_SLOT_M0];
    out->bound_m1_address =
        driver.bound_address[ADC_DBM_DRIVER_SLOT_M1];
    out->dma_cr = DMA2_Stream0->CR;
    out->dma_ndtr = DMA2_Stream0->NDTR;
    out->dma_m0ar = DMA2_Stream0->M0AR;
    out->dma_m1ar = DMA2_Stream0->M1AR;
    out->dma_ct = CurrentCt();
    out->dma_lisr = DMA2->LISR;
    out->adc_sr = ADC1->SR;
    out->adc_cr2 = ADC1->CR2;
    out->tim2_cr1 = TIM2->CR1;
    out->suppressed_completion_count =
        driver.suppressed_completion_count;
    out->dma_error_count = driver.dma_error_count;
    out->inactive_keep_success_count =
        driver.inactive_keep_success_count;
    out->inactive_rebind_success_count =
        driver.inactive_rebind_success_count;
    out->inactive_failure_count = driver.inactive_failure_count;
    ExitCritical(primask);

    return ADC_DBM_DRIVER_OK;
}

AdcDbmDriverState AdcDbmDriver_GetState(void)
{
    return driver.state;
}
