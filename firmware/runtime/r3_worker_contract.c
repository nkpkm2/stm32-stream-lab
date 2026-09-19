#include "r3_worker_contract.h"

#include <stddef.h>
#include <string.h>

static int ValidWorkerId(R3WorkerId worker_id)
{
    return (worker_id == R3_WORKER_PROCESSING) ||
        (worker_id == R3_WORKER_INTERFERENCE);
}

static int SameRun(
    const R3WorkerRunKey *a,
    const R3WorkerRunKey *b)
{
    return (a->boot_id == b->boot_id) &&
        (a->run_id == b->run_id) &&
        (a->generation == b->generation);
}

static void Reject(R3WorkerContract *worker)
{
    if ((worker != NULL) && (worker->reject_count != UINT32_MAX))
    {
        ++worker->reject_count;
    }
}

R3WorkerStatus R3WorkerContract_Initialize(
    R3WorkerContract *worker,
    R3WorkerId worker_id)
{
    if ((worker == NULL) || !ValidWorkerId(worker_id))
    {
        return R3_WORKER_INVALID_ARGUMENT;
    }

    (void)memset(worker, 0, sizeof(*worker));
    worker->worker_id = worker_id;
    worker->phase = R3_WORKER_PHASE_UNBOUND;
    return R3_WORKER_OK;
}

R3WorkerStatus R3WorkerContract_BindRun(
    R3WorkerContract *worker,
    const R3WorkerRunKey *run)
{
    if ((worker == NULL) || (run == NULL) ||
        !ValidWorkerId(worker->worker_id))
    {
        return R3_WORKER_INVALID_ARGUMENT;
    }

    if (worker->phase == R3_WORKER_PHASE_RUN_BOUND)
    {
        if (SameRun(&worker->run, run))
        {
            return R3_WORKER_OK;
        }
        Reject(worker);
        return R3_WORKER_INVALID_STATE;
    }

    if (worker->phase == R3_WORKER_PHASE_STOP_PENDING)
    {
        Reject(worker);
        return R3_WORKER_INVALID_STATE;
    }

    if ((worker->phase == R3_WORKER_PHASE_QUIESCED) &&
        SameRun(&worker->run, run))
    {
        Reject(worker);
        return R3_WORKER_STALE_RUN;
    }

    worker->run = *run;
    worker->stop_id = 0U;
    worker->ack_emitted = 0U;
    worker->phase = R3_WORKER_PHASE_RUN_BOUND;
    if (worker->bind_count != UINT32_MAX)
    {
        ++worker->bind_count;
    }
    return R3_WORKER_OK;
}

R3WorkerStatus R3WorkerContract_RequestStop(
    R3WorkerContract *worker,
    const R3WorkerStopContext *stop)
{
    if ((worker == NULL) || (stop == NULL) ||
        !ValidWorkerId(worker->worker_id))
    {
        return R3_WORKER_INVALID_ARGUMENT;
    }

    if (worker->phase == R3_WORKER_PHASE_UNBOUND)
    {
        Reject(worker);
        return R3_WORKER_INVALID_STATE;
    }

    if (!SameRun(&worker->run, &stop->run))
    {
        Reject(worker);
        return R3_WORKER_STALE_RUN;
    }

    if (worker->phase == R3_WORKER_PHASE_QUIESCED)
    {
        if ((worker->ack_emitted != 0U) &&
            (worker->stop_id == stop->stop_id))
        {
            return R3_WORKER_ALREADY_ACKED;
        }
        Reject(worker);
        return R3_WORKER_INVALID_STATE;
    }

    if (worker->phase == R3_WORKER_PHASE_STOP_PENDING)
    {
        if (worker->stop_id == stop->stop_id)
        {
            return R3_WORKER_OK;
        }
        Reject(worker);
        return R3_WORKER_STOP_CONFLICT;
    }

    worker->stop_id = stop->stop_id;
    worker->phase = R3_WORKER_PHASE_STOP_PENDING;
    if (worker->stop_latch_count != UINT32_MAX)
    {
        ++worker->stop_latch_count;
    }
    return R3_WORKER_OK;
}

R3WorkerStatus R3WorkerContract_BuildQuiescedAck(
    R3WorkerContract *worker,
    uint32_t resources_released,
    R3WorkerQuiescedAck *out_ack)
{
    if ((worker == NULL) || (out_ack == NULL) ||
        !ValidWorkerId(worker->worker_id))
    {
        return R3_WORKER_INVALID_ARGUMENT;
    }

    if (worker->phase == R3_WORKER_PHASE_QUIESCED)
    {
        return R3_WORKER_ALREADY_ACKED;
    }

    if (worker->phase != R3_WORKER_PHASE_STOP_PENDING)
    {
        Reject(worker);
        return R3_WORKER_INVALID_STATE;
    }

    if (resources_released == 0U)
    {
        return R3_WORKER_NOT_READY;
    }

    out_ack->boot_id = worker->run.boot_id;
    out_ack->run_id = worker->run.run_id;
    out_ack->generation = worker->run.generation;
    out_ack->stop_id = worker->stop_id;
    out_ack->worker_id = worker->worker_id;

    worker->ack_emitted = 1U;
    worker->phase = R3_WORKER_PHASE_QUIESCED;
    if (worker->ack_count != UINT32_MAX)
    {
        ++worker->ack_count;
    }
    return R3_WORKER_OK;
}

