#include "r4_hw_harness.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"

#include "r4_runtime_target.h"
#include "r4_tick_service_target.h"
#include "r3_w3_runtime.h"

#define R4_HW_STACK_WORDS 1280U
#define R4_HW_BOOT UINT32_C(0x52340001)
#define R4_HW_COMPLETE_BUDGET_CYCLES 1800U
#define R4_HW_TICK_CYCLES UINT64_C(180000)
#define R4_HW_TICK_INTERVAL_LIMIT_CYCLES UINT64_C(270000)
#define R4_HW_TICK_GAP_MASK_CYCLES UINT32_C(540000)
#define R4_HW_TIM2_START_PHASE_BOUND_CYCLES UINT64_C(1800)
#define R4_HW_MICROBENCH_SAMPLES 33U
#define R4_HW_MICROBENCH_IRQ_A ((uintptr_t)UINT32_C(0xE1))
#define R4_HW_MICROBENCH_IRQ_B ((uintptr_t)UINT32_C(0xE2))
#define R4_HW_MONITOR_INTERVAL_LIMIT_CYCLES UINT64_C(1800000000)
#define R4_HW_MONITOR_PERIOD_MS 1000U
#define R4_HW_SYNTHETIC_A_ITERATIONS 50000U
#define R4_HW_SYNTHETIC_B_ITERATIONS 10000U
#define R4_HW_SYNTHETIC_DONE_A UINT32_C(1)
#define R4_HW_SYNTHETIC_DONE_B UINT32_C(2)
#define R4_HW_DMA_WINDOW_OPEN_SEQUENCE 1U
#define R4_HW_DMA_WINDOW_CLOSE_SEQUENCE 4U
#define R4_HW_DMA_WINDOW_MIN_POST_CLOSE_SEQUENCE 5U
#define R4_HW_DMA_WINDOW_WAIT_TICKS 2000U
#define R4_HW_RESPONSE_WORK_ITERATIONS 50000U
#define R4_HW_RESPONSE_MAX_CYCLES UINT32_C(5000000)
#define R4_HW_RESPONSE_OWNER_COVERAGE_PERMILLE UINT64_C(990)
#ifndef R4_HW_SOAK_MS
#define R4_HW_SOAK_MS 0U
#endif
#ifndef R4_HW_CASE_ID
#error "R4 formal target build must define R4_HW_CASE_ID"
#endif

volatile R4HwHarnessResult g_r4_hw_result;
static StaticTask_t harness_tcb;
static StackType_t harness_stack[R4_HW_STACK_WORDS];
static StaticTask_t idle_tcb;
static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
static volatile uint32_t tick_start_callback_count;
static volatile uint32_t tick_release_callback_count;
#if (R4_HW_CASE_ID == 6U)
static StaticTask_t synthetic_a_tcb;
static StaticTask_t synthetic_b_tcb;
static StackType_t synthetic_a_stack[configMINIMAL_STACK_SIZE];
static StackType_t synthetic_b_stack[configMINIMAL_STACK_SIZE];
static volatile uint32_t synthetic_a_sink;
static volatile uint32_t synthetic_b_sink;
static TaskHandle_t synthetic_a_task;
static TaskHandle_t synthetic_b_task;
#endif

#if (R4_HW_CASE_ID == 14U)
static StaticTask_t response_tcb;
static StackType_t response_stack[configMINIMAL_STACK_SIZE];
static volatile uint32_t response_sink;
static TaskHandle_t response_task;

static void ResponseTask(void *argument)
{
    uint32_t index;
    (void)argument;

    /* The task blocks before the harness records its release endpoint. */
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    g_r4_hw_result.response_start_raw = DWT->CYCCNT;
    (void)R4_RuntimeTarget_Checkpoint();
    for (index = 0U; index < R4_HW_RESPONSE_WORK_ITERATIONS; ++index)
    {
        response_sink = (response_sink * UINT32_C(1664525)) + UINT32_C(1013904223);
    }
    g_r4_hw_result.response_complete_raw = DWT->CYCCNT;
    g_r4_hw_result.response_work_raw =
        g_r4_hw_result.response_complete_raw - g_r4_hw_result.response_start_raw;
    (void)R4_RuntimeTarget_Checkpoint();
    g_r4_hw_result.response_worker_done = 1U;

    for (;;)
    {
        vTaskDelay(portMAX_DELAY);
    }
}
#endif

#if (R4_HW_CASE_ID == 4U) || (R4_HW_CASE_ID == 6U) || (R4_HW_CASE_ID == 14U)
static uint64_t OwnerCycles(const R4_RuntimeOwnerBucket *buckets,
    uint32_t capacity, uintptr_t identity)
{
    uint32_t index;
    for (index = 0U; index < capacity; ++index)
    {
        if (buckets[index].identity == identity)
        {
            return buckets[index].cycles;
        }
    }
    return 0U;
}
#endif

#if (R4_HW_CASE_ID == 9U)
static void SummarizeSamples(uint32_t *samples, uint32_t count,
    volatile uint64_t *minimum, volatile uint64_t *median,
    volatile uint64_t *maximum)
{
    uint32_t index;
    uint32_t inner;

    for (index = 1U; index < count; ++index)
    {
        uint32_t value = samples[index];
        inner = index;
        while ((inner > 0U) && (samples[inner - 1U] > value))
        {
            samples[inner] = samples[inner - 1U];
            --inner;
        }
        samples[inner] = value;
    }
    *minimum = samples[0];
    *median = samples[count / 2U];
    *maximum = samples[count - 1U];
}

static void MeasureRuntimeEventPath(uint32_t *samples,
    R4_RuntimeEventKind first, R4_RuntimeEventKind second,
    R4_RuntimeEventKind third, R4_RuntimeEventKind fourth,
    uint32_t events)
{
    uint32_t index;

    for (index = 0U; index < R4_HW_MICROBENCH_SAMPLES; ++index)
    {
        uint32_t start = DWT->CYCCNT;
        (void)R4_RuntimeTarget_TestApplyEvent(first, R4_HW_MICROBENCH_IRQ_A);
        if (events > 1U)
        {
            (void)R4_RuntimeTarget_TestApplyEvent(second, R4_HW_MICROBENCH_IRQ_A);
        }
        if (events > 2U)
        {
            (void)R4_RuntimeTarget_TestApplyEvent(third, R4_HW_MICROBENCH_IRQ_B);
        }
        if (events > 3U)
        {
            (void)R4_RuntimeTarget_TestApplyEvent(fourth, R4_HW_MICROBENCH_IRQ_B);
        }
        samples[index] = (uint32_t)(DWT->CYCCNT - start);
    }
}

