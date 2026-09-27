#ifndef R4_HW_HARNESS_H
#define R4_HW_HARNESS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define R4_HW_MAGIC UINT32_C(0x52344857)
#define R4_HW_COMPLETE UINT32_C(0x5234444E)
#define R4_HW_SCHEMA_VERSION UINT32_C(1)
#define R4_HW_SYNTHETIC_SCHEMA_VERSION UINT32_C(1)
/* Target-result extension ABI consumed by tools/r4/r4_evidence.py.  The
 * stable eleven-word prefix intentionally remains schema v1. */
#define R4_HW_WINDOW_CYCLES_WORD 20U
#define R4_HW_T17_ARM_STATUS_WORD 40U
#define R4_HW_T17_NESTED_ARM_STATUS_WORD 41U
#define R4_HW_T17_CHECKPOINT_STATUS_WORD 42U
#define R4_HW_T17_POST_CLOSE_STATUS_WORD 43U
#define R4_HW_T17_DUPLICATE_EXIT_STATUS_WORD 44U
#define R4_HW_T17_SERIAL_BEFORE_WORD 46U
#define R4_HW_T17_SERIAL_AFTER_PENDING_WORD 48U
#define R4_HW_T17_LOW_IRQ_BEFORE_WORD 50U
#define R4_HW_T17_HIGH_IRQ_BEFORE_WORD 52U
#define R4_HW_T17_LOW_IRQ_AFTER_WORD 54U
#define R4_HW_T17_HIGH_IRQ_AFTER_WORD 56U
#define R4_HW_T17_WINDOW_AT_CLOSE_WORD 58U
#define R4_HW_T17_WINDOW_AFTER_CLOSE_WORD 60U
#define R4_HW_T12_SOAK_CONFIGURED_MS_WORD 88U
#define R4_HW_T12_SOAK_START_HIGH_WORD 89U
#define R4_HW_T12_SOAK_END_HIGH_WORD 90U
#define R4_HW_T12_DMA_TAIL_STATUS_WORD 91U
#define R4_HW_T12_DMA_IRQ_COUNT_WORD 96U
#define R4_HW_T12_DMA_YIELD_COUNT_WORD 98U
#define R4_HW_T12_DMA_NO_YIELD_COUNT_WORD 100U
#define R4_HW_T12_NO_EVENT_STATUS_WORD 102U
#define R4_HW_T12_NO_EVENT_IRQ_BEFORE_WORD 104U
#define R4_HW_T12_NO_EVENT_IRQ_AFTER_WORD 106U
#define R4_HW_T12_NO_EVENT_NO_YIELD_BEFORE_WORD 108U
#define R4_HW_T12_NO_EVENT_NO_YIELD_AFTER_WORD 110U
#define R4_HW_T12_HEALTH_STATUS_WORD 112U
#define R4_HW_T12_HEALTH_FIRST_FAULT_WORD 113U
#define R4_HW_T12_HEALTH_FAIL_CLOSED_WORD 114U
#define R4_HW_T12_HEALTH_SERVICE_COUNT_WORD 116U
#define R4_HW_T12_HEALTH_MAX_INTERVAL_WORD 118U
#define R4_HW_T12_HEALTH_INTERVAL_LIMIT_WORD 120U
#define R4_HW_T15_REGISTER_STATUS_WORD 62U
#define R4_HW_T15_ARM_STATUS_WORD 63U
#define R4_HW_T15_SNAPSHOT_STATUS_WORD 64U
#define R4_HW_T15_START_CALLBACK_WORD 65U
#define R4_HW_T15_RELEASE_CALLBACK_WORD 66U
#define R4_HW_T15_SERVICE_SEQ_WORD 68U
#define R4_HW_T15_START_COUNT_WORD 70U
#define R4_HW_T15_RELEASE_COUNT_WORD 72U
#define R4_HW_T15_SKIPPED_COUNT_WORD 74U
#define R4_HW_T15_SEQUENCE_AFTER_SUSPENSION_WORD 76U
#define R4_HW_T15_TIMING_CONFIGURE_WORD 78U
#define R4_HW_T15_TIMING_SNAPSHOT_WORD 79U
#define R4_HW_T15_TIMING_SERVICE_COUNT_WORD 80U
#define R4_HW_T15_TIMING_MAX_INTERVAL_WORD 82U
#define R4_HW_T15_TIMING_PHASE_ERROR_WORD 84U
#define R4_HW_T15_TIMING_OVER_LIMIT_WORD 86U
#define R4_HW_SYNTHETIC_CREATE_MASK_WORD 122U
#define R4_HW_SYNTHETIC_A_CYCLES_WORD 128U
#define R4_HW_SYNTHETIC_B_CYCLES_WORD 130U
#define R4_HW_SYNTHETIC_WINDOW_TASK_WORD 132U
#define R4_HW_SYNTHETIC_WINDOW_IRQ_WORD 134U
#define R4_HW_SYNTHETIC_WINDOW_IDLE_WORD 136U
#define R4_HW_SYNTHETIC_WINDOW_UNCLASSIFIED_WORD 138U
#define R4_HW_DMA_WINDOW_SNAPSHOT_WORD 140U
#define R4_HW_T15_SYSTICK_SNAPSHOT_STATUS_WORD 151U
#define R4_HW_T15_SYSTICK_ENTER_WORD 152U
#define R4_HW_T15_SYSTICK_EXIT_WORD 154U
#define R4_HW_T15_PHASE_Q0_WORD 156U
#define R4_HW_T15_PHASE_TIM2_BEFORE_WORD 158U
#define R4_HW_T15_PHASE_TIM2_AFTER_WORD 160U
#define R4_HW_T15_PHASE_TIM2_CEN_WORD 162U
#define R4_HW_TICK_GAP_HEALTH_STATUS_WORD 163U
#define R4_HW_TICK_GAP_FIRST_FAULT_WORD 164U
#define R4_HW_TICK_GAP_FAIL_CLOSED_WORD 165U
#define R4_HW_TICK_GAP_OVER_LIMIT_WORD 166U
#define R4_HW_TICK_GAP_MAX_INTERVAL_WORD 168U
#define R4_HW_TICK_GAP_INTERVAL_LIMIT_WORD 170U
#define R4_HW_MICROBENCH_SAMPLE_COUNT_WORD 172U
#define R4_HW_MICROBENCH_TASK_MIN_WORD 174U
#define R4_HW_MICROBENCH_TASK_MEDIAN_WORD 176U
#define R4_HW_MICROBENCH_TASK_MAX_WORD 178U
#define R4_HW_MICROBENCH_IRQ_MIN_WORD 180U
#define R4_HW_MICROBENCH_IRQ_MEDIAN_WORD 182U
#define R4_HW_MICROBENCH_IRQ_MAX_WORD 184U
#define R4_HW_MICROBENCH_WINDOW_MIN_WORD 186U
#define R4_HW_MICROBENCH_WINDOW_MEDIAN_WORD 188U
#define R4_HW_MICROBENCH_WINDOW_MAX_WORD 190U
#define R4_HW_MICROBENCH_NESTED_MIN_WORD 192U
#define R4_HW_MICROBENCH_NESTED_MEDIAN_WORD 194U
#define R4_HW_MICROBENCH_NESTED_MAX_WORD 196U
#define R4_HW_MASK_NORMAL_STATUS_WORD 198U
#define R4_HW_MASK_NORMAL_PRIMASK_BEFORE_WORD 199U
#define R4_HW_MASK_NORMAL_PRIMASK_AFTER_WORD 200U
#define R4_HW_MASK_MASKED_STATUS_WORD 201U
#define R4_HW_MASK_MASKED_PRIMASK_BEFORE_WORD 202U
#define R4_HW_MASK_MASKED_PRIMASK_AFTER_WORD 203U
#define R4_HW_MASK_BASEPRI_BEFORE_WORD 204U
#define R4_HW_MASK_BASEPRI_AFTER_WORD 205U
#define R4_HW_TIME_REGRESSION_INJECT_STATUS_WORD 206U
#define R4_HW_TIME_REGRESSION_POST_STATUS_WORD 207U
#define R4_HW_TIME_REGRESSION_SERIAL_BEFORE_WORD 208U
#define R4_HW_TIME_REGRESSION_SERIAL_AFTER_WORD 210U
#define R4_HW_COMMIT_PENDING_ARM_STATUS_WORD 212U
#define R4_HW_COMMIT_PENDING_SNAPSHOT_STATUS_WORD 213U
#define R4_HW_COMMIT_PENDING_ARM_COUNT_WORD 214U
#define R4_HW_COMMIT_PENDING_IRQ_COUNT_WORD 215U
#define R4_HW_COMMIT_PENDING_ACTIVE_AT_IRQ_WORD 216U
/* Five preceding uint32_t fields end at word 217; natural uint64_t alignment
 * reserves word 217 and makes the first 64-bit target-window field word 218. */
