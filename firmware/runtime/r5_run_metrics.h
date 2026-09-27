#ifndef R5_RUN_METRICS_H
#define R5_RUN_METRICS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define R5_METRICS_MAX_COHORT_BLOCKS 128U
#define R5_METRICS_HISTOGRAM_BINS 128U

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

typedef struct
{
    uint32_t s0;
    uint32_t s1;
    uint32_t tail_blocks;
    uint64_t epoch_cycles;
    uint64_t block_period_cycles;
    uint64_t deadline_cycles;
    uint64_t time_epsilon_cycles;
} R5MetricsConfig;

typedef struct
{
    uint64_t lower_cycles;
    uint64_t upper_cycles;
    R5MetricsP99Status status;
} R5MetricsP99;

typedef struct
{
    R5MetricsPhase phase;
    R5MetricsStatus outcome_status;
    R5MetricsConfig config;
    uint32_t s2;
    uint32_t next_input_sequence;
    uint64_t last_event_serial;
    uint64_t raw_input_count;
    uint64_t cohort_input_count;
    uint64_t admitted_count;
    uint64_t drop_count;
    uint64_t on_time_count;
    uint64_t late_completed_count;
    uint64_t expired_unresolved_count;
    uint64_t completed_count;
    uint64_t post_cutoff_completion_count;
    uint64_t histogram_overflow_count;
    uint64_t histogram[R5_METRICS_HISTOGRAM_BINS];
    uint64_t window_open_time;
    uint64_t window_close_time;
    uint32_t window_opened;
    uint32_t window_closed;
    uint32_t observation_closed;
    uint32_t cohort_admitted[R5_METRICS_MAX_COHORT_BLOCKS];
    uint32_t cohort_completed[R5_METRICS_MAX_COHORT_BLOCKS];
    R5MetricsP99 p99_completed_by_cutoff;
    R5MetricsP99 p99_all_admitted;
} R5RunMetrics;

R5MetricsStatus R5RunMetrics_Initialize(R5RunMetrics *metrics,
    const R5MetricsConfig *config);

/* Called for every integrity-checked input event in its RuntimeEvent serial
 * order.  S2 closes observation before any ordinary admission decision. */
R5MetricsStatus R5RunMetrics_OnInput(R5RunMetrics *metrics,
    uint32_t sequence, uint64_t irq_time, uint64_t event_serial,
    uint32_t free_available);

/* Called at the successful COMPLETE logical-commit point in the same serial
 * domain as OnInput.  Late arrivals after cutoff cannot alter sealed outcome. */
R5MetricsStatus R5RunMetrics_OnCompletion(R5RunMetrics *metrics,
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