static void RunMicrobenchmark(void)
{
    uint32_t samples[R4_HW_MICROBENCH_SAMPLES];
    uint32_t index;

    /* The diagnostic entrance is the same guarded Apply transaction used by
     * production trace endpoints.  IRQ/nested rows time ledger semantics,
     * not Cortex-M exception-entry/return machine cycles. */
    MeasureRuntimeEventPath(samples, R4_RUNTIME_EVENT_CHECKPOINT,
        R4_RUNTIME_EVENT_CHECKPOINT, R4_RUNTIME_EVENT_CHECKPOINT,
        R4_RUNTIME_EVENT_CHECKPOINT, 1U);
    SummarizeSamples(samples, R4_HW_MICROBENCH_SAMPLES,
        &g_r4_hw_result.microbench_task_min_cycles,
        &g_r4_hw_result.microbench_task_median_cycles,
        &g_r4_hw_result.microbench_task_max_cycles);
    MeasureRuntimeEventPath(samples, R4_RUNTIME_EVENT_IRQ_ENTER,
        R4_RUNTIME_EVENT_IRQ_EXIT, R4_RUNTIME_EVENT_CHECKPOINT,
        R4_RUNTIME_EVENT_CHECKPOINT, 2U);
    SummarizeSamples(samples, R4_HW_MICROBENCH_SAMPLES,
        &g_r4_hw_result.microbench_irq_min_cycles,
        &g_r4_hw_result.microbench_irq_median_cycles,
        &g_r4_hw_result.microbench_irq_max_cycles);
    for (index = 0U; index < R4_HW_MICROBENCH_SAMPLES; ++index)
    {
        uint32_t start = DWT->CYCCNT;
        (void)R4_RuntimeTarget_TestApplyEvent(R4_RUNTIME_EVENT_WINDOW_OPEN, 1U);
        (void)R4_RuntimeTarget_TestApplyEvent(R4_RUNTIME_EVENT_WINDOW_CLOSE, 1U);
        samples[index] = (uint32_t)(DWT->CYCCNT - start);
    }
    SummarizeSamples(samples, R4_HW_MICROBENCH_SAMPLES,
        &g_r4_hw_result.microbench_window_min_cycles,
        &g_r4_hw_result.microbench_window_median_cycles,
        &g_r4_hw_result.microbench_window_max_cycles);
    for (index = 0U; index < R4_HW_MICROBENCH_SAMPLES; ++index)
    {
        uint32_t start = DWT->CYCCNT;
        (void)R4_RuntimeTarget_TestApplyEvent(R4_RUNTIME_EVENT_IRQ_ENTER,
            R4_HW_MICROBENCH_IRQ_A);
        (void)R4_RuntimeTarget_TestApplyEvent(R4_RUNTIME_EVENT_IRQ_ENTER,
            R4_HW_MICROBENCH_IRQ_B);
        (void)R4_RuntimeTarget_TestApplyEvent(R4_RUNTIME_EVENT_IRQ_EXIT,
            R4_HW_MICROBENCH_IRQ_B);
        (void)R4_RuntimeTarget_TestApplyEvent(R4_RUNTIME_EVENT_IRQ_EXIT,
            R4_HW_MICROBENCH_IRQ_A);
        samples[index] = (uint32_t)(DWT->CYCCNT - start);
    }
    SummarizeSamples(samples, R4_HW_MICROBENCH_SAMPLES,
        &g_r4_hw_result.microbench_nested_min_cycles,
        &g_r4_hw_result.microbench_nested_median_cycles,
        &g_r4_hw_result.microbench_nested_max_cycles);
    g_r4_hw_result.microbench_sample_count = R4_HW_MICROBENCH_SAMPLES;
}
#endif

static void FailInvariant(R4HwInvariant invariant)
{
    g_r4_hw_result.invariant_failure_mask |= (uint32_t)invariant;
}

