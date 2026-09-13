#ifndef R1_ACQUISITION_H
#define R1_ACQUISITION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define R1_ACQUISITION_BLOCK_SAMPLES 256U
#define R1_ACQUISITION_TRACE_CAPACITY 128U
#define R1_ACQUISITION_EXPECTED_BLOCK_CYCLES 230400U

typedef enum
{
    R1_ACQUISITION_STATE_UNINITIALIZED = 0,
    R1_ACQUISITION_STATE_STOPPED,
    R1_ACQUISITION_STATE_STARTING,
    R1_ACQUISITION_STATE_RUNNING,
    R1_ACQUISITION_STATE_STOPPING,
    R1_ACQUISITION_STATE_ERROR
} R1_AcquisitionState;

typedef enum
{
    R1_ACQUISITION_OK = 0,
    R1_ACQUISITION_BUSY,
    R1_ACQUISITION_INVALID_STATE,
    R1_ACQUISITION_CONFIG_ERROR,
    R1_ACQUISITION_HAL_ERROR
} R1_AcquisitionStatus;

typedef struct
{
    uint32_t sequence;
    uint32_t cyccnt;
    uint32_t dma_lisr;
    uint32_t completed_target;
    uint32_t active_target;
    uint32_t adc_ovr_observed;
} R1_AcquisitionTraceEntry;

typedef struct
{
    uint32_t start_count;
    uint32_t restart_count;
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
    uint32_t previous_tc_cyccnt;
    uint32_t last_delta_cycles;
    uint32_t min_delta_cycles;
    uint32_t max_delta_cycles;
    uint64_t sum_delta_cycles;
    uint32_t trace_count;

    uint32_t start_dma_cr;
    uint32_t start_dma_ndtr;
    uint32_t start_dma_m0ar;
    uint32_t start_dma_m1ar;
    uint32_t start_dma_ct;
    uint32_t start_adc_cr2;
    uint32_t start_tim2_cr1;
    uint32_t partial_stop_count;
    uint32_t stop_remaining_ndtr;
    uint32_t stop_captured_samples;
    uint32_t stop_active_target;
    uint32_t stop_artifact_count;
    R1_AcquisitionState state;
} R1_AcquisitionDiagnostics;

void R1_Acquisition_Init(void);
R1_AcquisitionStatus R1_Acquisition_Start(void);
R1_AcquisitionStatus R1_Acquisition_Stop(void);
void R1_Acquisition_GetDiagnostics(R1_AcquisitionDiagnostics *out);
size_t R1_Acquisition_CopyTrace(R1_AcquisitionTraceEntry *out, size_t capacity);
const uint16_t *R1_Acquisition_GetBuffer(uint32_t target);
void R1_Acquisition_DmaIrqEnter(uint32_t dma2_lisr);

#ifdef __cplusplus
}
#endif

#endif
