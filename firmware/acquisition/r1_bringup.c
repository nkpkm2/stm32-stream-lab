#include "r1_bringup.h"

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

#include <stddef.h>
#include <stdint.h>

extern TIM_HandleTypeDef htim2;

#define R1_LIFECYCLE_MAGIC 0x52314C31U
#define R1_LIFECYCLE_TASK_STACK_WORDS 512U
#define R1_LIFECYCLE_SETTLE_MS 100U
#define R1_LIFECYCLE_QUIET_MS 2U
#define R1_LIFECYCLE_MEAN_TOLERANCE_CYCLES 2304U

volatile R1_LifecycleResult g_r1_lifecycle_result;

static StaticTask_t r1_lifecycle_task_control;
static StackType_t r1_lifecycle_task_stack[R1_LIFECYCLE_TASK_STACK_WORDS];

static const uint32_t r1_target_run_cycles[R1_LIFECYCLE_CYCLE_COUNT] =
{
    806400U,
    864000U,
    979200U,
    1036800U,
    1094400U,
    1209600U
};

static void R1_Lifecycle_DelayCycles(uint32_t cycles)
{
    uint32_t start = DWT->CYCCNT;

    while ((uint32_t)(DWT->CYCCNT - start) < cycles)
    {
        __NOP();
    }
}

static uint32_t R1_Lifecycle_AbsDiff(uint32_t a, uint32_t b)
{
    return (a >= b) ? (a - b) : (b - a);
}