static void EvaluateFormalInvariants(void)
{
    const uint32_t ok = (uint32_t)R4_RUNTIME_OK;
    const uint32_t tick_ok = (uint32_t)R4_TICK_SERVICE_OK;

    if (g_r4_hw_result.init_status != ok)
    {
        FailInvariant(R4_HW_INVARIANT_INIT);
    }
    if ((g_r4_hw_result.window_open_status != ok) ||
        (g_r4_hw_result.window_close_status != ok) ||
        (g_r4_hw_result.checkpoint_status != ok) ||
        (g_r4_hw_result.t17_post_close_status != ok) ||
        (g_r4_hw_result.t17_window_cycles_at_close !=
         g_r4_hw_result.t17_window_cycles_after_close))
    {
        FailInvariant(R4_HW_INVARIANT_WINDOW);
    }
    if ((g_r4_hw_result.tick_register_status != tick_ok) ||
        (g_r4_hw_result.tick_snapshot_status != tick_ok) ||
        (g_r4_hw_result.tick_timing_configure_status != tick_ok) ||
        (g_r4_hw_result.tick_timing_snapshot_status != tick_ok) ||
        (g_r4_hw_result.tick_timing_service_count == 0U) ||
#if (R4_HW_CASE_ID != 8U)
        (g_r4_hw_result.tick_timing_over_limit_count != 0U) ||
#endif
        (g_r4_hw_result.tick_systick_snapshot_status != ok) ||
        (g_r4_hw_result.tick_systick_enter_count == 0U) ||
        (g_r4_hw_result.tick_systick_enter_count !=
         g_r4_hw_result.tick_systick_exit_count) ||
        (g_r4_hw_result.tick_systick_enter_count !=
         g_r4_hw_result.tick_timing_service_count))
    {
        FailInvariant(R4_HW_INVARIANT_TICK);
    }

#if (R4_HW_CASE_ID == 1U)
    if ((g_r4_hw_result.lifecycle_init_status != (uint32_t)R3_W3_RUNTIME_OK) ||
        (g_r4_hw_result.lifecycle_start_status != (uint32_t)R3_W3_RUNTIME_OK) ||
        (g_r4_hw_result.lifecycle_stop_status != (uint32_t)R3_W3_RUNTIME_OK) ||
        (g_r4_hw_result.soak_configured_ms < 60000U) ||
        (g_r4_hw_result.soak_end_clock_high_word <=
         g_r4_hw_result.soak_start_clock_high_word) ||
        (g_r4_hw_result.dma_irq_count == 0U) ||
        (g_r4_hw_result.dma_yield_requested_count == 0U) ||
        (g_r4_hw_result.dma_no_event_snapshot_status != ok) ||
        ((g_r4_hw_result.dma_no_event_irq_after -
          g_r4_hw_result.dma_no_event_irq_before) != UINT64_C(1)) ||
        ((g_r4_hw_result.dma_no_event_no_yield_after -
          g_r4_hw_result.dma_no_event_no_yield_before) != UINT64_C(1)))
    {
        FailInvariant(R4_HW_INVARIANT_CASE);
    }
    if ((g_r4_hw_result.health_snapshot_status != ok) ||
        (g_r4_hw_result.health_first_fault != R4_RUNTIME_INFRA_NONE) ||
        (g_r4_hw_result.health_fail_closed_requested != 0U) ||
        (g_r4_hw_result.health_monitor_service_count < 60U) ||
        (g_r4_hw_result.health_max_monitor_interval_cycles >
         g_r4_hw_result.health_monitor_interval_limit_cycles))
    {
        FailInvariant(R4_HW_INVARIANT_HEALTH);
    }
#elif (R4_HW_CASE_ID == 2U)
    if ((g_r4_hw_result.tick_arm_status != tick_ok) ||
        (g_r4_hw_result.tick_start_callback_count != 1U) ||
        (g_r4_hw_result.tick_release_callback_count != 1U) ||
        (g_r4_hw_result.tick_start_count != UINT64_C(1)) ||
        (g_r4_hw_result.tick_release_count != UINT64_C(1)) ||
        (g_r4_hw_result.tick_skipped_count != UINT64_C(0)) ||
        (g_r4_hw_result.tick_phase_q0 == 0U) ||
        (g_r4_hw_result.tick_phase_tim2_cen != 1U) ||
        (g_r4_hw_result.tick_phase_tim2_after <
         g_r4_hw_result.tick_phase_tim2_before) ||
        ((g_r4_hw_result.tick_phase_tim2_after -
          g_r4_hw_result.tick_phase_tim2_before) >
         R4_HW_TIM2_START_PHASE_BOUND_CYCLES))
    {
        FailInvariant(R4_HW_INVARIANT_CASE);
    }
#elif (R4_HW_CASE_ID == 3U)
    if ((g_r4_hw_result.tick_arm_status != tick_ok) ||
        (g_r4_hw_result.tick_start_callback_count != 1U) ||
        (g_r4_hw_result.tick_release_callback_count != 1U) ||
        (g_r4_hw_result.tick_start_count != UINT64_C(1)) ||
        (g_r4_hw_result.tick_release_count != UINT64_C(1)) ||
        (g_r4_hw_result.tick_skipped_count != UINT64_C(1)) ||
        (g_r4_hw_result.tick_phase_q0 == 0U) ||
        (g_r4_hw_result.tick_phase_tim2_cen != 1U) ||
        (g_r4_hw_result.tick_phase_tim2_after <
         g_r4_hw_result.tick_phase_tim2_before) ||
        ((g_r4_hw_result.tick_phase_tim2_after -
          g_r4_hw_result.tick_phase_tim2_before) >
         R4_HW_TIM2_START_PHASE_BOUND_CYCLES))
    {
        FailInvariant(R4_HW_INVARIANT_CASE);
    }
#elif (R4_HW_CASE_ID == 4U)
    if ((g_r4_hw_result.t17_arm_status != ok) ||
        (g_r4_hw_result.t17_checkpoint_status != ok) ||
        (g_r4_hw_result.t17_serial_after_pending_irq <=
         g_r4_hw_result.t17_serial_before) ||
        (g_r4_hw_result.irq_depth != 0U) ||
        (g_r4_hw_result.t17_duplicate_exit_status !=
         (uint32_t)R4_RUNTIME_IRQ_EXIT_MISMATCH) ||
        (g_r4_hw_result.t17_nested_arm_status != ok) ||
        (g_r4_hw_result.t17_low_irq_cycles_after <=
         g_r4_hw_result.t17_low_irq_cycles_before) ||
        (g_r4_hw_result.t17_high_irq_cycles_after <=
         g_r4_hw_result.t17_high_irq_cycles_before))
    {
        FailInvariant(R4_HW_INVARIANT_CASE);
    }
#elif (R4_HW_CASE_ID == 5U)
    if ((g_r4_hw_result.lifecycle_init_status != (uint32_t)R3_W3_RUNTIME_OK) ||
        (g_r4_hw_result.lifecycle_start_status != (uint32_t)R3_W3_RUNTIME_OK) ||
        (g_r4_hw_result.lifecycle_stop_status != (uint32_t)R3_W3_RUNTIME_OK) ||
        (g_r4_hw_result.completion_budget_pass == 0U) ||
        (g_r4_hw_result.completion_count < 100U) ||
        (g_r4_hw_result.completion_lock_count !=
         g_r4_hw_result.completion_commit_count) ||
        (g_r4_hw_result.completion_commit_count !=
         g_r4_hw_result.completion_unlock_count) ||
        (g_r4_hw_result.completion_max_total_cycles >
         R4_HW_COMPLETE_BUDGET_CYCLES))
    {
        FailInvariant(R4_HW_INVARIANT_COMPLETION);
    }
    if ((g_r4_hw_result.health_snapshot_status != ok) ||
        (g_r4_hw_result.health_first_fault != R4_RUNTIME_INFRA_NONE) ||
        (g_r4_hw_result.health_fail_closed_requested != 0U))
    {
        FailInvariant(R4_HW_INVARIANT_HEALTH);
    }
#elif (R4_HW_CASE_ID == 6U)
    if ((g_r4_hw_result.synthetic_schema_version !=
         R4_HW_SYNTHETIC_SCHEMA_VERSION) ||
        (g_r4_hw_result.synthetic_task_create_mask !=
         (R4_HW_SYNTHETIC_DONE_A | R4_HW_SYNTHETIC_DONE_B)) ||
        (g_r4_hw_result.synthetic_task_done_mask !=
         (R4_HW_SYNTHETIC_DONE_A | R4_HW_SYNTHETIC_DONE_B)) ||
        (g_r4_hw_result.synthetic_task_a_iterations !=
         R4_HW_SYNTHETIC_A_ITERATIONS) ||
        (g_r4_hw_result.synthetic_task_b_iterations !=
         R4_HW_SYNTHETIC_B_ITERATIONS) ||
        (g_r4_hw_result.synthetic_task_a_cycles == 0U) ||
        (g_r4_hw_result.synthetic_task_b_cycles == 0U) ||
        (g_r4_hw_result.synthetic_task_a_cycles <=
         (g_r4_hw_result.synthetic_task_b_cycles * UINT64_C(2))) ||
        (g_r4_hw_result.synthetic_window_idle_cycles == 0U) ||
        (g_r4_hw_result.window_cycles !=
         (g_r4_hw_result.synthetic_window_task_cycles +
          g_r4_hw_result.synthetic_window_irq_cycles +
          g_r4_hw_result.synthetic_window_idle_cycles +
          g_r4_hw_result.synthetic_window_unclassified_cycles)))
    {
        FailInvariant(R4_HW_INVARIANT_CASE);
    }
#elif (R4_HW_CASE_ID == 7U)
    if ((g_r4_hw_result.lifecycle_init_status != (uint32_t)R3_W3_RUNTIME_OK) ||
        (g_r4_hw_result.lifecycle_start_status != (uint32_t)R3_W3_RUNTIME_OK) ||
        (g_r4_hw_result.lifecycle_stop_status != (uint32_t)R3_W3_RUNTIME_OK) ||
        (g_r4_hw_result.dma_window_snapshot_status != ok) ||
        (g_r4_hw_result.dma_window_configured != 1U) ||
        (g_r4_hw_result.dma_window_opened != 1U) ||
        (g_r4_hw_result.dma_window_closed != 1U) ||
        (g_r4_hw_result.dma_window_open_sequence !=
         R4_HW_DMA_WINDOW_OPEN_SEQUENCE) ||
        (g_r4_hw_result.dma_window_close_sequence !=
         R4_HW_DMA_WINDOW_CLOSE_SEQUENCE) ||
        (g_r4_hw_result.dma_window_last_sequence <
         R4_HW_DMA_WINDOW_MIN_POST_CLOSE_SEQUENCE) ||
        (g_r4_hw_result.dma_window_first_error_sequence != 0U) ||
        (g_r4_hw_result.dma_window_open_status != ok) ||
        (g_r4_hw_result.dma_window_close_status != ok) ||
        (g_r4_hw_result.dma_window_boundary_status != ok))
    {
        FailInvariant(R4_HW_INVARIANT_CASE);
    }
#elif (R4_HW_CASE_ID == 8U)
    if ((g_r4_hw_result.tick_gap_health_status != ok) ||
        (g_r4_hw_result.tick_gap_first_fault !=
         R4_RUNTIME_INFRA_TICK_SERVICE_GAP) ||
        (g_r4_hw_result.tick_gap_fail_closed_requested != 1U) ||
        (g_r4_hw_result.tick_gap_over_limit_count == 0U) ||
        (g_r4_hw_result.tick_gap_max_interval_cycles <=
         g_r4_hw_result.tick_gap_interval_limit_cycles))
    {
        FailInvariant(R4_HW_INVARIANT_CASE);
    }
#elif (R4_HW_CASE_ID == 9U)
    if ((g_r4_hw_result.microbench_sample_count != R4_HW_MICROBENCH_SAMPLES) ||
        (g_r4_hw_result.microbench_task_min_cycles == 0U) ||
        (g_r4_hw_result.microbench_task_min_cycles >
         g_r4_hw_result.microbench_task_median_cycles) ||
        (g_r4_hw_result.microbench_task_median_cycles >
         g_r4_hw_result.microbench_task_max_cycles) ||
        (g_r4_hw_result.microbench_irq_min_cycles == 0U) ||
        (g_r4_hw_result.microbench_irq_min_cycles >
         g_r4_hw_result.microbench_irq_median_cycles) ||
        (g_r4_hw_result.microbench_irq_median_cycles >
         g_r4_hw_result.microbench_irq_max_cycles) ||
        (g_r4_hw_result.microbench_window_min_cycles == 0U) ||
        (g_r4_hw_result.microbench_window_min_cycles >
         g_r4_hw_result.microbench_window_median_cycles) ||
        (g_r4_hw_result.microbench_window_median_cycles >
         g_r4_hw_result.microbench_window_max_cycles) ||
        (g_r4_hw_result.microbench_nested_min_cycles == 0U) ||
        (g_r4_hw_result.microbench_nested_min_cycles >
         g_r4_hw_result.microbench_nested_median_cycles) ||
        (g_r4_hw_result.microbench_nested_median_cycles >
         g_r4_hw_result.microbench_nested_max_cycles))
    {
        FailInvariant(R4_HW_INVARIANT_CASE);
    }
#elif (R4_HW_CASE_ID == 10U)
    if ((g_r4_hw_result.mask_normal_status != ok) ||
        (g_r4_hw_result.mask_normal_primask_before != 0U) ||
        (g_r4_hw_result.mask_normal_primask_after != 0U) ||
        (g_r4_hw_result.mask_masked_status != ok) ||
        (g_r4_hw_result.mask_masked_primask_before != 1U) ||
        (g_r4_hw_result.mask_masked_primask_after != 1U) ||
        (g_r4_hw_result.mask_basepri_before !=
         g_r4_hw_result.mask_basepri_after))
    {
        FailInvariant(R4_HW_INVARIANT_CASE);
    }
#elif (R4_HW_CASE_ID == 11U)
    if ((g_r4_hw_result.time_regression_inject_status !=
         (uint32_t)R4_RUNTIME_TIME_REGRESSION) ||
        (g_r4_hw_result.time_regression_post_status !=
         (uint32_t)R4_RUNTIME_TIME_REGRESSION) ||
        (g_r4_hw_result.time_regression_serial_after !=
         g_r4_hw_result.time_regression_serial_before))
    {
        FailInvariant(R4_HW_INVARIANT_CASE);
    }
#elif (R4_HW_CASE_ID == 12U)
    if ((g_r4_hw_result.lifecycle_init_status != (uint32_t)R3_W3_RUNTIME_OK) ||
        (g_r4_hw_result.lifecycle_start_status != (uint32_t)R3_W3_RUNTIME_OK) ||
        (g_r4_hw_result.lifecycle_stop_status != (uint32_t)R3_W3_RUNTIME_OK) ||
        (g_r4_hw_result.completion_count < 100U) ||
        (g_r4_hw_result.completion_lock_count !=
         g_r4_hw_result.completion_commit_count) ||
        (g_r4_hw_result.completion_commit_count !=
         g_r4_hw_result.completion_unlock_count) ||
        (g_r4_hw_result.commit_pending_arm_status != ok) ||
        (g_r4_hw_result.commit_pending_snapshot_status != ok) ||
        (g_r4_hw_result.commit_pending_arm_count != 1U) ||
        (g_r4_hw_result.commit_pending_irq_count != 1U) ||
        (g_r4_hw_result.commit_pending_active_at_irq != 0U))
    {
        FailInvariant(R4_HW_INVARIANT_COMPLETION);
    }
#elif (R4_HW_CASE_ID == 13U)
    if ((g_r4_hw_result.window_open_status != ok) ||
        (g_r4_hw_result.window_close_status != ok) ||
        (g_r4_hw_result.checkpoint_status != ok) ||
        (g_r4_hw_result.t17_window_cycles_at_close == 0U) ||
        (g_r4_hw_result.t17_window_cycles_after_close !=
         g_r4_hw_result.t17_window_cycles_at_close) ||
        (g_r4_hw_result.window_cycles !=
         (g_r4_hw_result.target_window_task_cycles +
          g_r4_hw_result.target_window_irq_cycles +
          g_r4_hw_result.target_window_idle_cycles +
          g_r4_hw_result.target_window_unclassified_cycles)))
    {
        FailInvariant(R4_HW_INVARIANT_WINDOW);
    }
#elif (R4_HW_CASE_ID == 14U)
    /* The wall endpoints are direct CYCCNT observations.  They deliberately
     * do not call through Clock64 or inspect ledger state.  The ledger's
     * owner bucket must nevertheless contain the known worker interval. */
    if ((g_r4_hw_result.response_worker_created != 1U) ||
        (g_r4_hw_result.response_worker_done != 1U) ||
        (g_r4_hw_result.response_start_raw == 0U) ||
        (g_r4_hw_result.response_complete_raw == 0U) ||
        (g_r4_hw_result.response_work_raw == 0U) ||
        ((g_r4_hw_result.response_start_raw -
          g_r4_hw_result.response_release_raw) > R4_HW_RESPONSE_MAX_CYCLES) ||
        ((g_r4_hw_result.response_complete_raw -
          g_r4_hw_result.response_release_raw) > R4_HW_RESPONSE_MAX_CYCLES) ||
        (g_r4_hw_result.response_work_raw >
         (g_r4_hw_result.response_complete_raw -
          g_r4_hw_result.response_release_raw)) ||
        ((g_r4_hw_result.response_owner_cycles * UINT64_C(1000)) <
         ((uint64_t)g_r4_hw_result.response_work_raw *
          R4_HW_RESPONSE_OWNER_COVERAGE_PERMILLE)))
    {
        FailInvariant(R4_HW_INVARIANT_CASE);
    }
#endif
    g_r4_hw_result.terminal_pass =
        g_r4_hw_result.invariant_failure_mask == 0U ? 1U : 0U;
}

