#include "r6_prediction.h"

#include <string.h>

static uint32_t KeyIsComplete(const R6CostKey *key)
{
    return (key->pipeline_id != 0U) && (key->coefficient_version != 0U) &&
        (key->window_version != 0U) && (key->feature_set != 0U) &&
        (key->datatype != 0U) && (key->compile_flags != 0U) &&
        (key->library_version != 0U) && (key->dac_profile != 0U) &&
        (key->instrumentation_profile != 0U);
}

static uint64_t Max(uint64_t left, uint64_t right)
{
    return left > right ? left : right;
}

R6PredictionStatus R6Prediction_Evaluate(const R6CostTable *cost,
    const R6PredictionConfig *config, R6Prediction *prediction)
{
    uint32_t index;
    uint64_t processor_available = 0U;
    uint64_t final_free = 0U;

    if ((cost == NULL) || (config == NULL) || (prediction == NULL) ||
        (config->block_count == 0U) ||
        (config->block_count > R6_PREDICTION_MAX_BLOCKS) ||
        (config->pending_free_count > R6_PREDICTION_MAX_BLOCKS) ||
        (config->block_period_cycles == 0U))
    {
        return R6_PREDICTION_INVALID_ARGUMENT;
    }
    (void)memset(prediction, 0, sizeof(*prediction));
    if (KeyIsComplete(&cost->key) == 0U)
    {
        prediction->status = R6_PREDICTION_INVALID_COST_KEY;
        return prediction->status;
    }
    for (index = 1U; index < config->pending_free_count; ++index)
    {
        if (config->pending_free_at_cycles[index] <
            config->pending_free_at_cycles[index - 1U])
        {
            prediction->status = R6_PREDICTION_INVALID_TIMELINE;
            return prediction->status;
        }
    }
    for (index = 0U; index < config->block_count; ++index)
    {
        R6PredictedBlock *block = &prediction->blocks[index];
        uint32_t previous;
        uint64_t available = config->initial_free_tokens;
        uint64_t consumed = 0U;

        block->nominal_trigger_cycles = config->first_nominal_trigger_cycles +
            ((uint64_t)index * config->block_period_cycles);
        block->dma_complete_cycles = block->nominal_trigger_cycles +
            cost->dma_complete_offset_cycles;
        block->admission_cycles = block->dma_complete_cycles +
            cost->irq_admission_offset_cycles;
        /* This <= establishes the frozen FREE-before-admission tie policy. */
        for (previous = 0U; previous < config->pending_free_count; ++previous)
        {
            if (config->pending_free_at_cycles[previous] <= block->admission_cycles)
            {
                ++available;
            }
        }
        for (previous = 0U; previous < index; ++previous)
        {
            if (prediction->blocks[previous].admitted != 0U)
            {
                ++consumed;
                if (prediction->blocks[previous].unlock_cycles <= block->admission_cycles)
                {
                    ++available;
                }
            }
        }
        if (available <= consumed)
        {
            ++prediction->dropped_count;
            final_free = available - consumed;
            continue;
        }
        final_free = available - consumed - UINT64_C(1);
        block->admitted = 1U;
        ++prediction->admitted_count;
        block->processing_start_cycles = Max(block->admission_cycles,
            processor_available);
        block->lock_cycles = block->processing_start_cycles + cost->dsp_work_cycles;
        block->logical_commit_cycles = block->lock_cycles + cost->commit_prefix_cycles;
        block->unlock_cycles = block->logical_commit_cycles + cost->commit_suffix_cycles;
        block->processing_available_cycles = block->unlock_cycles +
            cost->processing_epilogue_cycles;
        processor_available = block->processing_available_cycles;
    }
    /* The snapshot is taken at the last admission point, not after imaginary
     * future work; it therefore cannot claim quiet wall-clock time as CPU. */
    prediction->final_free_tokens = (uint32_t)final_free;
    prediction->final_processing_available_cycles = processor_available;
    prediction->status = R6_PREDICTION_OK;
    return prediction->status;
}
