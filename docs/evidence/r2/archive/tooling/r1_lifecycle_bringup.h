#ifndef R1_BRINGUP_H
#define R1_BRINGUP_H

#include <stdint.h>

#include "r1_acquisition.h"

#ifdef __cplusplus
extern "C" {
#endif

#define R1_LIFECYCLE_CYCLE_COUNT 6U

typedef enum
{
    R1_LIFECYCLE_PHASE_RESET = 0,
    R1_LIFECYCLE_PHASE_TASK_CREATED,
    R1_LIFECYCLE_PHASE_INITIALIZED,
    R1_LIFECYCLE_PHASE_RUNNING,
    R1_LIFECYCLE_PHASE_COMPLETE,
    R1_LIFECYCLE_PHASE_FAILED
} R1_LifecyclePhase;

typedef struct
{
    uint32_t target_run_cycles;

    uint32_t start_status;
    uint32_t stop_status;
    uint32_t tim2_cr1_after_start;

    uint32_t restart_count;

    uint32_t dbm_bit_seen;
    uint32_t start_ndtr;
    uint32_t start_ct;
    uint32_t m0ar_matches_buffer0;
    uint32_t m1ar_matches_buffer1;
    uint32_t adc_dma_bit_seen;
    uint32_t adc_dds_bit_seen;

    uint32_t tc_count;
    uint32_t m0_complete_count;
    uint32_t m1_complete_count;

    uint32_t ct_mismatch_count;
    uint32_t alternation_mismatch_count;
    uint32_t suspected_event_loss_count;

    uint32_t adc_ovr_count;
    uint32_t dma_te_count;
    uint32_t dma_dme_count;
    uint32_t dma_fe_count;
    uint32_t dma_other_error_count;

    uint32_t timing_interval_count;
    uint32_t mean_delta_cycles;
    uint32_t min_delta_cycles;
    uint32_t max_delta_cycles;

    uint32_t trace_count;
    uint32_t first_sequence;
    uint32_t first_completed_target;
    uint32_t second_sequence;
    uint32_t second_completed_target;

    uint32_t partial_stop_count;
    uint32_t stop_remaining_ndtr;
    uint32_t stop_captured_samples;
    uint32_t stop_active_target;
    uint32_t stop_artifact_count;

    uint32_t quiet_tc_count;
    uint32_t quiet_stop_artifact_count;
    uint32_t quiet_state;

    uint32_t cycle_pass;
} R1_LifecycleCycleResult;

typedef struct
{
    uint32_t magic;
    uint32_t task_created;
    uint32_t phase;
    uint32_t completed_cycles;
    uint32_t all_pass;

    R1_LifecycleCycleResult cycles[R1_LIFECYCLE_CYCLE_COUNT];
} R1_LifecycleResult;

extern volatile R1_LifecycleResult g_r1_lifecycle_result;

void R1_Bringup_CreateTask(void);

#ifdef __cplusplus
}
#endif

#endif