static int HarnessTickStart(uint64_t service_seq, void *context)
{
    (void)context;
#if (R4_HW_CASE_ID == 2U) || (R4_HW_CASE_ID == 3U)
    /* q0 arrives solely through the real tick hook.  Bracket the physical
     * TIM2 CEN write directly; do not infer a start time from a later DMA IRQ. */
    g_r4_hw_result.tick_phase_q0 = service_seq;
    TIM2->CNT = 0U;
    g_r4_hw_result.tick_phase_tim2_before = DWT->CYCCNT;
    TIM2->CR1 |= TIM_CR1_CEN;
    g_r4_hw_result.tick_phase_tim2_after = DWT->CYCCNT;
    g_r4_hw_result.tick_phase_tim2_cen =
        (TIM2->CR1 & TIM_CR1_CEN) != 0U ? 1U : 0U;
#else
    (void)service_seq;
#endif
    ++tick_start_callback_count;
    return 1;
}

static int HarnessTickRelease(uint64_t planned_service_seq,
    uint64_t actual_cycle, void *context)
{
    (void)planned_service_seq;
    (void)actual_cycle;
    (void)context;
    ++tick_release_callback_count;
    return 1;
}

#if (R4_HW_CASE_ID == 6U)
static void SyntheticTaskA(void *argument)
{
    uint32_t index;
    (void)argument;
    for (index = 0U; index < R4_HW_SYNTHETIC_A_ITERATIONS; ++index)
    {
        synthetic_a_sink = (synthetic_a_sink * UINT32_C(1664525)) +
            UINT32_C(1013904223);
    }
    g_r4_hw_result.synthetic_task_a_iterations = index;
    g_r4_hw_result.synthetic_task_done_mask |= R4_HW_SYNTHETIC_DONE_A;
    /* The frozen profile intentionally excludes delete/suspend APIs.  A
     * portMAX_DELAY block keeps the static TCB identity stable while yielding
     * the CPU to task B and then Idle for this finite hardware experiment. */
    for (;;)
    {
        vTaskDelay(portMAX_DELAY);
    }
}