R3WorkerStatus R3WorkerContract_GetSnapshot(
    const R3WorkerContract *worker,
    R3WorkerContractSnapshot *out)
{
    if ((worker == NULL) || (out == NULL) ||
        !ValidWorkerId(worker->worker_id))
    {
        return R3_WORKER_INVALID_ARGUMENT;
    }

    out->worker_id = worker->worker_id;
    out->phase = worker->phase;
    out->run = worker->run;
    out->stop_id = worker->stop_id;
    out->ack_emitted = worker->ack_emitted;
    out->bind_count = worker->bind_count;
    out->stop_latch_count = worker->stop_latch_count;
    out->ack_count = worker->ack_count;
    out->reject_count = worker->reject_count;
    return R3_WORKER_OK;
}

uint32_t R3WorkerContract_AllowsBoundRunMutation(
    const R3WorkerContract *worker)
{
    if ((worker == NULL) || !ValidWorkerId(worker->worker_id))
    {
        return 0U;
    }

    return ((worker->phase == R3_WORKER_PHASE_RUN_BOUND) ||
            (worker->phase == R3_WORKER_PHASE_STOP_PENDING)) ? 1U : 0U;
}

uint32_t R3WorkerContract_NewWorkAllowed(
    const R3WorkerContract *worker,
    uint32_t lifecycle_gate_open)
{
    if ((worker == NULL) || !ValidWorkerId(worker->worker_id))
    {
        return 0U;
    }

    return ((worker->phase == R3_WORKER_PHASE_RUN_BOUND) &&
            (lifecycle_gate_open != 0U)) ? 1U : 0U;
}

R3WorkerWakeSelection R3WorkerContract_SelectWake(uint32_t notification_bits)
{
    uint32_t masked = notification_bits & R3_WORKER_WAKE_MASK;

    if ((masked & R3_WORKER_WAKE_STOP) != 0U)
    {
        return R3_WORKER_WAKE_STOP_SELECTED;
    }
    if ((masked & R3_WORKER_WAKE_START) != 0U)
    {
        return R3_WORKER_WAKE_START_SELECTED;
    }
    if ((masked & R3_WORKER_WAKE_WORK) != 0U)
    {
        return R3_WORKER_WAKE_WORK_SELECTED;
    }
    return R3_WORKER_WAKE_NONE;
}

uint32_t R3WorkerAck_MatchesStop(
    const R3WorkerQuiescedAck *ack,
    const R3WorkerStopContext *stop,
    R3WorkerId expected_worker)
{
    if ((ack == NULL) || (stop == NULL) ||
        !ValidWorkerId(expected_worker))
    {
        return 0U;
    }

    return (ack->boot_id == stop->run.boot_id) &&
        (ack->run_id == stop->run.run_id) &&
        (ack->generation == stop->run.generation) &&
        (ack->stop_id == stop->stop_id) &&
        (ack->worker_id == expected_worker);
}

R3ProcessingStopAction R3ProcessingWorker_StopAction(
    const R3WorkerContract *worker,
    R3ProcessingResourceState resource_state,
    uint32_t ready_available)
{
    if ((worker == NULL) ||
        (worker->worker_id != R3_WORKER_PROCESSING) ||
        (worker->phase != R3_WORKER_PHASE_STOP_PENDING))
    {
        return R3_PROCESSING_STOP_INVALID;
    }

    switch (resource_state)
    {
        case R3_PROCESSING_RESOURCE_HELD_READY:
            return R3_PROCESSING_STOP_CANCEL_HELD;

        case R3_PROCESSING_RESOURCE_PROCESSING:
            return R3_PROCESSING_STOP_FINISH_CURRENT;

        case R3_PROCESSING_RESOURCE_NONE:
            return (ready_available != 0U) ?
                R3_PROCESSING_STOP_DRAIN_ONE_READY :
                R3_PROCESSING_STOP_EMIT_ACK;

        default:
            return R3_PROCESSING_STOP_INVALID;
    }
}

R3InterferenceStopAction R3InterferenceWorker_StopAction(
    const R3WorkerContract *worker,
    R3InterferenceActivity activity)
{
    if ((worker == NULL) ||
        (worker->worker_id != R3_WORKER_INTERFERENCE) ||
        (worker->phase != R3_WORKER_PHASE_STOP_PENDING))
    {
        return R3_INTERFERENCE_STOP_INVALID;
    }

    switch (activity)
    {
        case R3_INTERFERENCE_ACTIVITY_NONE:
            return R3_INTERFERENCE_STOP_EMIT_ACK;

        case R3_INTERFERENCE_ACTIVITY_PENDING:
            return R3_INTERFERENCE_STOP_CANCEL_PENDING;

        case R3_INTERFERENCE_ACTIVITY_RUNNING:
            return R3_INTERFERENCE_STOP_FINISH_SEGMENT;

        default:
            return R3_INTERFERENCE_STOP_INVALID;
    }
}
