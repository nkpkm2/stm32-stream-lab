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

/*
 * Frozen R2 compatibility profile. These are the same armed-state register
 * values proven by the W3-W6 hardware profiles. CT is zero before TIM2 starts.
 * Later sampling-rate generalization must be an explicit reviewed change, not
 * an implicit weakening of the R2 extraction gate.
 */
#define ADC_DBM_R2_REQUIRED_DMA_CR  0x00062D17U
#define ADC_DBM_R2_REQUIRED_ADC_CR2 0x16000701U

typedef struct
{
    volatile AdcDbmDriverState state;
    volatile uint32_t hardware_owned;
    AdcDbmDriverArmConfig arm;
    volatile uint32_t suppressed_completion_count;
    volatile uint32_t dma_error_count;
} AdcDbmDriverStorage;

static AdcDbmDriverStorage driver =
{
    ADC_DBM_DRIVER_STATE_UNINITIALIZED,
    0U,
    {0U, 0U, 0U, NULL, NULL, NULL},
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

    /*
     * HAL_ADC_Start may have failed before the ADC entered a DMA-running state,
     * so do not rely on HAL_ADC_Stop_DMA alone to disable the already-armed
     * stream. Abort the stream explicitly, then ask HAL to stop ADC state.
     */
    dma_status = HAL_DMA_Abort(&hdma_adc1);
    (void)HAL_ADC_Stop(&hadc1);

    ClearDmaFlags();
    HAL_NVIC_ClearPendingIRQ(DMA2_Stream0_IRQn);
    __DSB();

    if (HardwareQuiescent())
    {
        CLEAR_BIT(DMA2_Stream0->CR, DMA_SxCR_DBM | DMA_SxCR_CT);
        driver.hardware_owned = 0U;
    }

    if (!HardwareQuiescent())
    {
        return ADC_DBM_DRIVER_HARDWARE_ERROR;
    }

    return (dma_status == HAL_OK) ?
        ADC_DBM_DRIVER_OK : ADC_DBM_DRIVER_HAL_ERROR;
}

static void DmaM0Complete(DMA_HandleTypeDef *hdma)
{
    AdcDbmDriverCompleteCallback callback;
    void *context;

    if (hdma != &hdma_adc1)
    {
        return;
    }

    if (driver.state != ADC_DBM_DRIVER_STATE_RUNNING)
    {
        ++driver.suppressed_completion_count;
        return;
    }

    callback = driver.arm.complete_callback;
    context = driver.arm.callback_context;

    if (callback != NULL)
    {
        callback(ADC_DBM_DRIVER_SLOT_M0, context);
    }
}

static void DmaM1Complete(DMA_HandleTypeDef *hdma)
{
    AdcDbmDriverCompleteCallback callback;
    void *context;

    if (hdma != &hdma_adc1)
    {
        return;
    }

    if (driver.state != ADC_DBM_DRIVER_STATE_RUNNING)
    {
        ++driver.suppressed_completion_count;
        return;
    }

    callback = driver.arm.complete_callback;
    context = driver.arm.callback_context;

    if (callback != NULL)
    {
        callback(ADC_DBM_DRIVER_SLOT_M1, context);
    }
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
    CLEAR_BIT(TIM2->CR1, TIM_CR1_CEN);
    DisableDmaInterrupts();
    __DSB();
    driver.state = ADC_DBM_DRIVER_STATE_ERROR;

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
    driver.hardware_owned = 0U;
    driver.suppressed_completion_count = 0U;
    driver.dma_error_count = 0U;

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

    if ((config == NULL) ||
        !SpanValid(config->m0_address, config->block_samples) ||
        !SpanValid(config->m1_address, config->block_samples) ||
        SpansOverlap(
            config->m0_address,
            config->m1_address,
            config->block_samples) ||
        (config->complete_callback == NULL))
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
    driver.suppressed_completion_count = 0U;
    driver.dma_error_count = 0U;

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

    driver.state = ADC_DBM_DRIVER_STATE_ARMED;
    return ADC_DBM_DRIVER_OK;
}

AdcDbmDriverStatus AdcDbmDriver_CommitStart(void)
{
    HAL_StatusTypeDef hal_status;
    AdcDbmDriverStatus cleanup_status;
    uint32_t primask;

    if ((driver.state != ADC_DBM_DRIVER_STATE_ARMED) ||
        (driver.hardware_owned == 0U))
    {
        return ADC_DBM_DRIVER_INVALID_STATE;
    }

    primask = EnterCritical();
    driver.state = ADC_DBM_DRIVER_STATE_RUNNING;
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
    out->block_samples = driver.arm.block_samples;
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
    ExitCritical(primask);

    return ADC_DBM_DRIVER_OK;
}

AdcDbmDriverState AdcDbmDriver_GetState(void)
{
    return driver.state;
}
