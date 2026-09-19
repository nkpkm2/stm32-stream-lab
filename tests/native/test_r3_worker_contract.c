#include "r3_worker_contract.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { \
    if (!(x)) { \
        (void)fprintf(stderr, "CHECK failed: %s (%s:%d)\n", \
            #x, __FILE__, __LINE__); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static R3WorkerRunKey Run(uint32_t b, uint32_t r, uint32_t g)
{
    R3WorkerRunKey x;
    x.boot_id = b;
    x.run_id = r;
    x.generation = g;
    return x;
}

static R3WorkerStopContext Stop(
    R3WorkerRunKey run,
    uint32_t stop_id)
{
    R3WorkerStopContext s;
    s.run = run;
    s.stop_id = stop_id;
    return s;
}

static R3WorkerContract Worker(R3WorkerId id)
{
    R3WorkerContract w;
    CHECK(R3WorkerContract_Initialize(&w, id) == R3_WORKER_OK);
    return w;
}

static void Bind(
    R3WorkerContract *w,
    R3WorkerRunKey run)
{
    CHECK(R3WorkerContract_BindRun(w, &run) == R3_WORKER_OK);
}

static void LatchStop(
    R3WorkerContract *w,
    R3WorkerStopContext stop)
{
    CHECK(R3WorkerContract_RequestStop(w, &stop) == R3_WORKER_OK);
}

static void CaseInit(void)
{
    R3WorkerContract w = Worker(R3_WORKER_PROCESSING);
    R3WorkerContractSnapshot s;
    CHECK(R3WorkerContract_GetSnapshot(&w, &s) == R3_WORKER_OK);
    CHECK(s.phase == R3_WORKER_PHASE_UNBOUND);
    CHECK(s.worker_id == R3_WORKER_PROCESSING);
}

static void CaseBindIdempotent(void)
{
    R3WorkerContract w = Worker(R3_WORKER_PROCESSING);
    R3WorkerRunKey run = Run(1U, 2U, 3U);
    Bind(&w, run);
    CHECK(R3WorkerContract_BindRun(&w, &run) == R3_WORKER_OK);
    CHECK(w.bind_count == 1U);
}

static void CaseBindDifferentWhileActiveRejected(void)
{
    R3WorkerContract w = Worker(R3_WORKER_PROCESSING);
    R3WorkerRunKey a = Run(1U, 2U, 3U);
    R3WorkerRunKey b = Run(1U, 3U, 4U);
    Bind(&w, a);
    CHECK(R3WorkerContract_BindRun(&w, &b) ==
        R3_WORKER_INVALID_STATE);
    CHECK(w.reject_count == 1U);
}

static void CaseStaleStopRejected(void)
{
    R3WorkerContract w = Worker(R3_WORKER_PROCESSING);
    R3WorkerRunKey run = Run(1U, 2U, 3U);
    R3WorkerStopContext stale = Stop(Run(1U, 2U, 4U), 9U);
    Bind(&w, run);
    CHECK(R3WorkerContract_RequestStop(&w, &stale) ==
        R3_WORKER_STALE_RUN);
    CHECK(w.phase == R3_WORKER_PHASE_RUN_BOUND);
}

static void CaseDuplicateStopIdempotent(void)
{
    R3WorkerContract w = Worker(R3_WORKER_PROCESSING);
    R3WorkerRunKey run = Run(1U, 2U, 3U);
    R3WorkerStopContext stop = Stop(run, 9U);
    Bind(&w, run);
    LatchStop(&w, stop);
    CHECK(R3WorkerContract_RequestStop(&w, &stop) == R3_WORKER_OK);
    CHECK(w.stop_latch_count == 1U);
}

static void CaseConflictingStopRejected(void)
{
    R3WorkerContract w = Worker(R3_WORKER_PROCESSING);
    R3WorkerRunKey run = Run(1U, 2U, 3U);
    Bind(&w, run);
    LatchStop(&w, Stop(run, 9U));
    {
        R3WorkerStopContext other = Stop(run, 10U);
        CHECK(R3WorkerContract_RequestStop(&w, &other) ==
            R3_WORKER_STOP_CONFLICT);
    }
    CHECK(w.stop_id == 9U);
}

static void CaseWakeStopPrecedence(void)
{
    CHECK(R3WorkerContract_SelectWake(
        R3_WORKER_WAKE_WORK | R3_WORKER_WAKE_STOP |
        R3_WORKER_WAKE_START) == R3_WORKER_WAKE_STOP_SELECTED);
    CHECK(R3WorkerContract_SelectWake(
        R3_WORKER_WAKE_WORK | R3_WORKER_WAKE_START) ==
        R3_WORKER_WAKE_START_SELECTED);
}

static void CaseNewWorkClosesOnStop(void)
{
    R3WorkerContract w = Worker(R3_WORKER_PROCESSING);
    R3WorkerRunKey run = Run(1U, 2U, 3U);
    Bind(&w, run);
    CHECK(R3WorkerContract_NewWorkAllowed(&w, 1U) == 1U);
    LatchStop(&w, Stop(run, 9U));
    CHECK(R3WorkerContract_NewWorkAllowed(&w, 1U) == 0U);
}

static void CaseProcessingEmptyStopAck(void)
{
    R3WorkerContract w = Worker(R3_WORKER_PROCESSING);
    R3WorkerRunKey run = Run(1U, 2U, 3U);
    Bind(&w, run);
    LatchStop(&w, Stop(run, 9U));
    CHECK(R3ProcessingWorker_StopAction(
        &w, R3_PROCESSING_RESOURCE_NONE, 0U) ==
        R3_PROCESSING_STOP_EMIT_ACK);
}

static void CaseProcessingHeldCancel(void)
{
    R3WorkerContract w = Worker(R3_WORKER_PROCESSING);
    R3WorkerRunKey run = Run(1U, 2U, 3U);
    Bind(&w, run);
    LatchStop(&w, Stop(run, 9U));
    CHECK(R3ProcessingWorker_StopAction(
        &w, R3_PROCESSING_RESOURCE_HELD_READY, 0U) ==
        R3_PROCESSING_STOP_CANCEL_HELD);
}

static void CaseProcessingCurrentFinish(void)
{
    R3WorkerContract w = Worker(R3_WORKER_PROCESSING);
    R3WorkerRunKey run = Run(1U, 2U, 3U);
    Bind(&w, run);
    LatchStop(&w, Stop(run, 9U));
    CHECK(R3ProcessingWorker_StopAction(
        &w, R3_PROCESSING_RESOURCE_PROCESSING, 0U) ==
        R3_PROCESSING_STOP_FINISH_CURRENT);
}

static void CaseProcessingDrainBacklog(void)
{
    R3WorkerContract w = Worker(R3_WORKER_PROCESSING);
    R3WorkerRunKey run = Run(1U, 2U, 3U);
    Bind(&w, run);
    LatchStop(&w, Stop(run, 9U));
    CHECK(R3ProcessingWorker_StopAction(
        &w, R3_PROCESSING_RESOURCE_NONE, 1U) ==
        R3_PROCESSING_STOP_DRAIN_ONE_READY);
}

static void CaseInterferencePendingCancel(void)
{
    R3WorkerContract w = Worker(R3_WORKER_INTERFERENCE);
    R3WorkerRunKey run = Run(1U, 2U, 3U);
    Bind(&w, run);
    LatchStop(&w, Stop(run, 9U));
    CHECK(R3InterferenceWorker_StopAction(
        &w, R3_INTERFERENCE_ACTIVITY_PENDING) ==
        R3_INTERFERENCE_STOP_CANCEL_PENDING);
}

static void CaseInterferenceRunningFinish(void)
{
    R3WorkerContract w = Worker(R3_WORKER_INTERFERENCE);
    R3WorkerRunKey run = Run(1U, 2U, 3U);
    Bind(&w, run);
    LatchStop(&w, Stop(run, 9U));
    CHECK(R3InterferenceWorker_StopAction(
        &w, R3_INTERFERENCE_ACTIVITY_RUNNING) ==
        R3_INTERFERENCE_STOP_FINISH_SEGMENT);
}

static void CaseInterferenceIdleAck(void)
{
    R3WorkerContract w = Worker(R3_WORKER_INTERFERENCE);
    R3WorkerRunKey run = Run(1U, 2U, 3U);
    Bind(&w, run);
    LatchStop(&w, Stop(run, 9U));
    CHECK(R3InterferenceWorker_StopAction(
        &w, R3_INTERFERENCE_ACTIVITY_NONE) ==
        R3_INTERFERENCE_STOP_EMIT_ACK);
}

static void CaseAckRequiresReleased(void)
{
    R3WorkerContract w = Worker(R3_WORKER_PROCESSING);
    R3WorkerRunKey run = Run(1U, 2U, 3U);
    R3WorkerQuiescedAck ack;
    Bind(&w, run);
    LatchStop(&w, Stop(run, 9U));
    CHECK(R3WorkerContract_BuildQuiescedAck(
        &w, 0U, &ack) == R3_WORKER_NOT_READY);
    CHECK(w.phase == R3_WORKER_PHASE_STOP_PENDING);
}

static void CaseAckExactIdentity(void)
{
    R3WorkerContract w = Worker(R3_WORKER_PROCESSING);
    R3WorkerRunKey run = Run(11U, 22U, 33U);
    R3WorkerStopContext stop = Stop(run, 44U);
    R3WorkerQuiescedAck ack;
    Bind(&w, run);
    LatchStop(&w, stop);
    CHECK(R3WorkerContract_BuildQuiescedAck(
        &w, 1U, &ack) == R3_WORKER_OK);
    CHECK(R3WorkerAck_MatchesStop(
        &ack, &stop, R3_WORKER_PROCESSING) == 1U);
    CHECK(R3WorkerAck_MatchesStop(
        &ack, &stop, R3_WORKER_INTERFERENCE) == 0U);
}

static void CaseAckExactlyOnce(void)
{
    R3WorkerContract w = Worker(R3_WORKER_PROCESSING);
    R3WorkerRunKey run = Run(1U, 2U, 3U);
    R3WorkerQuiescedAck ack;
    Bind(&w, run);
    LatchStop(&w, Stop(run, 9U));
    CHECK(R3WorkerContract_BuildQuiescedAck(
        &w, 1U, &ack) == R3_WORKER_OK);
    CHECK(R3WorkerContract_BuildQuiescedAck(
        &w, 1U, &ack) == R3_WORKER_ALREADY_ACKED);
    CHECK(w.ack_count == 1U);
}

static void CasePostAckOldMutationClosed(void)
{
    R3WorkerContract w = Worker(R3_WORKER_PROCESSING);
    R3WorkerRunKey run = Run(1U, 2U, 3U);
    R3WorkerQuiescedAck ack;
    Bind(&w, run);
    LatchStop(&w, Stop(run, 9U));
    CHECK(R3WorkerContract_BuildQuiescedAck(
        &w, 1U, &ack) == R3_WORKER_OK);
    CHECK(R3WorkerContract_AllowsBoundRunMutation(&w) == 0U);
    CHECK(R3WorkerContract_NewWorkAllowed(&w, 1U) == 0U);
}

static void CaseRebindAfterAckNewGeneration(void)
{
    R3WorkerContract w = Worker(R3_WORKER_PROCESSING);
    R3WorkerRunKey a = Run(1U, 2U, 3U);
    R3WorkerRunKey b = Run(1U, 3U, 4U);
    R3WorkerQuiescedAck ack;
    Bind(&w, a);
    LatchStop(&w, Stop(a, 9U));
    CHECK(R3WorkerContract_BuildQuiescedAck(
        &w, 1U, &ack) == R3_WORKER_OK);
    CHECK(R3WorkerContract_BindRun(&w, &a) == R3_WORKER_STALE_RUN);
    CHECK(R3WorkerContract_BindRun(&w, &b) == R3_WORKER_OK);
    CHECK(w.phase == R3_WORKER_PHASE_RUN_BOUND);
    CHECK(w.run.generation == 4U);
}

typedef void (*CaseFn)(void);
typedef struct { const char *name; CaseFn fn; } Case;

static const Case cases[] =
{
    {"init", CaseInit},
    {"bind_idempotent", CaseBindIdempotent},
    {"bind_different_active_rejected", CaseBindDifferentWhileActiveRejected},
    {"stale_stop_rejected", CaseStaleStopRejected},
    {"duplicate_stop_idempotent", CaseDuplicateStopIdempotent},
    {"conflicting_stop_rejected", CaseConflictingStopRejected},
    {"wake_stop_precedence", CaseWakeStopPrecedence},
    {"new_work_closes_on_stop", CaseNewWorkClosesOnStop},
    {"processing_empty_stop_ack", CaseProcessingEmptyStopAck},
    {"processing_held_cancel", CaseProcessingHeldCancel},
    {"processing_current_finish", CaseProcessingCurrentFinish},
    {"processing_drain_backlog", CaseProcessingDrainBacklog},
    {"interference_pending_cancel", CaseInterferencePendingCancel},
    {"interference_running_finish", CaseInterferenceRunningFinish},
    {"interference_idle_ack", CaseInterferenceIdleAck},
    {"ack_requires_released", CaseAckRequiresReleased},
    {"ack_exact_identity", CaseAckExactIdentity},
    {"ack_exactly_once", CaseAckExactlyOnce},
    {"post_ack_old_mutation_closed", CasePostAckOldMutationClosed},
    {"rebind_after_ack_new_generation", CaseRebindAfterAckNewGeneration}
};

int main(int argc, char **argv)
{
    size_t i;
    if (argc != 2)
    {
        (void)fprintf(stderr, "Usage: %s <case>\n", argv[0]);
        return EXIT_FAILURE;
    }

    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        if (strcmp(argv[1], cases[i].name) == 0)
        {
            cases[i].fn();
            (void)puts("R3-W2 worker contract native test: PASS");
            return EXIT_SUCCESS;
        }
    }

    (void)fprintf(stderr, "Unknown case: %s\n", argv[1]);
    return EXIT_FAILURE;
}
