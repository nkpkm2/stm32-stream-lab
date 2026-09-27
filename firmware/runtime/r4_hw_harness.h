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
#define R4_HW_SYNTHETIC_CREATE_MASK_WORD 122U
#define R4_HW_SYNTHETIC_A_CYCLES_WORD 128U
#define R4_HW_SYNTHETIC_B_CYCLES_WORD 130U
#define R4_HW_SYNTHETIC_WINDOW_TASK_WORD 132U
#define R4_HW_SYNTHETIC_WINDOW_IRQ_WORD 134U
#define R4_HW_SYNTHETIC_WINDOW_IDLE_WORD 136U
#define R4_HW_SYNTHETIC_WINDOW_UNCLASSIFIED_WORD 138U
#define R4_HW_DMA_WINDOW_SNAPSHOT_WORD 140U

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

extern volatile R4HwHarnessResult g_r4_hw_result;

void R4_HW_Start(void);

#ifdef __cplusplus
}
#endif

#endif /* R4_HW_HARNESS_H */
