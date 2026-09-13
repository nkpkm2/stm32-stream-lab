#ifndef R1_BRINGUP_H
#define R1_BRINGUP_H

#include <stdint.h>

#include "r1_acquisition.h"

#ifdef __cplusplus
extern "C" {
#endif

#define R1_SOAK_DURATION_MS 600000U

typedef enum
{
    R1_SOAK_PHASE_RESET = 0,
    R1_SOAK_PHASE_TASK_CREATED,
    R1_SOAK_PHASE_INITIALIZED,
    R1_SOAK_PHASE_RUNNING,
    R1_SOAK_PHASE_STOPPING,
    R1_SOAK_PHASE_COMPLETE,
    R1_SOAK_PHASE_FAILED
} R1_SoakPhase;

typedef struct
{
    uint32_t magic;
    uint32_t task_created;
    uint32_t phase;
    uint32_t soak_pass;

    uint32_t start_status;
    uint32_t stop_status;

    uint32_t start_tick;
    uint32_t stop_tick;
    uint32_t elapsed_ticks;
    uint32_t elapsed_ms;

    uint32_t expected_tc_count;
    uint32_t actual_tc_count;
    int32_t tc_count_error;

    uint32_t observed_block_rate_millihz;
    uint32_t expected_block_rate_millihz;

    uint32_t dbm_bit_seen;
    uint32_t start_ndtr;
    uint32_t start_ct;
    uint32_t m0ar_matches_buffer0;
    uint32_t m1ar_matches_buffer1;
    uint32_t adc_dma_bit_seen;
    uint32_t adc_dds_bit_seen;
    uint32_t tim2_running_after_start;

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
    uint32_t mean_delta_error_cycles;

    uint32_t trace_count;

    uint32_t stop_remaining_ndtr;
    uint32_t stop_captured_samples;
    uint32_t stop_active_target;
    uint32_t partial_stop_count;
    uint32_t stop_artifact_count;

    uint32_t quiet_tc_count;
    uint32_t quiet_stop_artifact_count;
    uint32_t quiet_state;

    uint32_t raw0_min;
    uint32_t raw0_max;
    uint32_t raw1_min;
    uint32_t raw1_max;

    uint32_t system_core_clock;
    uint32_t aircr;
    uint32_t hal_tick_start;
    uint32_t hal_tick_stop;
    uint32_t hal_tick_delta;
} R1_SoakResult;

extern volatile R1_SoakResult g_r1_soak_result;

void R1_Bringup_CreateTask(void);

#ifdef __cplusplus
}
#endif

#endif
