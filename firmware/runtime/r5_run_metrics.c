#include "r5_run_metrics.h"

#include <string.h>

static R5MetricsStatus Fail(R5RunMetrics *metrics, R5MetricsStatus status)
{
    if (metrics != NULL)
    {
        metrics->phase = R5_METRICS_INVALID;
        metrics->outcome_status = status;
    }
    return status;
}

static uint64_t Nominal(const R5RunMetrics *metrics, uint32_t sequence)
{
    return metrics->config.epoch_cycles +
        ((uint64_t)sequence + UINT64_C(1)) * metrics->config.block_period_cycles;
}

static R5MetricsStatus CheckEvent(R5RunMetrics *metrics, uint64_t event_serial)
{
    if ((metrics == NULL) || (metrics->phase != R5_METRICS_OPEN))
    {
        return R5_METRICS_INVALID_STATE;
    }
    if (event_serial <= metrics->last_event_serial)
    {
        return Fail(metrics, R5_METRICS_SEQUENCE_ERROR);
    }
    metrics->last_event_serial = event_serial;
    return R5_METRICS_OK;
}

static void RecordLatency(R5RunMetrics *metrics, uint64_t latency)
{
    uint64_t bin_width = metrics->config.deadline_cycles / UINT64_C(16);
    uint64_t bin;

    if (latency >= metrics->config.deadline_cycles * UINT64_C(8))
    {
        ++metrics->histogram_overflow_count;
        return;
    }
    bin = latency / bin_width;
    if (bin >= R5_METRICS_HISTOGRAM_BINS)
    {
        ++metrics->histogram_overflow_count;
    }
    else
    {
        ++metrics->histogram[bin];
    }
}

static void RecordOccupancy(R5RunMetrics *metrics, uint32_t occupancy)
{
    if (occupancy == R5_METRICS_OCCUPANCY_UNKNOWN)
    {
        ++metrics->occupancy_unknown_count;
        return;
    }
    ++metrics->occupancy_sample_count;
    if (occupancy >= R5_METRICS_OCCUPANCY_BINS)
    {
        ++metrics->occupancy_overflow_count;
        return;
    }
    ++metrics->occupancy_histogram[occupancy];
}

static R5MetricsP99 ComputeP99(const R5RunMetrics *metrics, uint32_t censored)
{
    R5MetricsP99 result;
    uint64_t rank;
    uint64_t cumulative = 0U;
    uint32_t index;
    uint64_t bin_width = metrics->config.deadline_cycles / UINT64_C(16);

    result.lower_cycles = 0U;
    result.upper_cycles = 0U;
    result.status = R5_METRICS_P99_NOT_AVAILABLE;
    if (censored != 0U)
    {
        result.status = R5_METRICS_P99_CENSORED;
        return result;
    }
    if (metrics->completed_count == 0U)
    {
        return result;
    }
    rank = ((metrics->completed_count * UINT64_C(99)) + UINT64_C(99)) /
        UINT64_C(100);
    for (index = 0U; index < R5_METRICS_HISTOGRAM_BINS; ++index)
    {
        cumulative += metrics->histogram[index];
        if (cumulative >= rank)
        {
            result.lower_cycles = (uint64_t)index * bin_width;
            result.upper_cycles = result.lower_cycles + bin_width;
            result.status = R5_METRICS_P99_INTERVAL;
            return result;
        }
    }
    result.status = R5_METRICS_P99_OUT_OF_RANGE;
    return result;
}

static uint32_t OutcomesAreConserved(const R5RunMetrics *metrics)
{
    uint32_t index;
    uint32_t cohort_count = metrics->config.s1 - metrics->config.s0;
    uint64_t drops = 0U;
    uint64_t on_time = 0U;
    uint64_t late = 0U;
    uint64_t unresolved = 0U;

    for (index = 0U; index < cohort_count; ++index)
    {
        switch (metrics->cohort_outcome[index])
        {
        case R5_METRICS_OUTCOME_CAPACITY_DROP: ++drops; break;
        case R5_METRICS_OUTCOME_ON_TIME: ++on_time; break;
        case R5_METRICS_OUTCOME_LATE: ++late; break;
        case R5_METRICS_OUTCOME_EXPIRED_UNRESOLVED: ++unresolved; break;
        default: return 0U;
        }
    }
    return ((drops == metrics->drop_count) &&
        (on_time == metrics->on_time_count) &&
        (late == metrics->late_completed_count) &&
        (unresolved == metrics->expired_unresolved_count)) ? 1U : 0U;
}

