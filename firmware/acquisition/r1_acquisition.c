#include "r1_acquisition.h"

#include "main.h"
#include "stm32f4xx_hal_dma_ex.h"

#include <limits.h>

extern ADC_HandleTypeDef hadc1;
extern DMA_HandleTypeDef hdma_adc1;
extern TIM_HandleTypeDef htim2;

#define R1_INVALID_TARGET 0xFFFFFFFFU
#define R1_SUSPECTED_LOSS_THRESHOLD_CYCLES \
    ((R1_ACQUISITION_EXPECTED_BLOCK_CYCLES * 3U) / 2U)
#define R1_STOP_SETTLE_CYCLES 1200U

static uint16_t r1_buffer0[R1_ACQUISITION_BLOCK_SAMPLES]
    __attribute__((aligned(4)));
static uint16_t r1_buffer1[R1_ACQUISITION_BLOCK_SAMPLES]
    __attribute__((aligned(4)));

static volatile R1_AcquisitionState r1_state =
    R1_ACQUISITION_STATE_UNINITIALIZED;
static volatile R1_AcquisitionDiagnostics r1_diag;
static volatile R1_AcquisitionTraceEntry
    r1_trace[R1_ACQUISITION_TRACE_CAPACITY];
static volatile uint32_t r1_last_dma_lisr = 0U;
static volatile uint32_t r1_expected_next_target = R1_INVALID_TARGET;
static volatile uint32_t r1_have_previous_tc = 0U;
static volatile uint32_t r1_ever_started = 0U;

static uint32_t R1_EnterCritical(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __DSB();
    __ISB();
    return primask;
}

static void R1_ExitCritical(uint32_t primask)
{
    __DSB();
    __ISB();
    if ((primask & 1U) == 0U)
    {
        __enable_irq();
    }
}

static void R1_DwtEnsureEnabled(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static uint32_t R1_DwtNow(void)
{
    return DWT->CYCCNT;
}

static void R1_DwtDelayCycles(uint32_t cycles)
{
    uint32_t start = R1_DwtNow();
    while ((uint32_t)(R1_DwtNow() - start) < cycles)
    {
        __NOP();
    }
}

static void R1_ResetDiagnostics(void)
{
    uint32_t primask = R1_EnterCritical();
    uint32_t i;

    r1_diag.start_count = 0U;
    r1_diag.restart_count = 0U;
    r1_diag.tc_count = 0U;
    r1_diag.m0_complete_count = 0U;
    r1_diag.m1_complete_count = 0U;
    r1_diag.ct_mismatch_count = 0U;
    r1_diag.alternation_mismatch_count = 0U;
    r1_diag.suspected_event_loss_count = 0U;
    r1_diag.adc_ovr_count = 0U;
    r1_diag.dma_te_count = 0U;
    r1_diag.dma_dme_count = 0U;
    r1_diag.dma_fe_count = 0U;
    r1_diag.dma_other_error_count = 0U;
    r1_diag.timing_interval_count = 0U;
    r1_diag.previous_tc_cyccnt = 0U;
    r1_diag.last_delta_cycles = 0U;
    r1_diag.min_delta_cycles = UINT32_MAX;
    r1_diag.max_delta_cycles = 0U;
    r1_diag.sum_delta_cycles = 0U;
    r1_diag.trace_count = 0U;

    r1_diag.start_dma_cr = 0U;
    r1_diag.start_dma_ndtr = 0U;
    r1_diag.start_dma_m0ar = 0U;
    r1_diag.start_dma_m1ar = 0U;
    r1_diag.start_dma_ct = 0U;
    r1_diag.start_adc_cr2 = 0U;
    r1_diag.start_tim2_cr1 = 0U;
    r1_diag.partial_stop_count = 0U;
    r1_diag.stop_remaining_ndtr = 0U;
    r1_diag.stop_captured_samples = 0U;
    r1_diag.stop_active_target = 0U;
    r1_diag.stop_artifact_count = 0U;
    r1_diag.state = r1_state;

    r1_last_dma_lisr = 0U;
    r1_expected_next_target = R1_INVALID_TARGET;
    r1_have_previous_tc = 0U;

    for (i = 0U; i < R1_ACQUISITION_TRACE_CAPACITY; ++i)
    {
        r1_trace[i].sequence = 0U;
        r1_trace[i].cyccnt = 0U;
        r1_trace[i].dma_lisr = 0U;
        r1_trace[i].completed_target = 0U;
        r1_trace[i].active_target = 0U;
        r1_trace[i].adc_ovr_observed = 0U;
    }

    R1_ExitCritical(primask);
}

static uint32_t R1_ReadActiveTarget(void)
{
    return ((hdma_adc1.Instance->CR & DMA_SxCR_CT) != 0U) ? 1U : 0U;
}

static uint32_t R1_CheckAndClearAdcOverrun(void)
{
    if (__HAL_ADC_GET_FLAG(&hadc1, ADC_FLAG_OVR) != RESET)
    {
        ++r1_diag.adc_ovr_count;
        __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_OVR);
        return 1U;
    }
    return 0U;
}