static void SyntheticTaskB(void *argument)
{
    uint32_t index;
    (void)argument;
    for (index = 0U; index < R4_HW_SYNTHETIC_B_ITERATIONS; ++index)
    {
        synthetic_b_sink = (synthetic_b_sink * UINT32_C(22695477)) +
            UINT32_C(1);
    }
    g_r4_hw_result.synthetic_task_b_iterations = index;
    g_r4_hw_result.synthetic_task_done_mask |= R4_HW_SYNTHETIC_DONE_B;
    for (;;)
    {
        vTaskDelay(portMAX_DELAY);
    }
}
#endif
static void HarnessTask(void *argument)
{
    const R4_RuntimeLedger *ledger;
#if (R4_HW_CASE_ID == 5U) || (R4_HW_CASE_ID == 12U)
    R4_CompletionTimingSnapshot timing;
#endif
#if (R4_HW_CASE_ID == 1U) || (R4_HW_CASE_ID == 5U) || (R4_HW_CASE_ID == 7U) || \
    (R4_HW_CASE_ID == 12U)
    R3W3RuntimeConfig config;
    R3LifecycleStartRequest start;
    R3LifecycleStartTicket ticket;
    R3LifecycleStopRequest stop;
    R4_DmaTailSnapshot dma_tail;
    R4_RuntimeHealthSnapshot health;
    uint64_t soak_now;
#if R4_HW_SOAK_MS > 0U
    uint32_t soak_elapsed_ms;
#endif
#endif
#if (R4_HW_CASE_ID == 7U)
    R4_DmaWindowSnapshot dma_window;
    R3W3RuntimeSnapshot runtime_snapshot;
    uint32_t dma_window_wait_tick;
#endif
#if (R4_HW_CASE_ID == 8U)
    R4_RuntimeHealthSnapshot tick_gap_health;
    uint32_t tick_gap_mask_start;
    uint32_t tick_gap_saved_primask;
#endif
    R4_TickService tick_snapshot;
    R4_TickServiceTiming tick_timing;
    R4_SysTickTraceSnapshot systick_trace;
    R4_TickServiceTargetCallbacks tick_callbacks;

    (void)argument;
#if (R4_HW_CASE_ID == 7U)
    (void)memset(&dma_window, 0, sizeof(dma_window));
#endif
#if (R4_HW_CASE_ID == 7U)
    g_r4_hw_result.window_open_status = (uint32_t)
        R4_RuntimeTarget_ArmDmaWindow(0U, R4_HW_DMA_WINDOW_OPEN_SEQUENCE,
            R4_HW_DMA_WINDOW_CLOSE_SEQUENCE);
#elif (R4_HW_CASE_ID == 13U)
    /* A pre-open checkpoint is deliberately outside the formal interval. */
    g_r4_hw_result.checkpoint_status = (uint32_t)R4_RuntimeTarget_Checkpoint();
    g_r4_hw_result.window_open_status = (uint32_t)R4_RuntimeTarget_OpenWindow(0U);
#else
    g_r4_hw_result.window_open_status = (uint32_t)R4_RuntimeTarget_OpenWindow(0U);
#endif
    g_r4_hw_result.tick_snapshot_status = (uint32_t)
        R4_TickServiceTarget_GetSnapshot(&tick_snapshot);
    tick_callbacks.commit_start = HarnessTickStart;
    tick_callbacks.release_job = HarnessTickRelease;
    tick_callbacks.context = NULL;
    g_r4_hw_result.tick_register_status = (uint32_t)
        R4_TickServiceTarget_Register(&tick_callbacks);
    g_r4_hw_result.tick_timing_configure_status = (uint32_t)
        R4_TickServiceTarget_ConfigureTiming(R4_HW_TICK_CYCLES,
            R4_HW_TICK_INTERVAL_LIMIT_CYCLES);
/* T15 is deliberately split into two images.  q0 and the first release share
 * a TickService invocation, whereas the later occupied release is a distinct
 * requirement and must not borrow evidence from the q0 case. */
#if (R4_HW_CASE_ID == 2U) || (R4_HW_CASE_ID == 3U)
    if (g_r4_hw_result.tick_register_status == (uint32_t)R4_TICK_SERVICE_OK)
    {
        g_r4_hw_result.tick_arm_status = (uint32_t)
            R4_TickServiceTarget_ArmStart(tick_snapshot.service_seq + 2U,
#if (R4_HW_CASE_ID == 3U)
                /* The release test has a four-tick period and a five-tick
                 * suspension.  That interval contains exactly one later
                 * occupied release (q0+4), never a second q0+8 release. */
                4U);
#else
                2U);
#endif
    }
    else
    {
        g_r4_hw_result.tick_arm_status = (uint32_t)R4_TICK_SERVICE_INVALID_STATE;
    }
    if (g_r4_hw_result.tick_arm_status == (uint32_t)R4_TICK_SERVICE_OK)
    {
        uint32_t suspended_at;

        /* In RELEASE, first allow q0/start to occur, then cross only the next
         * release.  In Q0, suspend before q0.  SysTick still enters its real
         * hook; resume never manufactures a service invocation. */
#if (R4_HW_CASE_ID == 3U)
        vTaskDelay(pdMS_TO_TICKS(3U));
#endif
        suspended_at = DWT->CYCCNT;
        vTaskSuspendAll();
        while ((uint32_t)(DWT->CYCCNT - suspended_at) <
#if (R4_HW_CASE_ID == 3U)
               UINT32_C(900000))
#else
               UINT32_C(540000))
#endif
        {
        }
        (void)xTaskResumeAll();
        if (R4_TickServiceTarget_GetSnapshot(&tick_snapshot) ==
            R4_TICK_SERVICE_OK)
        {
            g_r4_hw_result.tick_service_seq_after_suspension =
                tick_snapshot.service_seq;
        }
    }
#endif
#if (R4_HW_CASE_ID == 8U)
    /* Establish an ordinary real-hook interval first.  Then withhold the
     * physical SysTick IRQ long enough to exceed the configured DWT bound;
     * restoring PRIMASK lets the pending real vector—not a test helper—latch
     * the fault on its next hook entrance. */
    vTaskDelay(pdMS_TO_TICKS(2U));
    tick_gap_saved_primask = __get_PRIMASK();
    __disable_irq();
    tick_gap_mask_start = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - tick_gap_mask_start) <
           R4_HW_TICK_GAP_MASK_CYCLES)
    {
    }
    __set_PRIMASK(tick_gap_saved_primask);
    vTaskDelay(pdMS_TO_TICKS(2U));
#endif
#if (R4_HW_CASE_ID == 9U)
    RunMicrobenchmark();
    /* Benchmark samples must not include a tick wait, but the formal image
     * still has to witness the normal production SysTick/tick-hook route. */
    vTaskDelay(pdMS_TO_TICKS(2U));
#endif
#if (R4_HW_CASE_ID == 10U)
    /* Apply must return a caller that entered unmasked to PRIMASK=0, and a
     * caller that entered with PRIMASK=1 to exactly that same masked state.
     * BASEPRI is only witnessed for non-modification; this test never owns it. */
    g_r4_hw_result.mask_normal_primask_before = __get_PRIMASK();
    g_r4_hw_result.mask_basepri_before = __get_BASEPRI();
    g_r4_hw_result.mask_normal_status = (uint32_t)
        R4_RuntimeTarget_TestApplyEvent(R4_RUNTIME_EVENT_CHECKPOINT, 0U);
    g_r4_hw_result.mask_normal_primask_after = __get_PRIMASK();
    {
        uint32_t saved_primask = __get_PRIMASK();
        __disable_irq();
        g_r4_hw_result.mask_masked_primask_before = __get_PRIMASK();
        g_r4_hw_result.mask_masked_status = (uint32_t)
            R4_RuntimeTarget_TestApplyEvent(R4_RUNTIME_EVENT_CHECKPOINT, 0U);
        g_r4_hw_result.mask_masked_primask_after = __get_PRIMASK();
        __set_PRIMASK(saved_primask);
    }
    g_r4_hw_result.mask_basepri_after = __get_BASEPRI();
    vTaskDelay(pdMS_TO_TICKS(2U));
#endif
#if (R4_HW_CASE_ID == 11U)
    /* Observe the normal production SysTick route before deliberately
     * invalidating the ledger at the very end of this diagnostic case. */
    vTaskDelay(pdMS_TO_TICKS(2U));
