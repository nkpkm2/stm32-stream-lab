#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "r5_run_metrics.h"
#include "r5_result_store.h"

#define CHECK(x) do { if (!(x)) { (void)fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #x); exit(EXIT_FAILURE); } } while (0)

static R5MetricsConfig Config(void)
{
    R5MetricsConfig config;
    config.s0 = 2U;
    config.s1 = 5U;
    config.tail_blocks = 2U;
    config.epoch_cycles = 0U;
    config.block_period_cycles = 100U;
    config.deadline_cycles = 160U;
    config.time_epsilon_cycles = 10U;
    return config;
}

static void Input(R5RunMetrics *metrics, uint32_t seq, uint64_t time,
    uint64_t serial, uint32_t free_available)
{
    CHECK(R5RunMetrics_OnInput(metrics, seq, time, serial, free_available) ==
        R5_METRICS_OK);
}

static void InputQ(R5RunMetrics *metrics, uint32_t seq, uint64_t time,
    uint64_t serial, uint32_t free_available, uint32_t occupancy)
{
    CHECK(R5RunMetrics_OnInputWithOccupancy(metrics, seq, time, serial,
        free_available, occupancy) == R5_METRICS_OK);
}

static void CaseKnownCohort(void)
{
    R5RunMetrics metrics;
    R5RunMetrics snapshot;
    R5MetricsConfig config = Config();

    CHECK(R5RunMetrics_Initialize(&metrics, &config) == R5_METRICS_OK);
    Input(&metrics, 0U, 10U, 1U, 1U);       /* WARMUP */
    Input(&metrics, 1U, 110U, 2U, 1U);      /* WARMUP */
    Input(&metrics, 2U, 210U, 3U, 1U);      /* S0, admit */
    CHECK(R5RunMetrics_OnCompletion(&metrics, 2U, 350U, 4U) == R5_METRICS_OK);
    Input(&metrics, 3U, 310U, 5U, 0U);      /* cohort drop */
    Input(&metrics, 4U, 410U, 6U, 1U);      /* cohort admit */
    CHECK(R5RunMetrics_OnCompletion(&metrics, 4U, 700U, 7U) == R5_METRICS_OK);
    Input(&metrics, 5U, 510U, 8U, 0U);      /* S1 closes CPU window; TAIL */
    Input(&metrics, 6U, 610U, 9U, 1U);      /* TAIL */
    Input(&metrics, 7U, 1000U, 10U, 0U);    /* S2: cutoff only */
    CHECK(R5RunMetrics_Seal(&metrics, 1U, 1U) == R5_METRICS_OK);
    CHECK(R5RunMetrics_GetSnapshot(&metrics, &snapshot) == R5_METRICS_OK);
    CHECK(snapshot.phase == R5_METRICS_SEALED);
    CHECK(snapshot.raw_input_count == 8U);
    CHECK(snapshot.cohort_input_count == 3U);
    CHECK(snapshot.admitted_count == 2U);
    CHECK(snapshot.drop_count == 1U);
    CHECK(snapshot.on_time_count == 1U);
    CHECK(snapshot.late_completed_count == 1U);
    CHECK(snapshot.expired_unresolved_count == 0U);
    CHECK(snapshot.window_open_time == 210U);
    CHECK(snapshot.window_close_time == 510U);
    CHECK(snapshot.p99_completed_by_cutoff.status == R5_METRICS_P99_INTERVAL);
    CHECK(snapshot.p99_all_admitted.status == R5_METRICS_P99_INTERVAL);
    CHECK(R5RunMetrics_OnCompletion(&metrics, 2U, 360U, 11U) ==
        R5_METRICS_INVALID_STATE);
}

static void CaseS2NeverAdmits(void)
{
    R5RunMetrics metrics;
    R5MetricsConfig config = Config();

    config.s0 = 0U;
    config.s1 = 2U;
    CHECK(R5RunMetrics_Initialize(&metrics, &config) == R5_METRICS_OK);
    Input(&metrics, 0U, 1U, 1U, 0U);       /* S0 drop */
    Input(&metrics, 1U, 2U, 2U, 0U);       /* S1 drop and close */
    Input(&metrics, 2U, 3U, 3U, 1U);       /* TAIL, excluded */
    Input(&metrics, 3U, 4U, 4U, 1U);       /* TAIL, excluded */
    Input(&metrics, 4U, 1000U, 5U, 1U);    /* S2 with FREE: still cutoff */
    CHECK(metrics.raw_input_count == 5U);
    CHECK(metrics.cohort_input_count == 2U);
    CHECK(metrics.drop_count == 2U);
    CHECK(metrics.admitted_count == 0U);
    CHECK(metrics.phase == R5_METRICS_OUTCOME_CLOSED);
}