static void R1_RecordTrace(
    uint32_t now,
    uint32_t completed_target,
    uint32_t active_target,
    uint32_t adc_ovr_observed)
{
    uint32_t index = r1_diag.trace_count;
    if (index >= R1_ACQUISITION_TRACE_CAPACITY)
    {
        return;
    }

    r1_trace[index].sequence = r1_diag.tc_count;
    r1_trace[index].cyccnt = now;
    r1_trace[index].dma_lisr = r1_last_dma_lisr;
    r1_trace[index].completed_target = completed_target;
    r1_trace[index].active_target = active_target;
    r1_trace[index].adc_ovr_observed = adc_ovr_observed;
    r1_diag.trace_count = index + 1U;
}

static void R1_UpdateTiming(uint32_t now)
{
    uint32_t delta;
    uint32_t blocks_elapsed;

    if (r1_have_previous_tc == 0U)
    {
        r1_diag.previous_tc_cyccnt = now;
        r1_have_previous_tc = 1U;
        return;
    }

    delta = (uint32_t)(now - r1_diag.previous_tc_cyccnt);
    r1_diag.previous_tc_cyccnt = now;
    r1_diag.last_delta_cycles = delta;
    ++r1_diag.timing_interval_count;
    r1_diag.sum_delta_cycles += (uint64_t)delta;

    if (delta < r1_diag.min_delta_cycles)
    {
        r1_diag.min_delta_cycles = delta;
    }
    if (delta > r1_diag.max_delta_cycles)
    {
        r1_diag.max_delta_cycles = delta;
    }

    if (delta > R1_SUSPECTED_LOSS_THRESHOLD_CYCLES)
    {
        blocks_elapsed =
            (delta + (R1_ACQUISITION_EXPECTED_BLOCK_CYCLES / 2U)) /
            R1_ACQUISITION_EXPECTED_BLOCK_CYCLES;
        if (blocks_elapsed > 1U)
        {
            r1_diag.suspected_event_loss_count += blocks_elapsed - 1U;
        }
    }
}

static void R1_OnTransferComplete(
    DMA_HandleTypeDef *hdma,
    uint32_t completed_target)
{
    uint32_t now;
    uint32_t active_target;
    uint32_t expected_active_target;
    uint32_t adc_ovr_observed;

    if (hdma != &hdma_adc1)
    {
        return;
    }

    if (r1_state != R1_ACQUISITION_STATE_RUNNING)
    {
        ++r1_diag.stop_artifact_count;
        return;
    }

    now = R1_DwtNow();
    active_target = R1_ReadActiveTarget();
    expected_active_target = completed_target ^ 1U;

    if (active_target != expected_active_target)
    {
        ++r1_diag.ct_mismatch_count;
    }

    if ((r1_expected_next_target != R1_INVALID_TARGET) &&
        (completed_target != r1_expected_next_target))
    {
        ++r1_diag.alternation_mismatch_count;
    }
    r1_expected_next_target = completed_target ^ 1U;

    adc_ovr_observed = R1_CheckAndClearAdcOverrun();
    ++r1_diag.tc_count;

    if (completed_target == 0U)
    {
        ++r1_diag.m0_complete_count;
    }
    else
    {
        ++r1_diag.m1_complete_count;
    }

    R1_UpdateTiming(now);
    R1_RecordTrace(now, completed_target, active_target, adc_ovr_observed);
}

