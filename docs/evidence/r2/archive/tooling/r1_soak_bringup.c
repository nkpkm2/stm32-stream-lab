#include "r1_bringup.h"

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

#include <stddef.h>
#include <stdint.h>

extern TIM_HandleTypeDef htim2;
extern __IO uint32_t uwTick;

#define R1_SOAK_MAGIC 0x52315331U
#define R1_SOAK_TASK_STACK_WORDS 512U
#define R1_SOAK_SETTLE_MS 100U
#define R1_SOAK_QUIET_MS 5U

#define R1_SOAK_EXPECTED_BLOCK_RATE_MILLIHZ 781250U
#define R1_SOAK_MEAN_TOLERANCE_CYCLES 2304U
#define R1_SOAK_TC_COUNT_TOLERANCE 2U

volatile R1_SoakResult g_r1_soak_result;

static StaticTask_t r1_soak_task_control;
static StackType_t r1_soak_task_stack[R1_SOAK_TASK_STACK_WORDS];

static uint32_t R1_Soak_AbsDiff(uint32_t a, uint32_t b)
{
    return (a >= b) ? (a - b) : (b - a);
}

static void R1_Soak_GetRawRange(
    const uint16_t *buffer,
    uint32_t *minimum,
    uint32_t *maximum)
{
    uint32_t i;
    uint32_t min_value = 0xFFFFFFFFU;
    uint32_t max_value = 0U;

    for (i = 0U; i < R1_ACQUISITION_BLOCK_SAMPLES; ++i)
    {
        uint32_t value = buffer[i];

        if (value < min_value)
        {
            min_value = value;
        }

        if (value > max_value)
        {
            max_value = value;
        }
    }

    *minimum = min_value;
    *maximum = max_value;
}