R5MetricsStatus R5RunMetrics_Initialize(R5RunMetrics *metrics,
    const R5MetricsConfig *config)
{
    uint64_t s2;

    if ((metrics == NULL) || (config == NULL) || (config->s0 >= config->s1) ||
        (config->tail_blocks < 2U) ||
        ((config->s1 - config->s0) > R5_METRICS_MAX_COHORT_BLOCKS) ||
        (config->block_period_cycles == 0U) ||
        (config->deadline_cycles == 0U) ||
        ((config->deadline_cycles % UINT64_C(16)) != 0U))
    {
        return R5_METRICS_INVALID_ARGUMENT;
    }
    s2 = (uint64_t)config->s1 + config->tail_blocks;
    if (s2 > UINT32_MAX)
    {
        return R5_METRICS_INVALID_ARGUMENT;
    }
    (void)memset(metrics, 0, sizeof(*metrics));
    metrics->schema_version = R5_METRICS_SCHEMA_VERSION;
    metrics->phase = R5_METRICS_OPEN;
    metrics->outcome_status = R5_METRICS_OK;
    metrics->config = *config;
    metrics->s2 = (uint32_t)s2;
    return R5_METRICS_OK;
}

R5MetricsStatus R5RunMetrics_OnInputWithOccupancy(R5RunMetrics *metrics,
    uint32_t sequence, uint64_t irq_time, uint64_t event_serial,
    uint32_t free_available, uint32_t observed_occupancy)
{
    uint32_t cohort_index;
    uint64_t latest_deadline;
    R5MetricsStatus status = CheckEvent(metrics, event_serial);

    if (status != R5_METRICS_OK) return status;
    if (sequence != metrics->next_input_sequence)
    {
        return Fail(metrics, R5_METRICS_SEQUENCE_ERROR);
    }
    ++metrics->next_input_sequence;
    ++metrics->raw_input_count;
    if (sequence == metrics->config.s0)
    {
        metrics->window_opened = 1U;
        metrics->window_open_time = irq_time;
    }
    if (sequence == metrics->config.s1)
    {
        metrics->window_closed = 1U;
        metrics->window_close_time = irq_time;
    }
    if (sequence == metrics->s2)
    {
        latest_deadline = Nominal(metrics, metrics->config.s1 - 1U) +
            metrics->config.deadline_cycles + metrics->config.time_epsilon_cycles;
        metrics->observation_closed = 1U;
        metrics->phase = R5_METRICS_OUTCOME_CLOSED;
        if (irq_time <= latest_deadline)
        {
            metrics->outcome_status = R5_METRICS_INSUFFICIENT_OBSERVATION;
            return R5_METRICS_INSUFFICIENT_OBSERVATION;
        }
        for (cohort_index = 0U;
             cohort_index < (metrics->config.s1 - metrics->config.s0);
             ++cohort_index)
        {
            if ((metrics->cohort_admitted[cohort_index] != 0U) &&
                (metrics->cohort_completed[cohort_index] == 0U))
            {
                ++metrics->expired_unresolved_count;
                metrics->cohort_outcome[cohort_index] =
                    R5_METRICS_OUTCOME_EXPIRED_UNRESOLVED;
            }
        }
        return R5_METRICS_OK;
    }
    if (sequence > metrics->s2)
    {
        return Fail(metrics, R5_METRICS_SEQUENCE_ERROR);
    }
    /* Every ordinary admission attempt contributes one arrival-observed Q.
     * S2 returned above and therefore contributes none. */
    RecordOccupancy(metrics, observed_occupancy);
    if ((sequence >= metrics->config.s0) && (sequence < metrics->config.s1))
    {
        cohort_index = sequence - metrics->config.s0;
        ++metrics->cohort_input_count;
        if (free_available == 0U)
        {
            ++metrics->drop_count;
            metrics->cohort_outcome[cohort_index] =
                R5_METRICS_OUTCOME_CAPACITY_DROP;
        }
        else
        {
            metrics->cohort_admitted[cohort_index] = 1U;
            metrics->cohort_outcome[cohort_index] =
                R5_METRICS_OUTCOME_ADMITTED_PENDING;
            ++metrics->admitted_count;
        }
    }
    return R5_METRICS_OK;
}

R5MetricsStatus R5RunMetrics_OnInput(R5RunMetrics *metrics,
    uint32_t sequence, uint64_t irq_time, uint64_t event_serial,
    uint32_t free_available)
{
    return R5RunMetrics_OnInputWithOccupancy(metrics, sequence, irq_time,
        event_serial, free_available, R5_METRICS_OCCUPANCY_UNKNOWN);
}