static void R1_DmaM0Complete(DMA_HandleTypeDef *hdma)
{
    R1_OnTransferComplete(hdma, 0U);
}

static void R1_DmaM1Complete(DMA_HandleTypeDef *hdma)
{
    R1_OnTransferComplete(hdma, 1U);
}

static void R1_DmaError(DMA_HandleTypeDef *hdma)
{
    uint32_t known;

    if (hdma != &hdma_adc1)
    {
        return;
    }

    if ((hdma->ErrorCode & HAL_DMA_ERROR_TE) != 0U)
    {
        ++r1_diag.dma_te_count;
    }
    if ((hdma->ErrorCode & HAL_DMA_ERROR_DME) != 0U)
    {
        ++r1_diag.dma_dme_count;
    }
    if ((hdma->ErrorCode & HAL_DMA_ERROR_FE) != 0U)
    {
        ++r1_diag.dma_fe_count;
    }

    known = HAL_DMA_ERROR_TE | HAL_DMA_ERROR_DME | HAL_DMA_ERROR_FE;
    if ((hdma->ErrorCode & ~known) != HAL_DMA_ERROR_NONE)
    {
        ++r1_diag.dma_other_error_count;
    }

    (void)R1_CheckAndClearAdcOverrun();
    __HAL_TIM_DISABLE(&htim2);
    r1_state = R1_ACQUISITION_STATE_ERROR;
    r1_diag.state = r1_state;
}

static void R1_ClearDmaFlags(void)
{
    __HAL_DMA_CLEAR_FLAG(&hdma_adc1, __HAL_DMA_GET_TC_FLAG_INDEX(&hdma_adc1));
    __HAL_DMA_CLEAR_FLAG(&hdma_adc1, __HAL_DMA_GET_HT_FLAG_INDEX(&hdma_adc1));
    __HAL_DMA_CLEAR_FLAG(&hdma_adc1, __HAL_DMA_GET_TE_FLAG_INDEX(&hdma_adc1));
    __HAL_DMA_CLEAR_FLAG(&hdma_adc1, __HAL_DMA_GET_DME_FLAG_INDEX(&hdma_adc1));
    __HAL_DMA_CLEAR_FLAG(&hdma_adc1, __HAL_DMA_GET_FE_FLAG_INDEX(&hdma_adc1));
}

static R1_AcquisitionStatus R1_CheckConfiguration(void)
{
    if (hadc1.Instance != ADC1 || htim2.Instance != TIM2)
    {
        return R1_ACQUISITION_CONFIG_ERROR;
    }
    if (hdma_adc1.Instance != DMA2_Stream0 ||
        hdma_adc1.Init.Channel != DMA_CHANNEL_0 ||
        hdma_adc1.Init.Direction != DMA_PERIPH_TO_MEMORY ||
        hdma_adc1.Init.PeriphDataAlignment != DMA_PDATAALIGN_HALFWORD ||
        hdma_adc1.Init.MemDataAlignment != DMA_MDATAALIGN_HALFWORD)
    {
        return R1_ACQUISITION_CONFIG_ERROR;
    }
    if (hadc1.Init.ExternalTrigConv != ADC_EXTERNALTRIGCONV_T2_TRGO ||
        hadc1.Init.ExternalTrigConvEdge != ADC_EXTERNALTRIGCONVEDGE_RISING ||
        hadc1.Init.ContinuousConvMode != DISABLE ||
        hadc1.Init.DMAContinuousRequests != ENABLE)
    {
        return R1_ACQUISITION_CONFIG_ERROR;
    }
    if (htim2.Init.Prescaler != 0U || htim2.Init.Period != 449U)
    {
        return R1_ACQUISITION_CONFIG_ERROR;
    }
    return R1_ACQUISITION_OK;
}

void R1_Acquisition_Init(void)
{
    R1_DwtEnsureEnabled();
    r1_state = R1_ACQUISITION_STATE_STOPPED;
    R1_ResetDiagnostics();
    r1_diag.state = r1_state;
}