#endif
#if (R4_HW_CASE_ID == 4U)
    ledger = R4_RuntimeTarget_GetLedger();
    if (ledger != NULL)
    {
        g_r4_hw_result.t17_serial_before = ledger->event_serial;
    }
    g_r4_hw_result.t17_arm_status =
        (uint32_t)R4_RuntimeTarget_TestArmPendingIrq();
    g_r4_hw_result.t17_checkpoint_status =
        (uint32_t)R4_RuntimeTarget_Checkpoint();
    ledger = R4_RuntimeTarget_GetLedger();
    if (ledger != NULL)
    {
        /* This includes exactly the checkpoint plus TIM6 enter/exit; other
         * scheduling events may exist but can never make the delta smaller. */
        g_r4_hw_result.t17_serial_after_pending_irq = ledger->event_serial;
        g_r4_hw_result.t17_low_irq_cycles_before = OwnerCycles(
            ledger->irq_buckets, R4_RUNTIME_MAX_IRQ_BUCKETS,
            (uintptr_t)TIM7_IRQn + 16U);
        g_r4_hw_result.t17_high_irq_cycles_before = OwnerCycles(
            ledger->irq_buckets, R4_RUNTIME_MAX_IRQ_BUCKETS,
            (uintptr_t)TIM6_DAC_IRQn + 16U);
    }
    g_r4_hw_result.t17_nested_arm_status =
        (uint32_t)R4_RuntimeTarget_TestArmNestedIrq();
    vTaskDelay(pdMS_TO_TICKS(2U));
    ledger = R4_RuntimeTarget_GetLedger();
    if (ledger != NULL)
    {
        g_r4_hw_result.t17_low_irq_cycles_after = OwnerCycles(
            ledger->irq_buckets, R4_RUNTIME_MAX_IRQ_BUCKETS,
            (uintptr_t)TIM7_IRQn + 16U);
        g_r4_hw_result.t17_high_irq_cycles_after = OwnerCycles(
            ledger->irq_buckets, R4_RUNTIME_MAX_IRQ_BUCKETS,
            (uintptr_t)TIM6_DAC_IRQn + 16U);
    }
#endif
#if (R4_HW_CASE_ID == 6U)
    synthetic_a_task = xTaskCreateStatic(SyntheticTaskA, "R4SynA",
        configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2U,
        synthetic_a_stack, &synthetic_a_tcb);
    synthetic_b_task = xTaskCreateStatic(SyntheticTaskB, "R4SynB",
        configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1U,
        synthetic_b_stack, &synthetic_b_tcb);
    if (synthetic_a_task != NULL)
    {
        g_r4_hw_result.synthetic_task_create_mask |= R4_HW_SYNTHETIC_DONE_A;
    }
    if (synthetic_b_task != NULL)
    {
        g_r4_hw_result.synthetic_task_create_mask |= R4_HW_SYNTHETIC_DONE_B;
    }
    /* Yield the higher-priority harness for long enough that both fixed-loop
     * tasks finish and the actual Idle task receives measurable residency. */
    vTaskDelay(pdMS_TO_TICKS(20U));
#endif
#if (R4_HW_CASE_ID == 14U)
    /* Higher priority guarantees the release crosses a real scheduler handoff.
     * It blocks immediately on creation, then returns to its stable blocked
     * state after the fixed workload so the harness can seal the window. */
    response_task = xTaskCreateStatic(ResponseTask, "R4Rsp",
        configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 4U,
        response_stack, &response_tcb);
    if (response_task != NULL)
    {
        g_r4_hw_result.response_worker_created = 1U;
        g_r4_hw_result.response_release_raw = DWT->CYCCNT;
        (void)xTaskNotifyGive(response_task);
    }
    /* Do not close the window in the same tick as release; this also proves
     * normal SysTick/tick-hook accounting after the synthetic response. */
    vTaskDelay(pdMS_TO_TICKS(2U));
#endif
#if (R4_HW_CASE_ID == 1U) || (R4_HW_CASE_ID == 5U) || (R4_HW_CASE_ID == 7U) || \
    (R4_HW_CASE_ID == 12U)
    (void)memset(&config, 0, sizeof(config));
    config.boot_id = R4_HW_BOOT;
    config.k = 4U;
#if (R4_HW_CASE_ID == 12U)
    g_r4_hw_result.commit_pending_arm_status =
        (uint32_t)R4_RuntimeTarget_TestArmPendingCompletionIrq();
#endif
    g_r4_hw_result.lifecycle_init_status = (uint32_t)R3W3Runtime_Initialize(&config);
    if (g_r4_hw_result.lifecycle_init_status == (uint32_t)R3_W3_RUNTIME_OK)
    {
        start.boot_id = R4_HW_BOOT;
        start.request_id = 1U;
        start.generation = 1U;
        start.configuration_id = 4U;
        g_r4_hw_result.lifecycle_start_status =
            (uint32_t)R3W3Runtime_Start(&start, &ticket);
        if (g_r4_hw_result.lifecycle_start_status == (uint32_t)R3_W3_RUNTIME_OK)
        {
            ledger = R4_RuntimeTarget_GetLedger();
            g_r4_hw_result.soak_configured_ms = R4_HW_SOAK_MS;
            if (ledger != NULL)
            {
                g_r4_hw_result.soak_start_clock_high_word = ledger->clock->high_word;
            }
            (void)R4_RuntimeTarget_ReadNow(&soak_now);
            g_r4_hw_result.soak_start_cycle = soak_now;
#if R4_HW_SOAK_MS > 0U
            /* The coordinator is the R3 lifecycle owner.  It services
             * Clock64 once per second (well below one DWT wrap) and consumes
             * any IRQ-latched fail-closed request through the normal Stop(). */
            soak_elapsed_ms = 0U;
            while (soak_elapsed_ms < R4_HW_SOAK_MS)
            {
                uint32_t remaining = R4_HW_SOAK_MS - soak_elapsed_ms;
                uint32_t delay_ms = remaining < R4_HW_MONITOR_PERIOD_MS ?
                    remaining : R4_HW_MONITOR_PERIOD_MS;

                (void)R4_RuntimeTarget_MonitorService(
                    R4_HW_MONITOR_INTERVAL_LIMIT_CYCLES);
                if (R4_RuntimeTarget_GetHealthSnapshot(&health) != R4_RUNTIME_OK ||
                    health.fail_closed_requested != 0U)
                {
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(delay_ms));
                soak_elapsed_ms += delay_ms;
            }
#endif
#if (R4_HW_CASE_ID == 7U)
            /* S0/S1 are not task calls: wait until real DMA completions have
             * crossed S1 and at least one later completion proves the sealed
             * window is no longer being extended. */
            for (dma_window_wait_tick = 0U;
                 dma_window_wait_tick < R4_HW_DMA_WINDOW_WAIT_TICKS;
                 ++dma_window_wait_tick)
            {
                if ((R3W3Runtime_GetSnapshot(&runtime_snapshot) ==
                     R3_W3_RUNTIME_OK) &&
                    (runtime_snapshot.driver.completion_count >=
                     R4_HW_DMA_WINDOW_MIN_POST_CLOSE_SEQUENCE))
                {
                    break;
                }
                vTaskDelay(1U);
            }
            if (R3W3Runtime_GetSnapshot(&runtime_snapshot) == R3_W3_RUNTIME_OK)
            {
                g_r4_hw_result.dma_window_last_sequence =
                    runtime_snapshot.driver.completion_count;
            }
#endif
            (void)R4_RuntimeTarget_ReadNow(&soak_now);
            g_r4_hw_result.soak_end_cycle = soak_now;
            ledger = R4_RuntimeTarget_GetLedger();
            if (ledger != NULL)
            {
                g_r4_hw_result.soak_end_clock_high_word = ledger->clock->high_word;
            }
            g_r4_hw_result.dma_tail_snapshot_status = (uint32_t)
                R4_RuntimeTarget_GetDmaTailSnapshot(&dma_tail);
            g_r4_hw_result.dma_irq_count = dma_tail.dma_irq_count;
            g_r4_hw_result.dma_yield_requested_count =
                dma_tail.dma_yield_requested_count;
            g_r4_hw_result.dma_no_yield_count = dma_tail.dma_no_yield_count;
            g_r4_hw_result.health_snapshot_status = (uint32_t)
                R4_RuntimeTarget_GetHealthSnapshot(&health);
            g_r4_hw_result.health_first_fault = (uint32_t)health.first_fault;
            g_r4_hw_result.health_fail_closed_requested =
                health.fail_closed_requested;
            g_r4_hw_result.health_monitor_service_count =
                health.monitor_service_count;
            g_r4_hw_result.health_max_monitor_interval_cycles =
                health.max_monitor_interval_cycles;
            g_r4_hw_result.health_monitor_interval_limit_cycles =
                health.monitor_interval_limit_cycles;
            vTaskDelay(pdMS_TO_TICKS(20U));
            stop.stream_ticket = ticket.stream_ticket;
            stop.stop_id = UINT32_C(0x52340002);
            g_r4_hw_result.lifecycle_stop_status =
                (uint32_t)R3W3Runtime_Stop(&stop);
#if (R4_HW_CASE_ID == 7U)
            g_r4_hw_result.dma_window_snapshot_status = (uint32_t)
                R4_RuntimeTarget_GetDmaWindowSnapshot(&dma_window);
            g_r4_hw_result.dma_window_configured = dma_window.configured;
            g_r4_hw_result.dma_window_opened = dma_window.opened;
            g_r4_hw_result.dma_window_closed = dma_window.closed;
            g_r4_hw_result.dma_window_open_sequence = dma_window.open_sequence;
            g_r4_hw_result.dma_window_close_sequence = dma_window.close_sequence;
            if (dma_window.last_sequence > g_r4_hw_result.dma_window_last_sequence)
            {
                g_r4_hw_result.dma_window_last_sequence = dma_window.last_sequence;
            }
            g_r4_hw_result.dma_window_first_error_sequence =
                dma_window.first_error_sequence;
            g_r4_hw_result.dma_window_open_status = (uint32_t)dma_window.open_status;
            g_r4_hw_result.dma_window_close_status = (uint32_t)dma_window.close_status;
            g_r4_hw_result.dma_window_boundary_status =
                (uint32_t)dma_window.boundary_status;
#endif
#if (R4_HW_CASE_ID == 1U)
            /* T12's no-switch arm: the real DMA vector is pended only after
             * sampling is stopped and with no DMA status bit set.  It is not
             * a fabricated completion; it proves the common handler's
             * no-event tail still emits exactly one IRQ exit and requests no
             * scheduler switch. */
            if (g_r4_hw_result.lifecycle_stop_status == (uint32_t)R3_W3_RUNTIME_OK)
            {
                g_r4_hw_result.dma_no_event_snapshot_status = (uint32_t)
                    R4_RuntimeTarget_GetDmaTailSnapshot(&dma_tail);
                g_r4_hw_result.dma_no_event_irq_before = dma_tail.dma_irq_count;
                g_r4_hw_result.dma_no_event_no_yield_before =
                    dma_tail.dma_no_yield_count;
                NVIC_SetPendingIRQ(DMA2_Stream0_IRQn);
                vTaskDelay(pdMS_TO_TICKS(2U));
                g_r4_hw_result.dma_no_event_snapshot_status = (uint32_t)
                    R4_RuntimeTarget_GetDmaTailSnapshot(&dma_tail);
                g_r4_hw_result.dma_no_event_irq_after = dma_tail.dma_irq_count;
                g_r4_hw_result.dma_no_event_no_yield_after =
                    dma_tail.dma_no_yield_count;
            }
#endif
        }
    }
    else
    {
        g_r4_hw_result.lifecycle_start_status = (uint32_t)R3_W3_RUNTIME_INVALID_STATE;
        g_r4_hw_result.lifecycle_stop_status = (uint32_t)R3_W3_RUNTIME_INVALID_STATE;
    }