R5MetricsStatus R5RunMetrics_OnCompletion(R5RunMetrics *metrics,
    uint32_t sequence, uint64_t commit_time, uint64_t event_serial)
{
    uint32_t cohort_index;
    uint64_t nominal;
    uint64_t latency;
    R5MetricsStatus status;

    if ((metrics == NULL) || (metrics->phase == R5_METRICS_INVALID) ||
        (metrics->phase == R5_METRICS_SEALED)) return R5_METRICS_INVALID_STATE;
    if (metrics->phase == R5_METRICS_OUTCOME_CLOSED)
    {
        /* Live diagnostic only: terminal outcomes are already immutable. */
        ++metrics->post_cutoff_completion_count;
        ++metrics->live_post_cutoff_completion_count;
        if (event_serial <= metrics->last_event_serial)
        {
            ++metrics->live_order_fault_count;
        }
        return R5_METRICS_OBSERVATION_CLOSED;
    }
    status = CheckEvent(metrics, event_serial);
    if (status != R5_METRICS_OK) return status;
    if ((sequence < metrics->config.s0) || (sequence >= metrics->config.s1))
    {
        return R5_METRICS_NOT_ADMITTED;
    }
    cohort_index = sequence - metrics->config.s0;
    if (metrics->cohort_admitted[cohort_index] == 0U)
    {
        return R5_METRICS_NOT_ADMITTED;
    }
    if (metrics->cohort_completed[cohort_index] != 0U)
    {
        return Fail(metrics, R5_METRICS_DUPLICATE_COMPLETION);
    }
    nominal = Nominal(metrics, sequence);
    if (commit_time < nominal)
    {
        return Fail(metrics, R5_METRICS_TIME_ERROR);
    }
    latency = commit_time - nominal;
    metrics->cohort_completed[cohort_index] = 1U;
    ++metrics->completed_count;
    if (latency <= metrics->config.deadline_cycles)
    {
        ++metrics->on_time_count;
        metrics->cohort_outcome[cohort_index] = R5_METRICS_OUTCOME_ON_TIME;
    }
    else
    {
        ++metrics->late_completed_count;
        metrics->cohort_outcome[cohort_index] = R5_METRICS_OUTCOME_LATE;
    }
    RecordLatency(metrics, latency);
    return R5_METRICS_OK;
}

R5MetricsStatus R5RunMetrics_Seal(R5RunMetrics *metrics,
    uint32_t integrity_ok, uint32_t stop_ok)
{
    if ((metrics == NULL) || (metrics->phase != R5_METRICS_OUTCOME_CLOSED))
    {
        return R5_METRICS_INVALID_STATE;
    }
    if ((integrity_ok == 0U) || (stop_ok == 0U))
    {
        return Fail(metrics, R5_METRICS_INTEGRITY_ERROR);
    }
    if ((metrics->window_opened == 0U) || (metrics->window_closed == 0U) ||
        (metrics->window_close_time < metrics->window_open_time))
    {
        return Fail(metrics, R5_METRICS_INTEGRITY_ERROR);
    }
    if ((metrics->outcome_status == R5_METRICS_OK) &&
        ((metrics->cohort_input_count != (metrics->drop_count +
            metrics->on_time_count + metrics->late_completed_count +
            metrics->expired_unresolved_count)) ||
         (metrics->completed_count != (metrics->on_time_count +
            metrics->late_completed_count)) ||
         (OutcomesAreConserved(metrics) == 0U)))
    {
        return Fail(metrics, R5_METRICS_INTEGRITY_ERROR);
    }
    if (metrics->outcome_status == R5_METRICS_OK)
    {
        metrics->p99_completed_by_cutoff = ComputeP99(metrics, 0U);
        metrics->p99_all_admitted = ComputeP99(metrics,
            metrics->expired_unresolved_count != 0U ? 1U : 0U);
    }
    else
    {
        metrics->p99_completed_by_cutoff.status = R5_METRICS_P99_NOT_AVAILABLE;
        metrics->p99_all_admitted.status = R5_METRICS_P99_NOT_AVAILABLE;
    }
    metrics->phase = R5_METRICS_SEALED;
    return R5_METRICS_OK;
}

R5MetricsStatus R5RunMetrics_GetSnapshot(const R5RunMetrics *metrics,
    R5RunMetrics *out)
{
    if ((metrics == NULL) || (out == NULL)) return R5_METRICS_INVALID_ARGUMENT;
    *out = *metrics;
    return R5_METRICS_OK;
}
