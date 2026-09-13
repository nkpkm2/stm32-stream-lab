#include "r1_bringup.h"

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

#include <stddef.h>
#include <stdint.h>

extern TIM_HandleTypeDef htim2;

#define R1_BRINGUP_MAGIC 0x52314231U
#define R1_BRINGUP_TASK_STACK_WORDS 384U
#define R1_BRINGUP_SETTLE_MS 100U
#define R1_BRINGUP_RUN_MS 250U

volatile R1_BringupResult g_r1_bringup_result;

static StaticTask_t r1_bringup_task_control;
static StackType_t r1_bringup_task_stack[R1_BRINGUP_TASK_STACK_WORDS];

static void R1_Bringup_UpdateRawRange(
    const uint16_t *buffer,
    uint32_t *minimum,
    uint32_t *maximum)
{
    uint32_t i;
    uint32_t min_value = 0xFFFFU;
    uint32_t max_value = 0U;

    if ((buffer == NULL) || (minimum == NULL) || (maximum == NULL))
    {
        return;
    }

    for (i = 0U; i < R1_ACQUISITION_BLOCK_SAMPLES; ++i)
    {
        uint32_t value = (uint32_t)buffer[i];

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

static void R1_Bringup_Task(void *argument)
{
    R1_AcquisitionDiagnostics diagnostics;
    const uint16_t *buffer0;
    const uint16_t *buffer1;
    R1_AcquisitionStatus start_status;
    R1_AcquisitionStatus stop_status;
    uint32_t short_run_pass;
    uint32_t raw0_min;
    uint32_t raw0_max;
    uint32_t raw1_min;
    uint32_t raw1_max;

    (void)argument;

    vTaskDelay(pdMS_TO_TICKS(R1_BRINGUP_SETTLE_MS));

    R1_Acquisition_Init();
    g_r1_bringup_result.phase = R1_BRINGUP_PHASE_INITIALIZED;

    start_status = R1_Acquisition_Start();
    g_r1_bringup_result.start_status = (uint32_t)start_status;

    if (start_status != R1_ACQUISITION_OK)
    {
        g_r1_bringup_result.phase = R1_BRINGUP_PHASE_START_FAILED;

        for (;;)
        {
            vTaskDelay(pdMS_TO_TICKS(1000U));
        }
    }

    g_r1_bringup_result.tim2_cr1_after_start = htim2.Instance->CR1;
    g_r1_bringup_result.phase = R1_BRINGUP_PHASE_RUNNING;

    vTaskDelay(pdMS_TO_TICKS(R1_BRINGUP_RUN_MS));

    stop_status = R1_Acquisition_Stop();
    g_r1_bringup_result.stop_status = (uint32_t)stop_status;

    R1_Acquisition_GetDiagnostics(&diagnostics);
    g_r1_bringup_result.diagnostics = diagnostics;

    buffer0 = R1_Acquisition_GetBuffer(0U);
    buffer1 = R1_Acquisition_GetBuffer(1U);

    R1_Bringup_UpdateRawRange(
        buffer0,
        &raw0_min,
        &raw0_max);

    R1_Bringup_UpdateRawRange(
        buffer1,
        &raw1_min,
        &raw1_max);

    g_r1_bringup_result.raw0_min = raw0_min;
    g_r1_bringup_result.raw0_max = raw0_max;
    g_r1_bringup_result.raw1_min = raw1_min;
    g_r1_bringup_result.raw1_max = raw1_max;

    g_r1_bringup_result.dbm_bit_seen =
        ((diagnostics.start_dma_cr & DMA_SxCR_DBM) != 0U) ? 1U : 0U;

    g_r1_bringup_result.start_ndtr_is_256 =
        (diagnostics.start_dma_ndtr == R1_ACQUISITION_BLOCK_SAMPLES) ? 1U : 0U;

    g_r1_bringup_result.start_ct_is_m0 =
        (diagnostics.start_dma_ct == 0U) ? 1U : 0U;

    g_r1_bringup_result.m0ar_matches_buffer0 =
        (diagnostics.start_dma_m0ar == (uint32_t)(uintptr_t)buffer0) ? 1U : 0U;

    g_r1_bringup_result.m1ar_matches_buffer1 =
        (diagnostics.start_dma_m1ar == (uint32_t)(uintptr_t)buffer1) ? 1U : 0U;

    short_run_pass = 1U;

    if (stop_status != R1_ACQUISITION_OK)
    {
        short_run_pass = 0U;
    }

    if ((g_r1_bringup_result.tim2_cr1_after_start & TIM_CR1_CEN) == 0U)
    {
        short_run_pass = 0U;
    }

    if ((g_r1_bringup_result.dbm_bit_seen == 0U) ||
        (g_r1_bringup_result.start_ndtr_is_256 == 0U) ||
        (g_r1_bringup_result.start_ct_is_m0 == 0U) ||
        (g_r1_bringup_result.m0ar_matches_buffer0 == 0U) ||
        (g_r1_bringup_result.m1ar_matches_buffer1 == 0U))
    {
        short_run_pass = 0U;
    }

    if ((diagnostics.tc_count == 0U) ||
        (diagnostics.m0_complete_count == 0U) ||
        (diagnostics.m1_complete_count == 0U) ||
        (diagnostics.timing_interval_count == 0U))
    {
        short_run_pass = 0U;
    }

    if ((diagnostics.m0_complete_count + diagnostics.m1_complete_count) !=
        diagnostics.tc_count)
    {
        short_run_pass = 0U;
    }

    if ((diagnostics.ct_mismatch_count != 0U) ||
        (diagnostics.alternation_mismatch_count != 0U) ||
        (diagnostics.suspected_event_loss_count != 0U) ||
        (diagnostics.adc_ovr_count != 0U) ||
        (diagnostics.dma_te_count != 0U) ||
        (diagnostics.dma_dme_count != 0U) ||
        (diagnostics.dma_fe_count != 0U) ||
        (diagnostics.dma_other_error_count != 0U))
    {
        short_run_pass = 0U;
    }

    g_r1_bringup_result.short_run_pass = short_run_pass;

    if (stop_status == R1_ACQUISITION_OK)
    {
        g_r1_bringup_result.phase = R1_BRINGUP_PHASE_COMPLETE;
    }
    else
    {
        g_r1_bringup_result.phase = R1_BRINGUP_PHASE_STOP_FAILED;
    }

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

void R1_Bringup_CreateTask(void)
{
    TaskHandle_t handle;

    g_r1_bringup_result.magic = R1_BRINGUP_MAGIC;
    g_r1_bringup_result.phase = R1_BRINGUP_PHASE_RESET;

    handle = xTaskCreateStatic(
        R1_Bringup_Task,
        "R1Bringup",
        R1_BRINGUP_TASK_STACK_WORDS,
        NULL,
        tskIDLE_PRIORITY + 1U,
        r1_bringup_task_stack,
        &r1_bringup_task_control);

    if (handle == NULL)
    {
        g_r1_bringup_result.task_created = 0U;
        g_r1_bringup_result.phase = R1_BRINGUP_PHASE_TASK_CREATE_FAILED;
        return;
    }

    g_r1_bringup_result.task_created = 1U;
    g_r1_bringup_result.phase = R1_BRINGUP_PHASE_TASK_CREATED;
}