static void R1_Lifecycle_CopyCycleResult(
    uint32_t cycle_index,
    uint32_t target_run_cycles,
    R1_AcquisitionStatus start_status,
    R1_AcquisitionStatus stop_status,
    uint32_t tim2_cr1_after_start,
    const R1_AcquisitionDiagnostics *diagnostics,
    const R1_AcquisitionDiagnostics *quiet_diagnostics,
    const R1_AcquisitionTraceEntry *trace,
    size_t trace_count)
{
    volatile R1_LifecycleCycleResult *result;
    const uint16_t *buffer0;
    const uint16_t *buffer1;
    uint32_t pass = 1U;
    uint32_t mean_delta = 0U;
    uint32_t expected_restart;

    result = &g_r1_lifecycle_result.cycles[cycle_index];
    buffer0 = R1_Acquisition_GetBuffer(0U);
    buffer1 = R1_Acquisition_GetBuffer(1U);

    result->target_run_cycles = target_run_cycles;
    result->start_status = (uint32_t)start_status;
    result->stop_status = (uint32_t)stop_status;
    result->tim2_cr1_after_start = tim2_cr1_after_start;

    result->restart_count = diagnostics->restart_count;

    result->dbm_bit_seen =
        ((diagnostics->start_dma_cr & DMA_SxCR_DBM) != 0U) ? 1U : 0U;

    result->start_ndtr = diagnostics->start_dma_ndtr;
    result->start_ct = diagnostics->start_dma_ct;

    result->m0ar_matches_buffer0 =
        (diagnostics->start_dma_m0ar ==
         (uint32_t)(uintptr_t)buffer0) ? 1U : 0U;

    result->m1ar_matches_buffer1 =
        (diagnostics->start_dma_m1ar ==
         (uint32_t)(uintptr_t)buffer1) ? 1U : 0U;

    result->adc_dma_bit_seen =
        ((diagnostics->start_adc_cr2 & ADC_CR2_DMA) != 0U) ? 1U : 0U;

    result->adc_dds_bit_seen =
        ((diagnostics->start_adc_cr2 & ADC_CR2_DDS) != 0U) ? 1U : 0U;

    result->tc_count = diagnostics->tc_count;
    result->m0_complete_count = diagnostics->m0_complete_count;
    result->m1_complete_count = diagnostics->m1_complete_count;

    result->ct_mismatch_count = diagnostics->ct_mismatch_count;
    result->alternation_mismatch_count =
        diagnostics->alternation_mismatch_count;
    result->suspected_event_loss_count =
        diagnostics->suspected_event_loss_count;

    result->adc_ovr_count = diagnostics->adc_ovr_count;
    result->dma_te_count = diagnostics->dma_te_count;
    result->dma_dme_count = diagnostics->dma_dme_count;
    result->dma_fe_count = diagnostics->dma_fe_count;
    result->dma_other_error_count = diagnostics->dma_other_error_count;

    result->timing_interval_count = diagnostics->timing_interval_count;
    result->min_delta_cycles = diagnostics->min_delta_cycles;
    result->max_delta_cycles = diagnostics->max_delta_cycles;

    if (diagnostics->timing_interval_count != 0U)
    {
        mean_delta =
            (uint32_t)(
                diagnostics->sum_delta_cycles /
                diagnostics->timing_interval_count);
    }

    result->mean_delta_cycles = mean_delta;

    result->trace_count = (uint32_t)trace_count;

    if (trace_count >= 1U)
    {
        result->first_sequence = trace[0].sequence;
        result->first_completed_target = trace[0].completed_target;
    }

    if (trace_count >= 2U)
    {
        result->second_sequence = trace[1].sequence;
        result->second_completed_target = trace[1].completed_target;
    }

    result->partial_stop_count = diagnostics->partial_stop_count;
    result->stop_remaining_ndtr = diagnostics->stop_remaining_ndtr;
    result->stop_captured_samples = diagnostics->stop_captured_samples;
    result->stop_active_target = diagnostics->stop_active_target;
    result->stop_artifact_count = diagnostics->stop_artifact_count;

    result->quiet_tc_count = quiet_diagnostics->tc_count;
    result->quiet_stop_artifact_count =
        quiet_diagnostics->stop_artifact_count;
    result->quiet_state = (uint32_t)quiet_diagnostics->state;

    expected_restart = (cycle_index == 0U) ? 0U : 1U;

    if (start_status != R1_ACQUISITION_OK)
    {
        pass = 0U;
    }

    if (stop_status != R1_ACQUISITION_OK)
    {
        pass = 0U;
    }

    if ((tim2_cr1_after_start & TIM_CR1_CEN) == 0U)
    {
        pass = 0U;
    }

    if (diagnostics->restart_count != expected_restart)
    {
        pass = 0U;
    }

    if ((result->dbm_bit_seen == 0U) ||
        (result->start_ndtr != R1_ACQUISITION_BLOCK_SAMPLES) ||
        (result->start_ct != 0U) ||
        (result->m0ar_matches_buffer0 == 0U) ||
        (result->m1ar_matches_buffer1 == 0U) ||
        (result->adc_dma_bit_seen == 0U) ||
        (result->adc_dds_bit_seen == 0U))
    {
        pass = 0U;
    }

    if ((diagnostics->tc_count < 2U) ||
        (diagnostics->m0_complete_count == 0U) ||
        (diagnostics->m1_complete_count == 0U))
    {
        pass = 0U;
    }

    if ((diagnostics->m0_complete_count +
         diagnostics->m1_complete_count) !=
        diagnostics->tc_count)
    {
        pass = 0U;
    }

    if (R1_Lifecycle_AbsDiff(
            diagnostics->m0_complete_count,
            diagnostics->m1_complete_count) > 1U)
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
        (R1_Lifecycle_AbsDiff(
             mean_delta,
             R1_ACQUISITION_EXPECTED_BLOCK_CYCLES) >
         R1_LIFECYCLE_MEAN_TOLERANCE_CYCLES))
    {
        pass = 0U;
    }

    if ((trace_count < 2U) ||
        (trace[0].sequence != 1U) ||
        (trace[0].completed_target != 0U) ||
        (trace[1].sequence != 2U) ||
        (trace[1].completed_target != 1U))
    {
        pass = 0U;
    }

    if ((diagnostics->partial_stop_count != 1U) ||
        (diagnostics->stop_remaining_ndtr == 0U) ||
        (diagnostics->stop_remaining_ndtr >=
         R1_ACQUISITION_BLOCK_SAMPLES) ||
        ((diagnostics->stop_remaining_ndtr +
          diagnostics->stop_captured_samples) !=
         R1_ACQUISITION_BLOCK_SAMPLES) ||
        (diagnostics->stop_artifact_count != 0U) ||
        (diagnostics->state != R1_ACQUISITION_STATE_STOPPED))
    {
        pass = 0U;
    }

    if ((quiet_diagnostics->tc_count != diagnostics->tc_count) ||
        (quiet_diagnostics->stop_artifact_count !=
         diagnostics->stop_artifact_count) ||
        (quiet_diagnostics->adc_ovr_count != diagnostics->adc_ovr_count) ||
        (quiet_diagnostics->dma_te_count != diagnostics->dma_te_count) ||
        (quiet_diagnostics->dma_dme_count != diagnostics->dma_dme_count) ||
        (quiet_diagnostics->dma_fe_count != diagnostics->dma_fe_count) ||
        (quiet_diagnostics->state != R1_ACQUISITION_STATE_STOPPED))
    {
        pass = 0U;
    }

    result->cycle_pass = pass;
}

