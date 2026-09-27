#include "r5_hw_harness.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "r5_run_metrics.h"
#include "r5_result_store.h"

#define R5_HW_STACK_WORDS 512U

volatile R5HwHarnessResult g_r5_hw_result;
static StaticTask_t harness_tcb;
static StackType_t harness_stack[R5_HW_STACK_WORDS];
/* Two 128-bin result objects are deliberately static SRAM.  They are part of
 * the R5 static-memory budget, never temporary task-stack payloads. */
static R5RunMetrics synthetic_metrics;
static R5RunMetrics empty_s2_metrics;
static R5ResultStore result_store;

static R5MetricsConfig HarnessConfig(void)
{
    R5MetricsConfig config;

    config.s0 = 2U;
    config.s1 = 5U;
    config.tail_blocks = 2U;
    config.epoch_cycles = 0U;
    config.block_period_cycles = 100U;
    config.deadline_cycles = 160U;
    config.time_epsilon_cycles = 10U;
    return config;
}

static R5MetricsStatus Input(R5RunMetrics *metrics, uint32_t seq,
    uint64_t time, uint64_t serial, uint32_t free_available)
{
    return R5RunMetrics_OnInput(metrics, seq, time, serial, free_available);
}

static void HarnessTask(void *argument)
{
    R5MetricsConfig config = HarnessConfig();
    R5MetricsStatus status;
    const R5RunMetrics *published = NULL;

    (void)argument;
    g_r5_hw_result.init_status = (uint32_t)R5RunMetrics_Initialize(&synthetic_metrics,
        &config);
    if (g_r5_hw_result.init_status == (uint32_t)R5_METRICS_OK)
    {
        status = Input(&synthetic_metrics, 0U, 10U, 1U, 1U);
        if (status == R5_METRICS_OK) status = Input(&synthetic_metrics, 1U, 110U, 2U, 1U);
        if (status == R5_METRICS_OK) status = Input(&synthetic_metrics, 2U, 210U, 3U, 1U);
        if (status == R5_METRICS_OK) status = R5RunMetrics_OnCompletion(&synthetic_metrics, 2U, 350U, 4U);
        if (status == R5_METRICS_OK) status = Input(&synthetic_metrics, 3U, 310U, 5U, 0U);
        if (status == R5_METRICS_OK) status = Input(&synthetic_metrics, 4U, 410U, 6U, 1U);
        if (status == R5_METRICS_OK) status = R5RunMetrics_OnCompletion(&synthetic_metrics, 4U, 700U, 7U);
        if (status == R5_METRICS_OK) status = Input(&synthetic_metrics, 5U, 510U, 8U, 0U);
        if (status == R5_METRICS_OK) status = Input(&synthetic_metrics, 6U, 610U, 9U, 1U);
        g_r5_hw_result.known_status = (uint32_t)status;
        if (status == R5_METRICS_OK)
        {
            status = Input(&synthetic_metrics, 7U, 1000U, 10U, 1U);
        }
        g_r5_hw_result.cutoff_status = (uint32_t)status;
        if (status == R5_METRICS_OK)
        {
            status = R5RunMetrics_Seal(&synthetic_metrics, 1U, 1U);
        }
        g_r5_hw_result.seal_status = (uint32_t)status;
    }
    g_r5_hw_result.phase = (uint32_t)synthetic_metrics.phase;
    g_r5_hw_result.outcome_status = (uint32_t)synthetic_metrics.outcome_status;
    g_r5_hw_result.raw_input_count = synthetic_metrics.raw_input_count;
    g_r5_hw_result.cohort_input_count = synthetic_metrics.cohort_input_count;
    g_r5_hw_result.admitted_count = synthetic_metrics.admitted_count;
    g_r5_hw_result.drop_count = synthetic_metrics.drop_count;
    g_r5_hw_result.on_time_count = synthetic_metrics.on_time_count;
    g_r5_hw_result.late_completed_count = synthetic_metrics.late_completed_count;
    g_r5_hw_result.expired_unresolved_count = synthetic_metrics.expired_unresolved_count;
    g_r5_hw_result.completed_count = synthetic_metrics.completed_count;
    g_r5_hw_result.post_cutoff_completion_count = synthetic_metrics.post_cutoff_completion_count;
    g_r5_hw_result.window_open_time = synthetic_metrics.window_open_time;
    g_r5_hw_result.window_close_time = synthetic_metrics.window_close_time;
    g_r5_hw_result.p99_completed_status = (uint32_t)synthetic_metrics.p99_completed_by_cutoff.status;
    g_r5_hw_result.p99_all_admitted_status = (uint32_t)synthetic_metrics.p99_all_admitted.status;

    /* T18's other required S2 arm: FREE is empty.  The first two inputs are
     * normal cohort drops; S2 itself is only a cutoff and cannot add a third. */
    config.s0 = 0U;
    config.s1 = 2U;
    status = R5RunMetrics_Initialize(&empty_s2_metrics, &config);
    if (status == R5_METRICS_OK) status = Input(&empty_s2_metrics, 0U, 1U, 1U, 0U);
    if (status == R5_METRICS_OK) status = Input(&empty_s2_metrics, 1U, 2U, 2U, 0U);
    if (status == R5_METRICS_OK) status = Input(&empty_s2_metrics, 2U, 3U, 3U, 1U);
    if (status == R5_METRICS_OK) status = Input(&empty_s2_metrics, 3U, 4U, 4U, 1U);
    if (status == R5_METRICS_OK) status = Input(&empty_s2_metrics, 4U, 1000U, 5U, 0U);
    if (status == R5_METRICS_OK) status = R5RunMetrics_Seal(&empty_s2_metrics, 1U, 1U);
    g_r5_hw_result.s2_empty_status = (uint32_t)status;
    g_r5_hw_result.s2_empty_cohort_input_count = empty_s2_metrics.cohort_input_count;
    g_r5_hw_result.s2_empty_drop_count = empty_s2_metrics.drop_count;
    g_r5_hw_result.s2_empty_admitted_count = empty_s2_metrics.admitted_count;
    g_r5_hw_result.s2_empty_pass = (empty_s2_metrics.phase == R5_METRICS_SEALED) &&
        (empty_s2_metrics.cohort_input_count == 2U) &&
        (empty_s2_metrics.drop_count == 2U) &&
        (empty_s2_metrics.admitted_count == 0U) ? 1U : 0U;
    R5ResultStore_Initialize(&result_store);
    g_r5_hw_result.result_store_seal_status = (uint32_t)
        R5ResultStore_Seal(&result_store, 1U, &synthetic_metrics);
    if (g_r5_hw_result.result_store_seal_status == (uint32_t)R5_RESULT_STORE_OK)
    {
        g_r5_hw_result.result_store_acquire_status = (uint32_t)
            R5ResultStore_Acquire(&result_store, 1U, &published);
        if ((g_r5_hw_result.result_store_acquire_status ==
             (uint32_t)R5_RESULT_STORE_OK) &&
            (published != NULL) && (published->phase == R5_METRICS_SEALED))
        {
            g_r5_hw_result.result_store_release_status = (uint32_t)
                R5ResultStore_Release(&result_store, 1U);
        }
    }
    g_r5_hw_result.pass = (synthetic_metrics.phase == R5_METRICS_SEALED) &&
        (synthetic_metrics.raw_input_count == 8U) && (synthetic_metrics.cohort_input_count == 3U) &&
        (synthetic_metrics.admitted_count == 2U) && (synthetic_metrics.drop_count == 1U) &&
        (synthetic_metrics.on_time_count == 1U) && (synthetic_metrics.late_completed_count == 1U) &&
        (synthetic_metrics.expired_unresolved_count == 0U) && (synthetic_metrics.window_open_time == 210U) &&
        (synthetic_metrics.window_close_time == 510U) &&
        (synthetic_metrics.p99_completed_by_cutoff.status == R5_METRICS_P99_INTERVAL) &&
        (synthetic_metrics.p99_all_admitted.status == R5_METRICS_P99_INTERVAL) &&
        (g_r5_hw_result.s2_empty_pass != 0U) &&
        (g_r5_hw_result.result_store_seal_status == (uint32_t)R5_RESULT_STORE_OK) &&
        (g_r5_hw_result.result_store_acquire_status == (uint32_t)R5_RESULT_STORE_OK) &&
        (g_r5_hw_result.result_store_release_status == (uint32_t)R5_RESULT_STORE_OK) ? 1U : 0U;
    __DMB();
    g_r5_hw_result.complete_magic = R5_HW_COMPLETE;
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000U));
}

void R5_HW_Start(void)
{
    TaskHandle_t task;

    (void)memset((void *)&g_r5_hw_result, 0, sizeof(g_r5_hw_result));
    g_r5_hw_result.magic = R5_HW_MAGIC;
    task = xTaskCreateStatic(HarnessTask, "R5HW", R5_HW_STACK_WORDS, NULL,
        tskIDLE_PRIORITY + 3U, harness_stack, &harness_tcb);
    if (task == NULL)
    {
        g_r5_hw_result.init_status = (uint32_t)R5_METRICS_INVALID_STATE;
        g_r5_hw_result.complete_magic = R5_HW_COMPLETE;
        return;
    }
    vTaskStartScheduler();
    g_r5_hw_result.init_status = (uint32_t)R5_METRICS_INVALID_STATE;
    g_r5_hw_result.complete_magic = R5_HW_COMPLETE;
}
