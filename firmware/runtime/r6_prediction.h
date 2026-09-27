#ifndef R6_PREDICTION_H
#define R6_PREDICTION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define R6_PREDICTION_MAX_BLOCKS 32U

typedef enum
{
    R6_PREDICTION_OK = 0,
    R6_PREDICTION_INVALID_ARGUMENT,
    R6_PREDICTION_INVALID_COST_KEY,
    R6_PREDICTION_INVALID_TIMELINE,
    R6_PREDICTION_CAPACITY_EXCEEDED
} R6PredictionStatus;

/* This is the complete model lookup identity, rather than a short pipeline
 * nickname.  The host records the corresponding immutable hashes in the
 * prediction artifact before a VALIDATION run is started. */
typedef struct
{
    uint64_t pipeline_id;
    uint64_t coefficient_version;
    uint64_t window_version;
    uint64_t feature_set;
    uint64_t datatype;
    uint64_t compile_flags;
    uint64_t library_version;
    uint64_t dac_profile;
    uint64_t instrumentation_profile;
} R6CostKey;

typedef struct
{
    R6CostKey key;
    uint64_t dma_complete_offset_cycles;
    uint64_t irq_admission_offset_cycles;
    uint64_t dsp_work_cycles;
    uint64_t commit_prefix_cycles;
    uint64_t commit_suffix_cycles;
    uint64_t processing_epilogue_cycles;
    uint64_t fir_reset_gap_cycles;
    uint64_t admit_isr_cycles;
    uint64_t drop_isr_cycles;
    uint64_t monitor_platform_cycles;
} R6CostTable;

typedef struct
{
    uint64_t first_nominal_trigger_cycles;
    uint64_t block_period_cycles;
    uint32_t block_count;
    uint32_t initial_free_tokens;
    uint32_t pending_free_count;
    /* FREE events left over from work that predates this model window.  They
     * must be increasing; equal-time FREE is processed before admission. */
    uint64_t pending_free_at_cycles[R6_PREDICTION_MAX_BLOCKS];
} R6PredictionConfig;

typedef struct
{
    uint64_t nominal_trigger_cycles;
    uint64_t dma_complete_cycles;
    uint64_t admission_cycles;
    uint64_t processing_start_cycles;
    uint64_t lock_cycles;
    uint64_t logical_commit_cycles;
    uint64_t unlock_cycles;
    uint64_t processing_available_cycles;
    uint32_t admitted;
} R6PredictedBlock;

typedef struct
{
    R6PredictionStatus status;
    uint32_t admitted_count;
    uint32_t dropped_count;
    uint32_t final_free_tokens;
    uint64_t final_processing_available_cycles;
    R6PredictedBlock blocks[R6_PREDICTION_MAX_BLOCKS];
} R6Prediction;

/* Models frozen tie ordering: all FREE events at a timestamp are visible to
 * the ISR admission decision at that same timestamp.  t_unlock is the only
 * token-return time; the later epilogue only consumes processor availability. */
R6PredictionStatus R6Prediction_Evaluate(const R6CostTable *cost,
    const R6PredictionConfig *config, R6Prediction *prediction);

#ifdef __cplusplus
}
#endif

#endif /* R6_PREDICTION_H */
