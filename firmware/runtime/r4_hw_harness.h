#ifndef R4_HW_HARNESS_H
#define R4_HW_HARNESS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define R4_HW_MAGIC UINT32_C(0x52344857)
#define R4_HW_COMPLETE UINT32_C(0x5234444E)

typedef struct
{
    uint32_t magic;
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
    uint32_t t17_checkpoint_status;
    uint32_t t17_post_close_status;
    uint32_t t17_duplicate_exit_status;
    uint64_t t17_serial_before;
    uint64_t t17_serial_after_pending_irq;
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
} R4HwHarnessResult;

extern volatile R4HwHarnessResult g_r4_hw_result;

void R4_HW_Start(void);

#ifdef __cplusplus
}
#endif

#endif /* R4_HW_HARNESS_H */