#define R4_HW_TARGET_WINDOW_TASK_WORD 218U
#define R4_HW_TARGET_WINDOW_IRQ_WORD 220U
#define R4_HW_TARGET_WINDOW_IDLE_WORD 222U
#define R4_HW_TARGET_WINDOW_UNCLASSIFIED_WORD 224U
#define R4_HW_RESPONSE_CREATED_WORD 226U
#define R4_HW_RESPONSE_DONE_WORD 227U
#define R4_HW_RESPONSE_RELEASE_RAW_WORD 228U
#define R4_HW_RESPONSE_START_RAW_WORD 229U
#define R4_HW_RESPONSE_COMPLETE_RAW_WORD 230U
#define R4_HW_RESPONSE_WORK_RAW_WORD 231U
#define R4_HW_RESPONSE_OWNER_CYCLES_WORD 232U
#define R4_HW_MASK_TIMING_RESET_WORD 234U
#define R4_HW_MASK_TIMING_SAMPLE_COUNT_WORD 235U
#define R4_HW_MASK_TIMING_MAX_WORD 236U
#define R4_HW_MASK_TIMING_LIMIT_WORD 238U

/* Stable prefix consumed by the immutable-evidence reader.  New diagnostic
 * fields belong after this prefix; its offsets are a target evidence ABI. */