static void R1_Soak_Classify(
    const R1_AcquisitionDiagnostics *diagnostics,
    const R1_AcquisitionDiagnostics *quiet_diagnostics)
{
    const uint16_t *buffer0;
    const uint16_t *buffer1;
    uint64_t expected_tc_numerator;
    uint64_t observed_rate_numerator;
    uint32_t mean_delta = 0U;
    uint32_t pass = 1U;
    int64_t tc_error;

    buffer0 = R1_Acquisition_GetBuffer(0U);
    buffer1 = R1_Acquisition_GetBuffer(1U);

    g_r1_soak_result.system_core_clock = SystemCoreClock;
    g_r1_soak_result.aircr = SCB->AIRCR;

    g_r1_soak_result.dbm_bit_seen =
        ((diagnostics->start_dma_cr & DMA_SxCR_DBM) != 0U) ? 1U : 0U;

    g_r1_soak_result.start_ndtr = diagnostics->start_dma_ndtr;
    g_r1_soak_result.start_ct = diagnostics->start_dma_ct;

    g_r1_soak_result.m0ar_matches_buffer0 =
        (diagnostics->start_dma_m0ar ==
         (uint32_t)(uintptr_t)buffer0) ? 1U : 0U;

    g_r1_soak_result.m1ar_matches_buffer1 =
        (diagnostics->start_dma_m1ar ==
         (uint32_t)(uintptr_t)buffer1) ? 1U : 0U;

    g_r1_soak_result.adc_dma_bit_seen =
        ((diagnostics->start_adc_cr2 & ADC_CR2_DMA) != 0U) ? 1U : 0U;

    g_r1_soak_result.adc_dds_bit_seen =
        ((diagnostics->start_adc_cr2 & ADC_CR2_DDS) != 0U) ? 1U : 0U;

    g_r1_soak_result.m0_complete_count =
        diagnostics->m0_complete_count;

    g_r1_soak_result.m1_complete_count =
        diagnostics->m1_complete_count;

    g_r1_soak_result.ct_mismatch_count =
        diagnostics->ct_mismatch_count;

    g_r1_soak_result.alternation_mismatch_count =
        diagnostics->alternation_mismatch_count;

    g_r1_soak_result.suspected_event_loss_count =
        diagnostics->suspected_event_loss_count;

    g_r1_soak_result.adc_ovr_count =
        diagnostics->adc_ovr_count;

    g_r1_soak_result.dma_te_count =
        diagnostics->dma_te_count;

    g_r1_soak_result.dma_dme_count =
        diagnostics->dma_dme_count;

    g_r1_soak_result.dma_fe_count =
        diagnostics->dma_fe_count;

    g_r1_soak_result.dma_other_error_count =
        diagnostics->dma_other_error_count;

    g_r1_soak_result.timing_interval_count =
        diagnostics->timing_interval_count;

    g_r1_soak_result.min_delta_cycles =
        diagnostics->min_delta_cycles;

    g_r1_soak_result.max_delta_cycles =
        diagnostics->max_delta_cycles;

    g_r1_soak_result.trace_count =
        diagnostics->trace_count;

    if (diagnostics->timing_interval_count != 0U)
    {
        mean_delta =
            (uint32_t)(
                diagnostics->sum_delta_cycles /
                diagnostics->timing_interval_count);
    }

    g_r1_soak_result.mean_delta_cycles = mean_delta;

    g_r1_soak_result.mean_delta_error_cycles =
        R1_Soak_AbsDiff(
            mean_delta,
            R1_ACQUISITION_EXPECTED_BLOCK_CYCLES);

    g_r1_soak_result.stop_remaining_ndtr =
        diagnostics->stop_remaining_ndtr;

    g_r1_soak_result.stop_captured_samples =
        diagnostics->stop_captured_samples;

    g_r1_soak_result.stop_active_target =
        diagnostics->stop_active_target;

    g_r1_soak_result.partial_stop_count =
        diagnostics->partial_stop_count;

    g_r1_soak_result.stop_artifact_count =
        diagnostics->stop_artifact_count;

    g_r1_soak_result.quiet_tc_count =
        quiet_diagnostics->tc_count;

    g_r1_soak_result.quiet_stop_artifact_count =
        quiet_diagnostics->stop_artifact_count;

    g_r1_soak_result.quiet_state =
        (uint32_t)quiet_diagnostics->state;

    g_r1_soak_result.actual_tc_count =
        diagnostics->tc_count;

    expected_tc_numerator =
        ((uint64_t)g_r1_soak_result.elapsed_ms * 200000ULL) +
        128000ULL;

    g_r1_soak_result.expected_tc_count =
        (uint32_t)(expected_tc_numerator / 256000ULL);

    tc_error =
        (int64_t)g_r1_soak_result.actual_tc_count -
        (int64_t)g_r1_soak_result.expected_tc_count;

    g_r1_soak_result.tc_count_error = (int32_t)tc_error;

    if (g_r1_soak_result.elapsed_ms != 0U)
    {
        observed_rate_numerator =
            (uint64_t)diagnostics->tc_count * 1000000ULL;

        g_r1_soak_result.observed_block_rate_millihz =
            (uint32_t)(
                observed_rate_numerator /
                g_r1_soak_result.elapsed_ms);
    }

    g_r1_soak_result.expected_block_rate_millihz =
        R1_SOAK_EXPECTED_BLOCK_RATE_MILLIHZ;

    R1_Soak_GetRawRange(
        buffer0,
        (uint32_t *)&g_r1_soak_result.raw0_min,
        (uint32_t *)&g_r1_soak_result.raw0_max);

    R1_Soak_GetRawRange(
        buffer1,
        (uint32_t *)&g_r1_soak_result.raw1_min,
        (uint32_t *)&g_r1_soak_result.raw1_max);

    if ((g_r1_soak_result.start_status != R1_ACQUISITION_OK) ||
        (g_r1_soak_result.stop_status != R1_ACQUISITION_OK))
    {
        pass = 0U;
    }

    if (g_r1_soak_result.elapsed_ms < R1_SOAK_DURATION_MS)
    {
        pass = 0U;
    }

    if ((g_r1_soak_result.tim2_running_after_start == 0U) ||
        (g_r1_soak_result.dbm_bit_seen == 0U) ||
        (g_r1_soak_result.start_ndtr != R1_ACQUISITION_BLOCK_SAMPLES) ||
        (g_r1_soak_result.start_ct != 0U) ||
        (g_r1_soak_result.m0ar_matches_buffer0 == 0U) ||
        (g_r1_soak_result.m1ar_matches_buffer1 == 0U) ||
        (g_r1_soak_result.adc_dma_bit_seen == 0U) ||
        (g_r1_soak_result.adc_dds_bit_seen == 0U))
    {
        pass = 0U;
    }

    if ((diagnostics->tc_count == 0U) ||
        ((diagnostics->m0_complete_count +
          diagnostics->m1_complete_count) !=
         diagnostics->tc_count) ||
        (R1_Soak_AbsDiff(
             diagnostics->m0_complete_count,
             diagnostics->m1_complete_count) > 1U))
    {
        pass = 0U;
    }

    if ((diagnostics->ct_mismatch_count != 0U) ||
        (diagnostics->alternation_mismatch_count != 0U) ||
        (diagnostics->suspected_event_loss_count != 0U) ||
        (diagnostics->adc_ovr_count != 0U) ||
        (diagnostics->dma_te_count != 0U) ||
        (diagnostics->dma_dme_count != 0U) ||
        (diagnostics->dma_fe_count != 0U) ||
        (diagnostics->dma_other_error_count != 0U))
    {
        pass = 0U;
    }

    if (diagnostics->timing_interval_count !=
        (diagnostics->tc_count - 1U))
    {
        pass = 0U;
    }

    if ((diagnostics->timing_interval_count == 0U) ||
        (g_r1_soak_result.mean_delta_error_cycles >
         R1_SOAK_MEAN_TOLERANCE_CYCLES))
    {
        pass = 0U;
    }

    if ((tc_error < -(int64_t)R1_SOAK_TC_COUNT_TOLERANCE) ||
        (tc_error > (int64_t)R1_SOAK_TC_COUNT_TOLERANCE))
    {
        pass = 0U;
    }

    if ((quiet_diagnostics->tc_count != diagnostics->tc_count) ||
        (quiet_diagnostics->stop_artifact_count !=
         diagnostics->stop_artifact_count) ||
        (quiet_diagnostics->state != R1_ACQUISITION_STATE_STOPPED))
    {
        pass = 0U;
    }

    if ((g_r1_soak_result.raw0_min > 4095U) ||
        (g_r1_soak_result.raw0_max > 4095U) ||
        (g_r1_soak_result.raw1_min > 4095U) ||
        (g_r1_soak_result.raw1_max > 4095U))
    {
        pass = 0U;
    }

    if (SystemCoreClock != 180000000U)
    {
        pass = 0U;
    }

    if (((SCB->AIRCR & SCB_AIRCR_PRIGROUP_Msk) >>
         SCB_AIRCR_PRIGROUP_Pos) != 3U)
    {
        pass = 0U;
    }

    g_r1_soak_result.soak_pass = pass;
}