static void R1_Lifecycle_Task(void *argument)
{
    R1_AcquisitionDiagnostics diagnostics;
    R1_AcquisitionDiagnostics quiet_diagnostics;
    R1_AcquisitionTraceEntry trace[4];
    R1_AcquisitionStatus start_status;
    R1_AcquisitionStatus stop_status;
    uint32_t cycle_index;
    uint32_t tim2_cr1_after_start;
    size_t trace_count;

    (void)argument;

    vTaskDelay(pdMS_TO_TICKS(R1_LIFECYCLE_SETTLE_MS));

    R1_Acquisition_Init();

    g_r1_lifecycle_result.phase = R1_LIFECYCLE_PHASE_INITIALIZED;
    g_r1_lifecycle_result.all_pass = 1U;

    for (cycle_index = 0U;
         cycle_index < R1_LIFECYCLE_CYCLE_COUNT;
         ++cycle_index)
    {
        g_r1_lifecycle_result.phase = R1_LIFECYCLE_PHASE_RUNNING;

        start_status = R1_Acquisition_Start();

        if (start_status != R1_ACQUISITION_OK)
        {
            g_r1_lifecycle_result.cycles[cycle_index].start_status =
                (uint32_t)start_status;
            g_r1_lifecycle_result.cycles[cycle_index].cycle_pass = 0U;
            g_r1_lifecycle_result.all_pass = 0U;
            g_r1_lifecycle_result.phase = R1_LIFECYCLE_PHASE_FAILED;
            break;
        }

        tim2_cr1_after_start = htim2.Instance->CR1;

        R1_Lifecycle_DelayCycles(
            r1_target_run_cycles[cycle_index]);

        stop_status = R1_Acquisition_Stop();

        R1_Acquisition_GetDiagnostics(&diagnostics);

        trace_count =
            R1_Acquisition_CopyTrace(
                trace,
                sizeof(trace) / sizeof(trace[0]));

        vTaskDelay(pdMS_TO_TICKS(R1_LIFECYCLE_QUIET_MS));

        R1_Acquisition_GetDiagnostics(&quiet_diagnostics);

        R1_Lifecycle_CopyCycleResult(
            cycle_index,
            r1_target_run_cycles[cycle_index],
            start_status,
            stop_status,
            tim2_cr1_after_start,
            &diagnostics,
            &quiet_diagnostics,
            trace,
            trace_count);

        ++g_r1_lifecycle_result.completed_cycles;

        if (g_r1_lifecycle_result.cycles[cycle_index].cycle_pass == 0U)
        {
            g_r1_lifecycle_result.all_pass = 0U;
            g_r1_lifecycle_result.phase = R1_LIFECYCLE_PHASE_FAILED;
            break;
        }
    }

    if ((g_r1_lifecycle_result.completed_cycles ==
         R1_LIFECYCLE_CYCLE_COUNT) &&
        (g_r1_lifecycle_result.all_pass != 0U))
    {
        g_r1_lifecycle_result.phase = R1_LIFECYCLE_PHASE_COMPLETE;
    }
    else
    {
        g_r1_lifecycle_result.all_pass = 0U;
        g_r1_lifecycle_result.phase = R1_LIFECYCLE_PHASE_FAILED;
    }

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

void R1_Bringup_CreateTask(void)
{
    TaskHandle_t handle;

    g_r1_lifecycle_result.magic = R1_LIFECYCLE_MAGIC;
    g_r1_lifecycle_result.phase = R1_LIFECYCLE_PHASE_RESET;

    handle = xTaskCreateStatic(
        R1_Lifecycle_Task,
        "R1Lifecycle",
        R1_LIFECYCLE_TASK_STACK_WORDS,
        NULL,
        tskIDLE_PRIORITY + 2U,
        r1_lifecycle_task_stack,
        &r1_lifecycle_task_control);

    if (handle == NULL)
    {
        g_r1_lifecycle_result.task_created = 0U;
        g_r1_lifecycle_result.all_pass = 0U;
        g_r1_lifecycle_result.phase = R1_LIFECYCLE_PHASE_FAILED;
        return;
    }

    g_r1_lifecycle_result.task_created = 1U;
    g_r1_lifecycle_result.phase = R1_LIFECYCLE_PHASE_TASK_CREATED;
}