R1_AcquisitionStatus R1_Acquisition_Start(void)
{
    HAL_StatusTypeDef status;
    R1_AcquisitionStatus config_status;

    if (r1_state == R1_ACQUISITION_STATE_UNINITIALIZED)
    {
        return R1_ACQUISITION_INVALID_STATE;
    }
    if (r1_state != R1_ACQUISITION_STATE_STOPPED)
    {
        return R1_ACQUISITION_BUSY;
    }

    config_status = R1_CheckConfiguration();
    if (config_status != R1_ACQUISITION_OK)
    {
        return config_status;
    }

    if ((hdma_adc1.Instance->CR & DMA_SxCR_EN) != 0U ||
        hdma_adc1.State != HAL_DMA_STATE_READY)
    {
        return R1_ACQUISITION_BUSY;
    }

    R1_DwtEnsureEnabled();
    R1_ResetDiagnostics();
    r1_state = R1_ACQUISITION_STATE_STARTING;
    r1_diag.state = r1_state;
    r1_diag.start_count = 1U;
    r1_diag.restart_count = (r1_ever_started != 0U) ? 1U : 0U;
    r1_ever_started = 1U;

    (void)HAL_TIM_Base_Stop(&htim2);
    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);

    CLEAR_BIT(hdma_adc1.Instance->CR, DMA_SxCR_DBM | DMA_SxCR_CT);
    R1_ClearDmaFlags();
    HAL_NVIC_ClearPendingIRQ(DMA2_Stream0_IRQn);

    hdma_adc1.XferCpltCallback = R1_DmaM0Complete;
    hdma_adc1.XferM1CpltCallback = R1_DmaM1Complete;
    hdma_adc1.XferHalfCpltCallback = NULL;
    hdma_adc1.XferM1HalfCpltCallback = NULL;
    hdma_adc1.XferErrorCallback = R1_DmaError;
    hdma_adc1.XferAbortCallback = NULL;

    status = HAL_DMAEx_MultiBufferStart_IT(
        &hdma_adc1,
        (uint32_t)(uintptr_t)&hadc1.Instance->DR,
        (uint32_t)(uintptr_t)r1_buffer0,
        (uint32_t)(uintptr_t)r1_buffer1,
        R1_ACQUISITION_BLOCK_SAMPLES);

    if (status != HAL_OK)
    {
        r1_state = R1_ACQUISITION_STATE_ERROR;
        r1_diag.state = r1_state;
        return R1_ACQUISITION_HAL_ERROR;
    }

    __HAL_DMA_DISABLE_IT(&hdma_adc1, DMA_IT_HT);
    __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_EOC | ADC_FLAG_OVR);

    status = HAL_ADC_Start(&hadc1);
    if (status != HAL_OK)
    {
        (void)HAL_DMA_Abort(&hdma_adc1);
        CLEAR_BIT(hdma_adc1.Instance->CR, DMA_SxCR_DBM | DMA_SxCR_CT);
        r1_state = R1_ACQUISITION_STATE_ERROR;
        r1_diag.state = r1_state;
        return R1_ACQUISITION_HAL_ERROR;
    }

    SET_BIT(hadc1.Instance->CR2, ADC_CR2_DMA);
    if ((hadc1.Instance->CR2 & ADC_CR2_DDS) == 0U)
    {
        (void)HAL_ADC_Stop_DMA(&hadc1);
        CLEAR_BIT(hdma_adc1.Instance->CR, DMA_SxCR_DBM | DMA_SxCR_CT);
        r1_state = R1_ACQUISITION_STATE_ERROR;
        r1_diag.state = r1_state;
        return R1_ACQUISITION_CONFIG_ERROR;
    }

    r1_diag.start_dma_cr = hdma_adc1.Instance->CR;
    r1_diag.start_dma_ndtr = hdma_adc1.Instance->NDTR;
    r1_diag.start_dma_m0ar = hdma_adc1.Instance->M0AR;
    r1_diag.start_dma_m1ar = hdma_adc1.Instance->M1AR;
    r1_diag.start_dma_ct = R1_ReadActiveTarget();
    r1_diag.start_adc_cr2 = hadc1.Instance->CR2;
    r1_diag.start_tim2_cr1 = htim2.Instance->CR1;

    r1_state = R1_ACQUISITION_STATE_RUNNING;
    r1_diag.state = r1_state;

    status = HAL_TIM_Base_Start(&htim2);
    if (status != HAL_OK)
    {
        r1_state = R1_ACQUISITION_STATE_STOPPING;
        r1_diag.state = r1_state;
        (void)HAL_ADC_Stop_DMA(&hadc1);
        CLEAR_BIT(hdma_adc1.Instance->CR, DMA_SxCR_DBM | DMA_SxCR_CT);
        r1_state = R1_ACQUISITION_STATE_ERROR;
        r1_diag.state = r1_state;
        return R1_ACQUISITION_HAL_ERROR;
    }

    return R1_ACQUISITION_OK;
}

