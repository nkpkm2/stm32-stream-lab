#ifndef R5_RUN_METRICS_H
#define R5_RUN_METRICS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define R5_METRICS_MAX_COHORT_BLOCKS 128U
#define R5_METRICS_HISTOGRAM_BINS 128U
#define R5_METRICS_OCCUPANCY_BINS 128U
#define R5_METRICS_DEFAULT_TAIL_BLOCKS 8U
#define R5_METRICS_SCHEMA_VERSION 1U
#define R5_METRICS_OCCUPANCY_UNKNOWN UINT32_MAX

typedef enum
{
    R5_METRICS_OK = 0,
    R5_METRICS_INVALID_ARGUMENT,
    R5_METRICS_INVALID_STATE,
    R5_METRICS_SEQUENCE_ERROR,
    R5_METRICS_TIME_ERROR,
    R5_METRICS_DUPLICATE_COMPLETION,
    R5_METRICS_NOT_ADMITTED,
    R5_METRICS_OBSERVATION_CLOSED,
    R5_METRICS_INSUFFICIENT_OBSERVATION,
    R5_METRICS_INTEGRITY_ERROR
} R5MetricsStatus;

typedef enum
{
    R5_METRICS_OPEN = 0,
    R5_METRICS_OUTCOME_CLOSED,
    R5_METRICS_SEALED,
    R5_METRICS_INVALID
} R5MetricsPhase;

typedef enum
{
    R5_METRICS_P99_NOT_AVAILABLE = 0,
    R5_METRICS_P99_INTERVAL,
    R5_METRICS_P99_OUT_OF_RANGE,
    R5_METRICS_P99_CENSORED
} R5MetricsP99Status;

typedef enum
{
    R5_METRICS_RATE_NOT_AVAILABLE = 0,
    R5_METRICS_RATE_RATIO
} R5MetricsRateStatus;

/* A primary slot changes monotonically from NONE to a terminal outcome, with
 * ADMITTED_PENDING as the only non-terminal state.  It lets conservation be
 * audited directly instead of inferred from unrelated counters. */
typedef enum
{
    R5_METRICS_OUTCOME_NONE = 0,
    R5_METRICS_OUTCOME_CAPACITY_DROP,
    R5_METRICS_OUTCOME_ADMITTED_PENDING,
    R5_METRICS_OUTCOME_ON_TIME,
    R5_METRICS_OUTCOME_LATE,
    R5_METRICS_OUTCOME_EXPIRED_UNRESOLVED
} R5MetricsOutcome;

typedef struct
{
    uint32_t boot_id;
    uint32_t run_id;
    uint32_t generation;
    uint32_t s0;
    uint32_t s1;
    uint32_t tail_blocks;
    uint64_t epoch_cycles;
    uint64_t block_period_cycles;
    uint64_t deadline_cycles;
    uint64_t time_epsilon_cycles;
} R5MetricsConfig;

/* This object is deliberately not embedded in R5RunMetrics and is never
 * copied into R5ResultStore.  It may continue to evolve after outcome closure
 * without mutating the formal sealed result. */
typedef struct
{
    uint64_t post_cutoff_completion_count;
    uint64_t order_fault_count;
} R5LiveDiagnostics;

typedef struct
{
    uint64_t lower_cycles;
    uint64_t upper_cycles;
    R5MetricsP99Status status;
} R5MetricsP99;

typedef struct
{
    uint64_t numerator;
    uint64_t denominator;
    R5MetricsRateStatus status;
} R5MetricsRate;

typedef struct
{
    uint32_t schema_version;
    R5MetricsPhase phase;
    R5MetricsStatus outcome_status;
    R5MetricsConfig config;
    uint32_t s2;
    uint32_t next_input_sequence;
    uint64_t last_event_serial;
    uint64_t raw_input_count;
    uint64_t ordinary_admission_attempt_count;
    uint64_t ordinary_admitted_count;
    uint64_t ordinary_drop_count;
    uint64_t cohort_input_count;
    uint64_t admitted_count;
    uint64_t drop_count;
    uint64_t on_time_count;
    uint64_t late_completed_count;
    uint64_t expired_unresolved_count;
    uint64_t completed_count;
    uint64_t histogram_overflow_count;
    uint64_t histogram[R5_METRICS_HISTOGRAM_BINS];
    uint64_t occupancy_sample_count;
    uint64_t occupancy_unknown_count;
    uint64_t occupancy_overflow_count;
    uint64_t occupancy_histogram[R5_METRICS_OCCUPANCY_BINS];
    uint64_t window_open_time;
    uint64_t window_close_time;
    uint32_t window_opened;
    uint32_t window_closed;
    uint32_t observation_closed;
    uint32_t cohort_admitted[R5_METRICS_MAX_COHORT_BLOCKS];
    uint32_t cohort_completed[R5_METRICS_MAX_COHORT_BLOCKS];
    R5MetricsOutcome cohort_outcome[R5_METRICS_MAX_COHORT_BLOCKS];
    R5MetricsP99 p99_completed_by_cutoff;
    R5MetricsP99 p99_all_admitted;
    R5MetricsRate capacity_drop_rate;
    R5MetricsRate on_time_rate;
    R5MetricsRate late_rate;
    R5MetricsRate unresolved_rate;
    R5MetricsRate completion_rate;
    R5MetricsRate deadline_failure_rate_admitted;
} R5RunMetrics;

R5MetricsStatus R5RunMetrics_Initialize(R5RunMetrics *metrics,
    const R5MetricsConfig *config);

/* Called for every integrity-checked input event in its RuntimeEvent serial
 * order.  S2 closes observation before any ordinary admission decision. */
R5MetricsStatus R5RunMetrics_OnInput(R5RunMetrics *metrics,
    uint32_t sequence, uint64_t irq_time, uint64_t event_serial,
    uint32_t free_available);

/* Preferred production entry point.  observed_occupancy is Q immediately
 * before the ordinary admission decision; pass OCCUPANCY_UNKNOWN only for a
 * legacy/synthetic caller that cannot observe Q.  S2 never records a sample. */
R5MetricsStatus R5RunMetrics_OnInputWithOccupancy(R5RunMetrics *metrics,
    uint32_t sequence, uint64_t irq_time, uint64_t event_serial,
    uint32_t free_available, uint32_t observed_occupancy);

/* Called at the successful COMPLETE logical-commit point in the same serial
 * domain as OnInput.  Late arrivals after cutoff cannot alter sealed outcome. */
R5MetricsStatus R5RunMetrics_OnCompletion(R5RunMetrics *metrics,
    uint32_t sequence, uint64_t commit_time, uint64_t event_serial);

/* The diagnostic object is optional.  When non-NULL it receives permitted
 * post-cutoff observations, while R5RunMetrics remains frozen. */
R5MetricsStatus R5RunMetrics_OnCompletionWithDiagnostics(
    R5RunMetrics *metrics, R5LiveDiagnostics *diagnostics,
    uint32_t sequence, uint64_t commit_time, uint64_t event_serial);

/* Freezes outcomes after worker quiescence/integrity checking.  A sealed
 * instance is immutable and can safely be copied by a result transport. */
R5MetricsStatus R5RunMetrics_Seal(R5RunMetrics *metrics,
    uint32_t integrity_ok, uint32_t stop_ok);
R5MetricsStatus R5RunMetrics_GetSnapshot(const R5RunMetrics *metrics,
    R5RunMetrics *out);

#ifdef __cplusplus
}
#endif

#endif /* R5_RUN_METRICS_H */