#endif
/* T17's transaction is deliberately very short.  Keep the window open for
 * two real RTOS ticks after the injected IRQ has returned so this formal
 * image proves the production tick hook, rather than merely configuring it. */
#if (R4_HW_CASE_ID == 4U)
    vTaskDelay(pdMS_TO_TICKS(2U));
#endif
    g_r4_hw_result.checkpoint_status = (uint32_t)R4_RuntimeTarget_Checkpoint();
#if (R4_HW_CASE_ID == 7U)
    /* The actual S1 callback already closed the formal window.  Recording a
     * second CLOSE would turn a production proof into an artificial error. */
    g_r4_hw_result.window_close_status = g_r4_hw_result.dma_window_close_status;
#else
    g_r4_hw_result.window_close_status = (uint32_t)R4_RuntimeTarget_CloseWindow(0U);
#endif
    ledger = R4_RuntimeTarget_GetLedger();
    if (ledger != NULL)
    {
        g_r4_hw_result.t17_window_cycles_at_close = ledger->window_cycles[0];
    }
    /* A post-close settlement may update global ownership totals, but never
     * the sealed window field. */
    g_r4_hw_result.t17_post_close_status =
        (uint32_t)R4_RuntimeTarget_Checkpoint();
#if (R4_HW_CASE_ID == 13U) || (R4_HW_CASE_ID == 14U)
    /* Require real SysTick/tick-hook activity after CLOSE before taking the
     * formal snapshot.  Those events are live-only and must not reopen or
     * extend the sealed CPU window. */
    vTaskDelay(pdMS_TO_TICKS(2U));
#endif
    g_r4_hw_result.tick_snapshot_status = (uint32_t)
        R4_TickServiceTarget_GetSnapshot(&tick_snapshot);
    g_r4_hw_result.tick_start_callback_count = tick_start_callback_count;
    g_r4_hw_result.tick_release_callback_count = tick_release_callback_count;
    g_r4_hw_result.tick_service_seq = tick_snapshot.service_seq;
    g_r4_hw_result.tick_start_count = tick_snapshot.start_count;
    g_r4_hw_result.tick_release_count = tick_snapshot.release_count;
    g_r4_hw_result.tick_skipped_count = tick_snapshot.skipped_count;
    g_r4_hw_result.tick_timing_snapshot_status = (uint32_t)
        R4_TickServiceTarget_GetTiming(&tick_timing);
    g_r4_hw_result.tick_timing_service_count = tick_timing.service_count;
    g_r4_hw_result.tick_timing_max_interval_cycles = tick_timing.max_interval_cycles;
    g_r4_hw_result.tick_timing_max_phase_error_cycles =
        tick_timing.max_phase_error_cycles;
    g_r4_hw_result.tick_timing_over_limit_count =
        tick_timing.over_limit_interval_count;
    g_r4_hw_result.tick_systick_snapshot_status = (uint32_t)
        R4_RuntimeTarget_GetSysTickTraceSnapshot(&systick_trace);
    g_r4_hw_result.tick_systick_enter_count = systick_trace.enter_count;
    g_r4_hw_result.tick_systick_exit_count = systick_trace.exit_count;
#if (R4_HW_CASE_ID == 8U)
    g_r4_hw_result.tick_gap_health_status = (uint32_t)
        R4_RuntimeTarget_GetHealthSnapshot(&tick_gap_health);
    g_r4_hw_result.tick_gap_first_fault = (uint32_t)tick_gap_health.first_fault;
    g_r4_hw_result.tick_gap_fail_closed_requested =
        tick_gap_health.fail_closed_requested;
    g_r4_hw_result.tick_gap_over_limit_count =
        tick_timing.over_limit_interval_count;
    g_r4_hw_result.tick_gap_max_interval_cycles =
        tick_timing.max_interval_cycles;
    g_r4_hw_result.tick_gap_interval_limit_cycles =
        tick_timing.interval_limit_cycles;