typedef enum
{
    R4_HW_INVARIANT_INIT = UINT32_C(1) << 0,
    R4_HW_INVARIANT_RUNTIME = UINT32_C(1) << 1,
    R4_HW_INVARIANT_TICK = UINT32_C(1) << 2,
    R4_HW_INVARIANT_WINDOW = UINT32_C(1) << 3,
    R4_HW_INVARIANT_CASE = UINT32_C(1) << 4,
    R4_HW_INVARIANT_HEALTH = UINT32_C(1) << 5,
    R4_HW_INVARIANT_COMPLETION = UINT32_C(1) << 6
} R4HwInvariant;

typedef struct
{
    uint32_t magic;
    uint32_t case_id;
    uint32_t schema_version;
    uint32_t terminal_pass;
    uint32_t invariant_failure_mask;
    uint32_t init_status;
    uint32_t window_open_status;
    uint32_t window_close_status;
    uint32_t checkpoint_status;
    uint32_t runtime_status;
    uint32_t completed_magic;
    uint64_t event_serial;
    uint64_t last_time;
    uint64_t task_cycles;
    uint64_t irq_cycles;
    uint64_t window_cycles;
    uint32_t irq_depth;
    uint32_t completion_malformed_count;
    uint32_t completion_discarded_count;
    uint32_t lifecycle_init_status;
    uint32_t lifecycle_start_status;
    uint32_t lifecycle_stop_status;
    uint32_t completion_count;
    uint32_t completion_budget_pass;
    uint32_t completion_lock_count;
    uint32_t completion_commit_count;
    uint32_t completion_unlock_count;
    uint64_t completion_max_total_cycles;
    uint64_t completion_max_prefix_cycles;
    uint64_t completion_max_suffix_cycles;
    uint32_t t17_arm_status;
    uint32_t t17_nested_arm_status;
    uint32_t t17_checkpoint_status;
    uint32_t t17_post_close_status;
    uint32_t t17_duplicate_exit_status;
    uint64_t t17_serial_before;
    uint64_t t17_serial_after_pending_irq;
    uint64_t t17_low_irq_cycles_before;
    uint64_t t17_high_irq_cycles_before;
    uint64_t t17_low_irq_cycles_after;
    uint64_t t17_high_irq_cycles_after;
    uint64_t t17_window_cycles_at_close;
    uint64_t t17_window_cycles_after_close;
    uint32_t tick_register_status;
    uint32_t tick_arm_status;
    uint32_t tick_snapshot_status;
    uint32_t tick_start_callback_count;
    uint32_t tick_release_callback_count;
    uint64_t tick_service_seq;
    uint64_t tick_start_count;
    uint64_t tick_release_count;
    uint64_t tick_skipped_count;
    uint64_t tick_service_seq_after_suspension;
    uint32_t tick_timing_configure_status;
    uint32_t tick_timing_snapshot_status;
    uint64_t tick_timing_service_count;
    uint64_t tick_timing_max_interval_cycles;
    uint64_t tick_timing_max_phase_error_cycles;
    uint64_t tick_timing_over_limit_count;
    uint32_t soak_configured_ms;
    uint32_t soak_start_clock_high_word;
    uint32_t soak_end_clock_high_word;
    uint32_t dma_tail_snapshot_status;
    uint64_t soak_start_cycle;
    uint64_t soak_end_cycle;
    uint64_t dma_irq_count;
    uint64_t dma_yield_requested_count;
    uint64_t dma_no_yield_count;
    uint32_t dma_no_event_snapshot_status;
    uint64_t dma_no_event_irq_before;
    uint64_t dma_no_event_irq_after;
    uint64_t dma_no_event_no_yield_before;
    uint64_t dma_no_event_no_yield_after;
    uint32_t health_snapshot_status;
    uint32_t health_first_fault;
    uint32_t health_fail_closed_requested;
    uint64_t health_monitor_service_count;
    uint64_t health_max_monitor_interval_cycles;
    uint64_t health_monitor_interval_limit_cycles;
    uint32_t synthetic_task_create_mask;
    uint32_t synthetic_schema_version;
    uint32_t synthetic_task_done_mask;
    uint32_t synthetic_task_a_iterations;
    uint32_t synthetic_task_b_iterations;
    uint64_t synthetic_task_a_cycles;
    uint64_t synthetic_task_b_cycles;
    uint64_t synthetic_window_task_cycles;
    uint64_t synthetic_window_irq_cycles;
    uint64_t synthetic_window_idle_cycles;
    uint64_t synthetic_window_unclassified_cycles;
    uint32_t dma_window_snapshot_status;
    uint32_t dma_window_configured;
    uint32_t dma_window_opened;
    uint32_t dma_window_closed;
    uint32_t dma_window_open_sequence;
    uint32_t dma_window_close_sequence;
    uint32_t dma_window_last_sequence;
    uint32_t dma_window_first_error_sequence;
    uint32_t dma_window_open_status;
    uint32_t dma_window_close_status;
    uint32_t dma_window_boundary_status;
    uint32_t tick_systick_snapshot_status;
    uint64_t tick_systick_enter_count;
    uint64_t tick_systick_exit_count;
    uint64_t tick_phase_q0;
    uint64_t tick_phase_tim2_before;
    uint64_t tick_phase_tim2_after;
    uint32_t tick_phase_tim2_cen;
    uint32_t tick_gap_health_status;
    uint32_t tick_gap_first_fault;
    uint32_t tick_gap_fail_closed_requested;
    uint64_t tick_gap_over_limit_count;
    uint64_t tick_gap_max_interval_cycles;
    uint64_t tick_gap_interval_limit_cycles;
    /* Diagnostic R4-A21 measurements.  Each number brackets the complete
     * production RuntimeEvent Apply transaction; sample values themselves
     * remain outside the timed interval. */
    uint32_t microbench_sample_count;
    uint64_t microbench_task_min_cycles;
    uint64_t microbench_task_median_cycles;
    uint64_t microbench_task_max_cycles;
    uint64_t microbench_irq_min_cycles;
    uint64_t microbench_irq_median_cycles;
    uint64_t microbench_irq_max_cycles;
    uint64_t microbench_window_min_cycles;
    uint64_t microbench_window_median_cycles;
    uint64_t microbench_window_max_cycles;
    uint64_t microbench_nested_min_cycles;
    uint64_t microbench_nested_median_cycles;
    uint64_t microbench_nested_max_cycles;
    uint32_t mask_normal_status;
    uint32_t mask_normal_primask_before;
    uint32_t mask_normal_primask_after;
    uint32_t mask_masked_status;
    uint32_t mask_masked_primask_before;
    uint32_t mask_masked_primask_after;
    uint32_t mask_basepri_before;
    uint32_t mask_basepri_after;
    uint32_t time_regression_inject_status;
    uint32_t time_regression_post_status;
    uint64_t time_regression_serial_before;
    uint64_t time_regression_serial_after;
    uint32_t commit_pending_arm_status;
    uint32_t commit_pending_snapshot_status;
    uint32_t commit_pending_arm_count;
    uint32_t commit_pending_irq_count;
    uint32_t commit_pending_active_at_irq;
    uint64_t target_window_task_cycles;
    uint64_t target_window_irq_cycles;
    uint64_t target_window_idle_cycles;
    uint64_t target_window_unclassified_cycles;
    /* Case 14 only.  The raw DWT endpoints are intentionally read directly
     * by the synthetic worker, outside RuntimeEvent/ledger APIs. */
    uint32_t response_worker_created;
    uint32_t response_worker_done;
    uint32_t response_release_raw;
    uint32_t response_start_raw;
    uint32_t response_complete_raw;
    uint32_t response_work_raw;
    uint64_t response_owner_cycles;
    uint32_t mask_timing_reset_status;
    uint32_t mask_timing_sample_count;
    uint64_t mask_timing_max_cycles;
    uint64_t mask_timing_limit_cycles;
} R4HwHarnessResult;