R1_AcquisitionStatus R1_Acquisition_Stop(void)
{
    HAL_StatusTypeDef status;
    uint32_t remaining;
    uint32_t active_target;

    if ((r1_state != R1_ACQUISITION_STATE_RUNNING) &&
        (r1_state != R1_ACQUISITION_STATE_ERROR))
    {
        return R1_ACQUISITION_INVALID_STATE;
    }

    r1_state = R1_ACQUISITION_STATE_STOPPING;
    r1_diag.state = r1_state;

    (void)HAL_TIM_Base_Stop(&htim2);
    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);

    R1_DwtDelayCycles(R1_STOP_SETTLE_CYCLES);

    remaining = hdma_adc1.Instance->NDTR;
    active_target = R1_ReadActiveTarget();
    r1_diag.stop_remaining_ndtr = remaining;
    r1_diag.stop_active_target = active_target;
    r1_diag.stop_captured_samples =
        (remaining <= R1_ACQUISITION_BLOCK_SAMPLES) ?
        (R1_ACQUISITION_BLOCK_SAMPLES - remaining) : 0U;

    if ((remaining > 0U) && (remaining < R1_ACQUISITION_BLOCK_SAMPLES))
    {
        ++r1_diag.partial_stop_count;
    }

    (void)R1_CheckAndClearAdcOverrun();
    status = HAL_ADC_Stop_DMA(&hadc1);

    HAL_NVIC_ClearPendingIRQ(DMA2_Stream0_IRQn);
    R1_ClearDmaFlags();

    if ((hdma_adc1.Instance->CR & DMA_SxCR_EN) != 0U)
    {
        r1_state = R1_ACQUISITION_STATE_ERROR;
        r1_diag.state = r1_state;
        return R1_ACQUISITION_HAL_ERROR;
    }

    CLEAR_BIT(hdma_adc1.Instance->CR, DMA_SxCR_DBM | DMA_SxCR_CT);
    r1_last_dma_lisr = 0U;
    r1_expected_next_target = R1_INVALID_TARGET;
    r1_have_previous_tc = 0U;

    if (status != HAL_OK)
    {
        r1_state = R1_ACQUISITION_STATE_ERROR;
        r1_diag.state = r1_state;
        return R1_ACQUISITION_HAL_ERROR;
    }

    r1_state = R1_ACQUISITION_STATE_STOPPED;
    r1_diag.state = r1_state;
    return R1_ACQUISITION_OK;
}

void R1_Acquisition_GetDiagnostics(R1_AcquisitionDiagnostics *out)
{
    uint32_t primask;
    if (out == NULL)
    {
        return;
    }

    primask = R1_EnterCritical();
    *out = r1_diag;
    out->state = r1_state;
    R1_ExitCritical(primask);
}

size_t R1_Acquisition_CopyTrace(
    R1_AcquisitionTraceEntry *out,
    size_t capacity)
{
    uint32_t primask;
    size_t count;
    size_t i;

    if ((out == NULL) || (capacity == 0U))
    {
        return 0U;
    }

    primask = R1_EnterCritical();
    count = (size_t)r1_diag.trace_count;
    if (count > capacity)
    {
        count = capacity;
    }
    for (i = 0U; i < count; ++i)
    {
        out[i] = r1_trace[i];
    }
    R1_ExitCritical(primask);
    return count;
}

const uint16_t *R1_Acquisition_GetBuffer(uint32_t target)
{
    if (target == 0U)
    {
        return r1_buffer0;
    }
    if (target == 1U)
    {
        return r1_buffer1;
    }
    return NULL;
}

void R1_Acquisition_DmaIrqEnter(uint32_t dma2_lisr)
{
    r1_last_dma_lisr = dma2_lisr;
}