#endif
    ledger = R4_RuntimeTarget_GetLedger();
    if (ledger != NULL)
    {
        g_r4_hw_result.t17_window_cycles_after_close = ledger->window_cycles[0];
        g_r4_hw_result.runtime_status = (uint32_t)R4_RuntimeLedger_GetStatus(ledger);
        g_r4_hw_result.event_serial = ledger->event_serial;
        g_r4_hw_result.last_time = ledger->last_time;
        g_r4_hw_result.task_cycles = ledger->task_cycles;
        g_r4_hw_result.irq_cycles = ledger->irq_cycles;
        g_r4_hw_result.window_cycles = ledger->window_cycles[0];
        g_r4_hw_result.irq_depth = ledger->irq_depth;
#if (R4_HW_CASE_ID == 13U)
        g_r4_hw_result.target_window_task_cycles = ledger->window_task_cycles[0];
        g_r4_hw_result.target_window_irq_cycles = ledger->window_irq_cycles[0];
        g_r4_hw_result.target_window_idle_cycles = ledger->window_idle_cycles[0];
        g_r4_hw_result.target_window_unclassified_cycles =
            ledger->window_unclassified_cycles[0];
#endif
#if (R4_HW_CASE_ID == 14U)
        g_r4_hw_result.response_owner_cycles = OwnerCycles(
            ledger->window_task_buckets[0], R4_RUNTIME_MAX_TASK_BUCKETS,
            (uintptr_t)response_task);
#endif
#if (R4_HW_CASE_ID == 6U)
        g_r4_hw_result.synthetic_task_a_cycles = OwnerCycles(
            ledger->window_task_buckets[0], R4_RUNTIME_MAX_TASK_BUCKETS,
            (uintptr_t)synthetic_a_task);
        g_r4_hw_result.synthetic_task_b_cycles = OwnerCycles(
            ledger->window_task_buckets[0], R4_RUNTIME_MAX_TASK_BUCKETS,
            (uintptr_t)synthetic_b_task);
        g_r4_hw_result.synthetic_window_task_cycles = ledger->window_task_cycles[0];
        g_r4_hw_result.synthetic_window_irq_cycles = ledger->window_irq_cycles[0];
        g_r4_hw_result.synthetic_window_idle_cycles = ledger->window_idle_cycles[0];
        g_r4_hw_result.synthetic_window_unclassified_cycles =
            ledger->window_unclassified_cycles[0];
#endif
#if (R4_HW_CASE_ID == 13U)
    /* Genuine post-close scheduling/tick activity must remain live-only. */
    vTaskDelay(pdMS_TO_TICKS(2U));
    g_r4_hw_result.t17_post_close_status =
        (uint32_t)R4_RuntimeTarget_Checkpoint();
    ledger = R4_RuntimeTarget_GetLedger();
    if (ledger != NULL)
    {
        g_r4_hw_result.t17_window_cycles_after_close = ledger->window_cycles[0];
        g_r4_hw_result.runtime_status = (uint32_t)R4_RuntimeLedger_GetStatus(ledger);
        g_r4_hw_result.event_serial = ledger->event_serial;
        g_r4_hw_result.last_time = ledger->last_time;
        g_r4_hw_result.task_cycles = ledger->task_cycles;
        g_r4_hw_result.irq_cycles = ledger->irq_cycles;
    }
#endif
    }
#if (R4_HW_CASE_ID == 5U) || (R4_HW_CASE_ID == 12U)
    if (R4_RuntimeTarget_GetCompletionTiming(&timing) == R4_RUNTIME_OK)
    {
        g_r4_hw_result.completion_malformed_count = timing.malformed_count;
        g_r4_hw_result.completion_discarded_count = timing.discarded_count;
        g_r4_hw_result.completion_count = timing.completed_count;
        g_r4_hw_result.completion_lock_count = timing.lock_count;
        g_r4_hw_result.completion_commit_count = timing.commit_count;
        g_r4_hw_result.completion_unlock_count = timing.unlock_count;
        g_r4_hw_result.completion_max_total_cycles = timing.max_total_cycles;
        g_r4_hw_result.completion_max_prefix_cycles = timing.max_prefix_cycles;
        g_r4_hw_result.completion_max_suffix_cycles = timing.max_suffix_cycles;
        g_r4_hw_result.completion_budget_pass =
            (timing.completed_count != 0U) && (timing.malformed_count == 0U) &&
            (timing.max_total_cycles <= R4_HW_COMPLETE_BUDGET_CYCLES) ? 1U : 0U;
    }
#endif
#if (R4_HW_CASE_ID == 12U)
    g_r4_hw_result.commit_pending_snapshot_status = (uint32_t)
        R4_RuntimeTarget_TestGetCompletionPendingIrqSnapshot(
            &g_r4_hw_result.commit_pending_arm_count,
            &g_r4_hw_result.commit_pending_irq_count,
            &g_r4_hw_result.commit_pending_active_at_irq);
#endif
#if (R4_HW_CASE_ID == 4U)
    /* Deliberately repeat the already-completed TIM6 exit.  This is final:
     * RuntimeEvent correctly latches the fault and no later event may hide it. */
    g_r4_hw_result.t17_duplicate_exit_status =
        (uint32_t)R4_RuntimeTarget_TestInjectDuplicateExit(
            (uint32_t)TIM6_DAC_IRQn + 16U);
#endif
#if (R4_HW_CASE_ID == 11U)
    ledger = R4_RuntimeTarget_GetLedger();
    if (ledger != NULL)
    {
        g_r4_hw_result.time_regression_serial_before = ledger->event_serial;
    }
    g_r4_hw_result.time_regression_inject_status = (uint32_t)
        R4_RuntimeTarget_TestInjectTimeRegression();
    g_r4_hw_result.time_regression_post_status = (uint32_t)
        R4_RuntimeTarget_TestApplyEvent(R4_RUNTIME_EVENT_CHECKPOINT, 0U);
    ledger = R4_RuntimeTarget_GetLedger();
    if (ledger != NULL)
    {
        g_r4_hw_result.time_regression_serial_after = ledger->event_serial;
    }
#endif
    EvaluateFormalInvariants();
    __DMB();
    g_r4_hw_result.completed_magic = R4_HW_COMPLETE;
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

void R4_HW_Start(void)
{
    TaskHandle_t task;

    (void)memset((void *)&g_r4_hw_result, 0, sizeof(g_r4_hw_result));
    g_r4_hw_result.magic = R4_HW_MAGIC;
    g_r4_hw_result.case_id = R4_HW_CASE_ID;
    g_r4_hw_result.schema_version = R4_HW_SCHEMA_VERSION;
#if (R4_HW_CASE_ID == 6U)
    g_r4_hw_result.synthetic_schema_version = R4_HW_SYNTHETIC_SCHEMA_VERSION;
#endif
    g_r4_hw_result.init_status = (uint32_t)R4_RuntimeTarget_Initialize();
    if (g_r4_hw_result.init_status != (uint32_t)R4_RUNTIME_OK)
    {
        __DMB();
        g_r4_hw_result.completed_magic = R4_HW_COMPLETE;
        return;
    }
    task = xTaskCreateStatic(HarnessTask, "R4HW", R4_HW_STACK_WORDS, NULL,
        tskIDLE_PRIORITY + 3U, harness_stack, &harness_tcb);
    if (task == NULL)
    {
        g_r4_hw_result.runtime_status = (uint32_t)R4_RUNTIME_PLATFORM_ERROR;
        __DMB();
        g_r4_hw_result.completed_magic = R4_HW_COMPLETE;
        return;
    }
    vTaskStartScheduler();
    g_r4_hw_result.runtime_status = (uint32_t)R4_RUNTIME_PLATFORM_ERROR;
    __DMB();
    g_r4_hw_result.completed_magic = R4_HW_COMPLETE;
}

void vApplicationGetIdleTaskMemory(
    StaticTask_t **ppxIdleTaskTCBBuffer,
    StackType_t **ppxIdleTaskStackBuffer,
    configSTACK_DEPTH_TYPE *puxIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &idle_tcb;
    *ppxIdleTaskStackBuffer = idle_stack;
    *puxIdleTaskStackSize = configMINIMAL_STACK_SIZE;
    R4_RuntimeTarget_BindIdleTask((void *)&idle_tcb);
}