_Static_assert(offsetof(R4HwHarnessResult, synthetic_task_create_mask) ==
    (R4_HW_SYNTHETIC_CREATE_MASK_WORD * sizeof(uint32_t)),
    "R4 synthetic evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, dma_window_snapshot_status) ==
    (R4_HW_DMA_WINDOW_SNAPSHOT_WORD * sizeof(uint32_t)),
    "R4 DMA-window evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, window_cycles) ==
    (R4_HW_WINDOW_CYCLES_WORD * sizeof(uint32_t)),
    "R4 window-cycle evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, t17_arm_status) ==
    (R4_HW_T17_ARM_STATUS_WORD * sizeof(uint32_t)),
    "R4 T17 arm-status evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, t17_duplicate_exit_status) ==
    (R4_HW_T17_DUPLICATE_EXIT_STATUS_WORD * sizeof(uint32_t)),
    "R4 T17 duplicate-exit evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, t17_serial_before) ==
    (R4_HW_T17_SERIAL_BEFORE_WORD * sizeof(uint32_t)),
    "R4 T17 serial-before evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, t17_serial_after_pending_irq) ==
    (R4_HW_T17_SERIAL_AFTER_PENDING_WORD * sizeof(uint32_t)),
    "R4 T17 serial-after evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, t17_low_irq_cycles_before) ==
    (R4_HW_T17_LOW_IRQ_BEFORE_WORD * sizeof(uint32_t)),
    "R4 T17 low-IRQ-before evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, t17_high_irq_cycles_after) ==
    (R4_HW_T17_HIGH_IRQ_AFTER_WORD * sizeof(uint32_t)),
    "R4 T17 high-IRQ-after evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, t17_window_cycles_after_close) ==
    (R4_HW_T17_WINDOW_AFTER_CLOSE_WORD * sizeof(uint32_t)),
    "R4 T17 sealed-window evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, soak_configured_ms) ==
    (R4_HW_T12_SOAK_CONFIGURED_MS_WORD * sizeof(uint32_t)),
    "R4 T12 soak-duration evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, dma_irq_count) ==
    (R4_HW_T12_DMA_IRQ_COUNT_WORD * sizeof(uint32_t)),
    "R4 T12 DMA-IRQ evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, dma_yield_requested_count) ==
    (R4_HW_T12_DMA_YIELD_COUNT_WORD * sizeof(uint32_t)),
    "R4 T12 DMA-yield evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, dma_no_event_irq_before) ==
    (R4_HW_T12_NO_EVENT_IRQ_BEFORE_WORD * sizeof(uint32_t)),
    "R4 T12 no-event IRQ-before evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, health_monitor_service_count) ==
    (R4_HW_T12_HEALTH_SERVICE_COUNT_WORD * sizeof(uint32_t)),
    "R4 T12 health-service evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, tick_register_status) ==
    (R4_HW_T15_REGISTER_STATUS_WORD * sizeof(uint32_t)),
    "R4 T15 register evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, tick_service_seq) ==
    (R4_HW_T15_SERVICE_SEQ_WORD * sizeof(uint32_t)),
    "R4 T15 service-sequence evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, tick_timing_service_count) ==
    (R4_HW_T15_TIMING_SERVICE_COUNT_WORD * sizeof(uint32_t)),
    "R4 T15 timing-service evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, synthetic_task_a_cycles) ==
    (R4_HW_SYNTHETIC_A_CYCLES_WORD * sizeof(uint32_t)),
    "R4 synthetic-A evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, synthetic_task_b_cycles) ==
    (R4_HW_SYNTHETIC_B_CYCLES_WORD * sizeof(uint32_t)),
    "R4 synthetic-B evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, synthetic_window_task_cycles) ==
    (R4_HW_SYNTHETIC_WINDOW_TASK_WORD * sizeof(uint32_t)),
    "R4 synthetic-task-window evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, synthetic_window_irq_cycles) ==
    (R4_HW_SYNTHETIC_WINDOW_IRQ_WORD * sizeof(uint32_t)),
    "R4 synthetic-IRQ-window evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, synthetic_window_idle_cycles) ==
    (R4_HW_SYNTHETIC_WINDOW_IDLE_WORD * sizeof(uint32_t)),
    "R4 synthetic-idle-window evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, synthetic_window_unclassified_cycles) ==
    (R4_HW_SYNTHETIC_WINDOW_UNCLASSIFIED_WORD * sizeof(uint32_t)),
    "R4 synthetic-unclassified-window evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, tick_systick_snapshot_status) ==
    (R4_HW_T15_SYSTICK_SNAPSHOT_STATUS_WORD * sizeof(uint32_t)),
    "R4 SysTick snapshot-status evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, tick_systick_enter_count) ==
    (R4_HW_T15_SYSTICK_ENTER_WORD * sizeof(uint32_t)),
    "R4 SysTick enter evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, tick_systick_exit_count) ==
    (R4_HW_T15_SYSTICK_EXIT_WORD * sizeof(uint32_t)),
    "R4 SysTick exit evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, tick_phase_q0) ==
    (R4_HW_T15_PHASE_Q0_WORD * sizeof(uint32_t)),
    "R4 TIM2 phase-q0 evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, tick_phase_tim2_before) ==
    (R4_HW_T15_PHASE_TIM2_BEFORE_WORD * sizeof(uint32_t)),
    "R4 TIM2 phase-before evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, tick_phase_tim2_after) ==
    (R4_HW_T15_PHASE_TIM2_AFTER_WORD * sizeof(uint32_t)),
    "R4 TIM2 phase-after evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, tick_phase_tim2_cen) ==
    (R4_HW_T15_PHASE_TIM2_CEN_WORD * sizeof(uint32_t)),
    "R4 TIM2 phase-CEN evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, tick_gap_health_status) ==
    (R4_HW_TICK_GAP_HEALTH_STATUS_WORD * sizeof(uint32_t)),
    "R4 tick-gap health evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, tick_gap_over_limit_count) ==
    (R4_HW_TICK_GAP_OVER_LIMIT_WORD * sizeof(uint32_t)),
    "R4 tick-gap over-limit evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, microbench_sample_count) ==
    (R4_HW_MICROBENCH_SAMPLE_COUNT_WORD * sizeof(uint32_t)),
    "R4 microbenchmark sample-count evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, microbench_task_min_cycles) ==
    (R4_HW_MICROBENCH_TASK_MIN_WORD * sizeof(uint32_t)),
    "R4 microbenchmark task evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, microbench_nested_max_cycles) ==
    (R4_HW_MICROBENCH_NESTED_MAX_WORD * sizeof(uint32_t)),
    "R4 microbenchmark nested evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, mask_normal_status) ==
    (R4_HW_MASK_NORMAL_STATUS_WORD * sizeof(uint32_t)),
    "R4 mask-restore normal evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, mask_basepri_after) ==
    (R4_HW_MASK_BASEPRI_AFTER_WORD * sizeof(uint32_t)),
    "R4 mask-restore BASEPRI evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, time_regression_inject_status) ==
    (R4_HW_TIME_REGRESSION_INJECT_STATUS_WORD * sizeof(uint32_t)),
    "R4 time-regression status evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, time_regression_serial_before) ==
    (R4_HW_TIME_REGRESSION_SERIAL_BEFORE_WORD * sizeof(uint32_t)),
    "R4 time-regression serial evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, commit_pending_arm_status) ==
    (R4_HW_COMMIT_PENDING_ARM_STATUS_WORD * sizeof(uint32_t)),
    "R4 completion-pending arm evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, target_window_task_cycles) ==
    (R4_HW_TARGET_WINDOW_TASK_WORD * sizeof(uint32_t)),
    "R4 target-window task evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, response_worker_created) ==
    (R4_HW_RESPONSE_CREATED_WORD * sizeof(uint32_t)),
    "R4 response created evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, response_owner_cycles) ==
    (R4_HW_RESPONSE_OWNER_CYCLES_WORD * sizeof(uint32_t)),
    "R4 response owner evidence word offset changed");
_Static_assert(offsetof(R4HwHarnessResult, mask_timing_reset_status) ==
    (R4_HW_MASK_TIMING_RESET_WORD * sizeof(uint32_t)),
    "R4 mask timing reset evidence word offset changed");

extern volatile R4HwHarnessResult g_r4_hw_result;

void R4_HW_Start(void);

#ifdef __cplusplus
}
#endif

#endif /* R4_HW_HARNESS_H */