static void R1_Soak_Task(void *argument)
{
    R1_AcquisitionDiagnostics diagnostics;
    R1_AcquisitionDiagnostics quiet_diagnostics;
    R1_AcquisitionStatus start_status;
    R1_AcquisitionStatus stop_status;
    TickType_t start_tick;
    TickType_t stop_tick;

    (void)argument;

    vTaskDelay(pdMS_TO_TICKS(R1_SOAK_SETTLE_MS));

    R1_Acquisition_Init();

    g_r1_soak_result.phase = R1_SOAK_PHASE_INITIALIZED;
    g_r1_soak_result.system_core_clock = SystemCoreClock;
    g_r1_soak_result.aircr = SCB->AIRCR;

    g_r1_soak_result.hal_tick_start = uwTick;

    start_status = R1_Acquisition_Start();

    g_r1_soak_result.start_status = (uint32_t)start_status;

    if (start_status != R1_ACQUISITION_OK)
    {
        g_r1_soak_result.soak_pass = 0U;
        g_r1_soak_result.phase = R1_SOAK_PHASE_FAILED;

        for (;;)
        {
            vTaskDelay(pdMS_TO_TICKS(1000U));
        }
    }

    g_r1_soak_result.tim2_running_after_start =
        ((htim2.Instance->CR1 & TIM_CR1_CEN) != 0U) ? 1U : 0U;

    start_tick = xTaskGetTickCount();
    g_r1_soak_result.start_tick = (uint32_t)start_tick;

    g_r1_soak_result.phase = R1_SOAK_PHASE_RUNNING;

    vTaskDelay(pdMS_TO_TICKS(R1_SOAK_DURATION_MS));

    stop_tick = xTaskGetTickCount();
    g_r1_soak_result.stop_tick = (uint32_t)stop_tick;
    g_r1_soak_result.elapsed_ticks =
        (uint32_t)(stop_tick - start_tick);

    g_r1_soak_result.elapsed_ms =
        (uint32_t)(
            ((uint64_t)g_r1_soak_result.elapsed_ticks * 1000ULL) /
            configTICK_RATE_HZ);

    g_r1_soak_result.phase = R1_SOAK_PHASE_STOPPING;

    stop_status = R1_Acquisition_Stop();
    g_r1_soak_result.stop_status = (uint32_t)stop_status;

    g_r1_soak_result.hal_tick_stop = uwTick;
    g_r1_soak_result.hal_tick_delta =
        g_r1_soak_result.hal_tick_stop -
        g_r1_soak_result.hal_tick_start;

    R1_Acquisition_GetDiagnostics(&diagnostics);

    vTaskDelay(pdMS_TO_TICKS(R1_SOAK_QUIET_MS));

    R1_Acquisition_GetDiagnostics(&quiet_diagnostics);

    R1_Soak_Classify(
        &diagnostics,
        &quiet_diagnostics);

    if (g_r1_soak_result.soak_pass != 0U)
    {
        g_r1_soak_result.phase = R1_SOAK_PHASE_COMPLETE;
    }
    else
    {
        g_r1_soak_result.phase = R1_SOAK_PHASE_FAILED;
    }

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

void R1_Bringup_CreateTask(void)
{
    TaskHandle_t handle;

    g_r1_soak_result.magic = R1_SOAK_MAGIC;
    g_r1_soak_result.phase = R1_SOAK_PHASE_RESET;

    handle = xTaskCreateStatic(
        R1_Soak_Task,
        "R1Soak",
        R1_SOAK_TASK_STACK_WORDS,
        NULL,
        tskIDLE_PRIORITY + 2U,
        r1_soak_task_stack,
        &r1_soak_task_control);

    if (handle == NULL)
    {
        g_r1_soak_result.task_created = 0U;
        g_r1_soak_result.soak_pass = 0U;
        g_r1_soak_result.phase = R1_SOAK_PHASE_FAILED;
        return;
    }

    g_r1_soak_result.task_created = 1U;
    g_r1_soak_result.phase = R1_SOAK_PHASE_TASK_CREATED;
}