static void CaseS2EmptyNeverDropsAgain(void)
{
    R5RunMetrics metrics;
    R5MetricsConfig config = Config();

    config.s0 = 0U;
    config.s1 = 2U;
    CHECK(R5RunMetrics_Initialize(&metrics, &config) == R5_METRICS_OK);
    Input(&metrics, 0U, 1U, 1U, 0U);
    Input(&metrics, 1U, 2U, 2U, 0U);
    Input(&metrics, 2U, 3U, 3U, 1U);
    Input(&metrics, 3U, 4U, 4U, 1U);
    Input(&metrics, 4U, 1000U, 5U, 0U);    /* S2 with no FREE */
    CHECK(metrics.cohort_input_count == 2U);
    CHECK(metrics.drop_count == 2U);
    CHECK(metrics.admitted_count == 0U);
    CHECK(metrics.raw_input_count == 5U);
}

static void CaseCutoffWins(void)
{
    R5RunMetrics metrics;
    R5MetricsConfig config = Config();

    config.s0 = 0U;
    config.s1 = 1U;
    CHECK(R5RunMetrics_Initialize(&metrics, &config) == R5_METRICS_OK);
    Input(&metrics, 0U, 1U, 1U, 1U);
    Input(&metrics, 1U, 2U, 2U, 1U);
    Input(&metrics, 2U, 3U, 3U, 1U);
    Input(&metrics, 3U, 1000U, 4U, 1U);    /* S2 before complete */
    CHECK(metrics.expired_unresolved_count == 1U);
    CHECK(R5RunMetrics_OnCompletion(&metrics, 0U, 120U, 5U) ==
        R5_METRICS_OBSERVATION_CLOSED);
    CHECK(metrics.expired_unresolved_count == 1U);
    CHECK(metrics.completed_count == 0U);
    CHECK(metrics.post_cutoff_completion_count == 1U);
    CHECK(R5RunMetrics_Seal(&metrics, 1U, 1U) == R5_METRICS_OK);
    CHECK(metrics.p99_all_admitted.status == R5_METRICS_P99_CENSORED);
}

static void CaseInsufficientObservation(void)
{
    R5RunMetrics metrics;
    R5MetricsConfig config = Config();

    config.s0 = 0U;
    config.s1 = 1U;
    CHECK(R5RunMetrics_Initialize(&metrics, &config) == R5_METRICS_OK);
    Input(&metrics, 0U, 1U, 1U, 1U);
    Input(&metrics, 1U, 2U, 2U, 1U);
    Input(&metrics, 2U, 3U, 3U, 1U);
    CHECK(R5RunMetrics_OnInput(&metrics, 3U, 205U, 4U, 1U) ==
        R5_METRICS_INSUFFICIENT_OBSERVATION);
    CHECK(metrics.expired_unresolved_count == 0U);
    CHECK(metrics.outcome_status == R5_METRICS_INSUFFICIENT_OBSERVATION);
    CHECK(R5RunMetrics_Seal(&metrics, 1U, 1U) == R5_METRICS_OK);
    CHECK(metrics.p99_completed_by_cutoff.status == R5_METRICS_P99_NOT_AVAILABLE);
}

static void CaseOverflowAndDuplicate(void)
{
    R5RunMetrics metrics;
    R5MetricsConfig config = Config();

    config.s0 = 0U;
    config.s1 = 1U;
    CHECK(R5RunMetrics_Initialize(&metrics, &config) == R5_METRICS_OK);
    Input(&metrics, 0U, 1U, 1U, 1U);
    CHECK(R5RunMetrics_OnCompletion(&metrics, 0U, 2000U, 2U) == R5_METRICS_OK);
    CHECK(metrics.histogram_overflow_count == 1U);
    CHECK(R5RunMetrics_OnCompletion(&metrics, 0U, 1001U, 3U) ==
        R5_METRICS_DUPLICATE_COMPLETION);
    CHECK(metrics.phase == R5_METRICS_INVALID);
}

static void CaseSealedResultStore(void)
{
    R5RunMetrics metrics;
    R5MetricsConfig config = Config();
    R5ResultStore store;
    const R5RunMetrics *published;

    config.s0 = 0U;
    config.s1 = 1U;
    CHECK(R5RunMetrics_Initialize(&metrics, &config) == R5_METRICS_OK);
    Input(&metrics, 0U, 1U, 1U, 1U);
    CHECK(R5RunMetrics_OnCompletion(&metrics, 0U, 150U, 2U) == R5_METRICS_OK);
    Input(&metrics, 1U, 2U, 3U, 1U);
    Input(&metrics, 2U, 3U, 4U, 1U);
    Input(&metrics, 3U, 1000U, 5U, 1U);
    CHECK(R5RunMetrics_Seal(&metrics, 1U, 1U) == R5_METRICS_OK);
    R5ResultStore_Initialize(&store);
    CHECK(R5ResultStore_Seal(&store, 99U, &metrics) == R5_RESULT_STORE_OK);
    CHECK(R5ResultStore_Acquire(&store, 99U, &published) == R5_RESULT_STORE_OK);
    CHECK(published->phase == R5_METRICS_SEALED);
    CHECK(R5ResultStore_CanBeginRun(&store) == R5_RESULT_STORE_BUSY);
    CHECK(R5ResultStore_Release(&store, 99U) == R5_RESULT_STORE_OK);
    CHECK(R5ResultStore_CanBeginRun(&store) == R5_RESULT_STORE_OK);
}

static void CaseCompletionWinsBeforeCutoff(void)
{
    R5RunMetrics metrics;
    R5MetricsConfig config = Config();

    config.s0 = 0U;
    config.s1 = 1U;
    CHECK(R5RunMetrics_Initialize(&metrics, &config) == R5_METRICS_OK);
    InputQ(&metrics, 0U, 1U, 1U, 1U, 4U);
    InputQ(&metrics, 1U, 2U, 2U, 1U, 5U);
    InputQ(&metrics, 2U, 3U, 3U, 1U, 6U);
    /* Same timestamp would be immaterial: serial 4 commits first. */
    CHECK(R5RunMetrics_OnCompletion(&metrics, 0U, 100U, 4U) == R5_METRICS_OK);
    InputQ(&metrics, 3U, 1000U, 5U, 1U, 99U);
    CHECK(metrics.cohort_outcome[0] == R5_METRICS_OUTCOME_ON_TIME);
    CHECK(metrics.expired_unresolved_count == 0U);
    CHECK(metrics.occupancy_sample_count == 3U);
    CHECK(metrics.occupancy_histogram[4U] == 1U);
    CHECK(metrics.occupancy_histogram[6U] == 1U);
    CHECK(metrics.occupancy_histogram[99U] == 0U); /* S2 has no Q sample. */
    CHECK(metrics.occupancy_histogram[0U] == 0U);
    CHECK(R5RunMetrics_Seal(&metrics, 1U, 1U) == R5_METRICS_OK);
}

static void CaseExplicitOutcomesAndExact8D(void)
{
    R5RunMetrics metrics;
    R5MetricsConfig config = Config();

    config.s0 = 0U;
    config.s1 = 3U;
    CHECK(R5RunMetrics_Initialize(&metrics, &config) == R5_METRICS_OK);
    Input(&metrics, 0U, 1U, 1U, 0U);
    Input(&metrics, 1U, 2U, 2U, 1U);
    CHECK(R5RunMetrics_OnCompletion(&metrics, 1U, 1480U, 3U) == R5_METRICS_OK);
    Input(&metrics, 2U, 4U, 4U, 1U);
    Input(&metrics, 3U, 5U, 5U, 1U);
    Input(&metrics, 4U, 6U, 6U, 1U);
    Input(&metrics, 5U, 1000U, 7U, 1U);
    CHECK(metrics.cohort_outcome[0] == R5_METRICS_OUTCOME_CAPACITY_DROP);
    CHECK(metrics.cohort_outcome[1] == R5_METRICS_OUTCOME_LATE);
    CHECK(metrics.cohort_outcome[2] == R5_METRICS_OUTCOME_EXPIRED_UNRESOLVED);
    CHECK(metrics.histogram_overflow_count == 1U); /* exact 8D: 1280 cycles */
    CHECK(R5RunMetrics_Seal(&metrics, 1U, 1U) == R5_METRICS_OK);
    CHECK(metrics.p99_all_admitted.status == R5_METRICS_P99_CENSORED);
}

int main(int argc, char **argv)
{
    if (argc != 2) return EXIT_FAILURE;
    if (strcmp(argv[1], "known") == 0) CaseKnownCohort();
    else if (strcmp(argv[1], "s2") == 0) CaseS2NeverAdmits();
    else if (strcmp(argv[1], "s2_empty") == 0) CaseS2EmptyNeverDropsAgain();
    else if (strcmp(argv[1], "cutoff") == 0) CaseCutoffWins();
    else if (strcmp(argv[1], "insufficient") == 0) CaseInsufficientObservation();
    else if (strcmp(argv[1], "overflow") == 0) CaseOverflowAndDuplicate();
    else if (strcmp(argv[1], "store") == 0) CaseSealedResultStore();
    else if (strcmp(argv[1], "completion_wins") == 0) CaseCompletionWinsBeforeCutoff();
    else if (strcmp(argv[1], "outcomes") == 0) CaseExplicitOutcomesAndExact8D();
    else return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
