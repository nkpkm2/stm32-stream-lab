#include "r3_worker_tasks.h"

#include <stddef.h>
#include <string.h>

typedef struct
{
    uint32_t valid;
    R3WorkerQuiescedAck ack;
} R3WorkerAckSlot;

typedef struct
{
    uint32_t create_attempted;
    uint32_t created;
    uint32_t faulted;
    R3WorkerTasksStatus first_fault;

    R3WorkerTasksConfig config;

    StaticTask_t processing_tcb;
    StaticTask_t interference_tcb;
    StackType_t processing_stack[R3_WORKER_TASKS_PROCESS_STACK_WORDS];
    StackType_t interference_stack[R3_WORKER_TASKS_INTERFERENCE_STACK_WORDS];
    TaskHandle_t processing_task;
    TaskHandle_t interference_task;

    uint32_t processing_ready;
    uint32_t interference_ready;

    R3WorkerContract processing_contract;
    R3WorkerContract interference_contract;

    uint32_t processing_ticket_valid;
    StreamRunTicket processing_ticket;

    uint32_t processing_current_valid;
    StreamOwnershipDescriptor processing_current;

    R3InterferenceActivity interference_activity;
    uint32_t interference_segment_more;

    R3WorkerAckSlot processing_ack;
    R3WorkerAckSlot interference_ack;

    uint32_t processing_wake_count;
    uint32_t interference_wake_count;
    uint32_t processing_complete_count;
    uint32_t processing_cancel_count;
    uint32_t interference_segment_count;
} R3WorkerTasksStorage;

static R3WorkerTasksStorage storage;

static int SameRun(
    const R3WorkerRunKey *a,
    const R3WorkerRunKey *b)
{
    return (a->boot_id == b->boot_id) &&
        (a->run_id == b->run_id) &&
        (a->generation == b->generation);
}

static int TicketMatchesRun(
    const StreamRunTicket *ticket,
    const R3WorkerRunKey *run)
{
    return (ticket->identity.boot_id == run->boot_id) &&
        (ticket->identity.run_id == run->run_id) &&
        (ticket->identity.generation == run->generation);
}

static R3WorkerTasksStatus LatchFault(R3WorkerTasksStatus status)
{
    taskENTER_CRITICAL();
    if (storage.faulted == 0U)
    {
        storage.first_fault = status;
        storage.faulted = 1U;
    }
    taskEXIT_CRITICAL();
    return status;
}

static R3WorkerTasksStatus LatchFaultFromISR(R3WorkerTasksStatus status)
{
    UBaseType_t saved_interrupt_status = taskENTER_CRITICAL_FROM_ISR();

    if (storage.faulted == 0U)
    {
        storage.first_fault = status;
        storage.faulted = 1U;
    }

    taskEXIT_CRITICAL_FROM_ISR(saved_interrupt_status);
    return status;
}

static int ValidConfig(const R3WorkerTasksConfig *config)
{
    if ((config == NULL) ||
        (config->communication_task == NULL) ||
        (config->processing_ack_notify_bit == 0U) ||
        (config->interference_ack_notify_bit == 0U) ||
        (config->processing_ack_notify_bit ==
            config->interference_ack_notify_bit) ||
        (config->hooks.read_control == NULL) ||
        (config->hooks.try_claim_held == NULL) ||
        (config->hooks.try_begin_interference == NULL) ||
        (config->hooks.process_block == NULL) ||
        (config->hooks.interference_segment == NULL))
    {
        return 0;
    }

    return 1;
}

static R3WorkerTasksStatus ReadControl(
    R3WorkerTaskControlView *out_view)
{
    R3WorkerTasksStatus status;

    if (out_view == NULL)
    {
        return R3_WORKER_TASKS_INVALID_ARGUMENT;
    }

    (void)memset(out_view, 0, sizeof(*out_view));
    status = storage.config.hooks.read_control(
        storage.config.hooks.context, out_view);
    if (status != R3_WORKER_TASKS_OK)
    {
        return LatchFault(R3_WORKER_TASKS_CONTROL_ERROR);
    }

    if (out_view->run_valid == 0U)
    {
        if (out_view->stop_valid != 0U)
        {
            return LatchFault(R3_WORKER_TASKS_CONTROL_ERROR);
        }
        return R3_WORKER_TASKS_OK;
    }

    if (!TicketMatchesRun(&out_view->stream_ticket, &out_view->run))
    {
        return LatchFault(R3_WORKER_TASKS_CONTROL_ERROR);
    }

    if ((out_view->stop_valid != 0U) &&
        !SameRun(&out_view->stop.run, &out_view->run))
    {
        return LatchFault(R3_WORKER_TASKS_CONTROL_ERROR);
    }

    return R3_WORKER_TASKS_OK;
}

static uint32_t AckPending(R3WorkerId worker_id)
{
    uint32_t pending = 0U;

    taskENTER_CRITICAL();
    if (worker_id == R3_WORKER_PROCESSING)
    {
        pending = storage.processing_ack.valid;
    }
    else if (worker_id == R3_WORKER_INTERFERENCE)
    {
        pending = storage.interference_ack.valid;
    }
    taskEXIT_CRITICAL();

    return pending;
}

static R3WorkerTasksStatus PublishAck(
    const R3WorkerQuiescedAck *ack)
{
    R3WorkerAckSlot *slot;
    uint32_t notify_bit;

    if (ack == NULL)
    {
        return R3_WORKER_TASKS_INVALID_ARGUMENT;
    }

    if (ack->worker_id == R3_WORKER_PROCESSING)
    {
        slot = &storage.processing_ack;
        notify_bit = storage.config.processing_ack_notify_bit;
    }
    else if (ack->worker_id == R3_WORKER_INTERFERENCE)
    {
        slot = &storage.interference_ack;
        notify_bit = storage.config.interference_ack_notify_bit;
    }
    else
    {
        return LatchFault(R3_WORKER_TASKS_INVALID_STATE);
    }

    taskENTER_CRITICAL();
    if (slot->valid != 0U)
    {
        taskEXIT_CRITICAL();
        return LatchFault(R3_WORKER_TASKS_ACK_BACKPRESSURE);
    }
    slot->ack = *ack;
    slot->valid = 1U;
    taskEXIT_CRITICAL();

    if (xTaskNotify(
            storage.config.communication_task,
            notify_bit,
            eSetBits) != pdPASS)
    {
        return LatchFault(R3_WORKER_TASKS_FREERTOS_ERROR);
    }

    return R3_WORKER_TASKS_OK;
}

static R3WorkerTasksStatus ReconcileWorker(
    R3WorkerContract *worker,
    R3WorkerId worker_id,
    const R3WorkerTaskControlView *view)
{
    R3WorkerContractSnapshot snapshot;
    R3WorkerStatus worker_status;

    if ((worker == NULL) || (view == NULL))
    {
        return R3_WORKER_TASKS_INVALID_ARGUMENT;
    }

    worker_status = R3WorkerContract_GetSnapshot(worker, &snapshot);
    if (worker_status != R3_WORKER_OK)
    {
        return LatchFault(R3_WORKER_TASKS_INVALID_STATE);
    }

    if (view->run_valid == 0U)
    {
        if ((snapshot.phase == R3_WORKER_PHASE_UNBOUND) ||
            (snapshot.phase == R3_WORKER_PHASE_QUIESCED))
        {
            return R3_WORKER_TASKS_OK;
        }
        return LatchFault(R3_WORKER_TASKS_CONTROL_ERROR);
    }

    if (snapshot.phase == R3_WORKER_PHASE_UNBOUND)
    {
        worker_status = R3WorkerContract_BindRun(worker, &view->run);
        if (worker_status != R3_WORKER_OK)
        {
            return LatchFault(R3_WORKER_TASKS_INVALID_STATE);
        }
        if (worker_id == R3_WORKER_PROCESSING)
        {
            storage.processing_ticket = view->stream_ticket;
            storage.processing_ticket_valid = 1U;
        }
    }
    else if (snapshot.phase == R3_WORKER_PHASE_QUIESCED)
    {
        if (!SameRun(&snapshot.run, &view->run))
        {
            if (AckPending(worker_id) != 0U)
            {
                return LatchFault(R3_WORKER_TASKS_ACK_BACKPRESSURE);
            }

            worker_status = R3WorkerContract_BindRun(worker, &view->run);
            if (worker_status != R3_WORKER_OK)
            {
                return LatchFault(R3_WORKER_TASKS_INVALID_STATE);
            }
            if (worker_id == R3_WORKER_PROCESSING)
            {
                storage.processing_ticket = view->stream_ticket;
                storage.processing_ticket_valid = 1U;
            }
        }
    }
    else
    {
        if (!SameRun(&snapshot.run, &view->run))
        {
            return LatchFault(R3_WORKER_TASKS_CONTROL_ERROR);
        }
    }

    if (worker_id == R3_WORKER_PROCESSING)
    {
        worker_status = R3WorkerContract_GetSnapshot(worker, &snapshot);
        if (worker_status != R3_WORKER_OK)
        {
            return LatchFault(R3_WORKER_TASKS_INVALID_STATE);
        }
        if ((snapshot.phase == R3_WORKER_PHASE_RUN_BOUND) ||
            (snapshot.phase == R3_WORKER_PHASE_STOP_PENDING))
        {
            if ((storage.processing_ticket_valid == 0U) ||
                !TicketMatchesRun(
                    &storage.processing_ticket,
                    &snapshot.run))
            {
                return LatchFault(R3_WORKER_TASKS_CONTROL_ERROR);
            }
        }
    }

    if (view->stop_valid != 0U)
    {
        worker_status = R3WorkerContract_RequestStop(
            worker, &view->stop);
        if ((worker_status != R3_WORKER_OK) &&
            (worker_status != R3_WORKER_ALREADY_ACKED))
        {
            return LatchFault(R3_WORKER_TASKS_INVALID_STATE);
        }
    }
    else
    {
        worker_status = R3WorkerContract_GetSnapshot(worker, &snapshot);
        if (worker_status != R3_WORKER_OK)
        {
            return LatchFault(R3_WORKER_TASKS_INVALID_STATE);
        }
        if (snapshot.phase == R3_WORKER_PHASE_STOP_PENDING)
        {
            return LatchFault(R3_WORKER_TASKS_CONTROL_ERROR);
        }
    }

    return R3_WORKER_TASKS_OK;
}

static R3WorkerTasksStatus CancelHeldProcessing(void)
{
    StreamQueueAdapterReceipt receipt;
    StreamRunAuthorityStatus authority_status;

    if (storage.processing_ticket_valid == 0U)
    {
        return LatchFault(R3_WORKER_TASKS_INVALID_STATE);
    }

    authority_status = StreamRunAuthority_CancelHeldReady(
        &storage.processing_ticket, &receipt);
    if (authority_status != STREAM_RUN_AUTHORITY_OK)
    {
        return LatchFault(R3_WORKER_TASKS_AUTHORITY_ERROR);
    }

    if (storage.processing_cancel_count != UINT32_MAX)
    {
        ++storage.processing_cancel_count;
    }
    return R3_WORKER_TASKS_OK;
}

static R3WorkerTasksStatus CompleteProcessing(void)
{
    StreamQueueAdapterReceipt receipt;
    StreamRunAuthorityStatus authority_status;

    if ((storage.processing_ticket_valid == 0U) ||
        (storage.processing_current_valid == 0U))
    {
        return LatchFault(R3_WORKER_TASKS_INVALID_STATE);
    }

    authority_status = StreamRunAuthority_CompleteAndReleaseBlock(
        &storage.processing_ticket, &receipt);
    if (authority_status != STREAM_RUN_AUTHORITY_OK)
    {
        return LatchFault(R3_WORKER_TASKS_AUTHORITY_ERROR);
    }

    (void)memset(
        &storage.processing_current,
        0,
        sizeof(storage.processing_current));
    storage.processing_current_valid = 0U;
    if (storage.processing_complete_count != UINT32_MAX)
    {
        ++storage.processing_complete_count;
    }
    return R3_WORKER_TASKS_OK;
}

static R3WorkerTasksStatus ProcessingAck(void)
{
    R3WorkerQuiescedAck ack;
    R3WorkerStatus worker_status;

    if (AckPending(R3_WORKER_PROCESSING) != 0U)
    {
        return LatchFault(R3_WORKER_TASKS_ACK_BACKPRESSURE);
    }
    if (storage.processing_current_valid != 0U)
    {
        return LatchFault(R3_WORKER_TASKS_INVALID_STATE);
    }

    (void)memset(
        &storage.processing_ticket,
        0,
        sizeof(storage.processing_ticket));
    storage.processing_ticket_valid = 0U;

    worker_status = R3WorkerContract_BuildQuiescedAck(
        &storage.processing_contract, 1U, &ack);
    if (worker_status != R3_WORKER_OK)
    {
        return LatchFault(R3_WORKER_TASKS_INVALID_STATE);
    }

    return PublishAck(&ack);
}

static R3WorkerTasksStatus QuiesceProcessing(void)
{
    StreamRunAuthoritySnapshot authority;
    StreamRunAuthorityStatus authority_status;
    StreamOwnershipDescriptor ownership;

    for (;;)
    {
        authority_status = StreamRunAuthority_GetSnapshot(&authority);
        if (authority_status != STREAM_RUN_AUTHORITY_OK)
        {
            return LatchFault(R3_WORKER_TASKS_AUTHORITY_ERROR);
        }

        if (authority.processing_valid != 0U)
        {
            if (storage.processing_current_valid == 0U)
            {
                return LatchFault(R3_WORKER_TASKS_AUTHORITY_ERROR);
            }
            if (CompleteProcessing() != R3_WORKER_TASKS_OK)
            {
                return storage.first_fault;
            }
            continue;
        }

        if (authority.held_valid != 0U)
        {
            if (CancelHeldProcessing() != R3_WORKER_TASKS_OK)
            {
                return storage.first_fault;
            }
            continue;
        }

        if (storage.processing_ticket_valid == 0U)
        {
            return LatchFault(R3_WORKER_TASKS_INVALID_STATE);
        }

        authority_status = StreamRunAuthority_TakeReady(
            &storage.processing_ticket, &ownership);
        if (authority_status == STREAM_RUN_AUTHORITY_EMPTY)
        {
            return ProcessingAck();
        }
        if (authority_status != STREAM_RUN_AUTHORITY_OK)
        {
            return LatchFault(R3_WORKER_TASKS_AUTHORITY_ERROR);
        }

        if (CancelHeldProcessing() != R3_WORKER_TASKS_OK)
        {
            return storage.first_fault;
        }
    }
}

static R3WorkerTasksStatus ServiceProcessing(
    uint32_t notification_bits)
{
    R3WorkerTaskControlView view;
    R3WorkerContractSnapshot worker_snapshot;
    StreamOwnershipDescriptor ownership;
    StreamRunAuthorityStatus authority_status;
    R3WorkerClaimDecision decision;
    R3WorkerStopContext stop_from_gate;
    R3WorkerStatus worker_status;
    uint32_t work_selected;

    if (storage.faulted != 0U)
    {
        return R3_WORKER_TASKS_FAULTED;
    }

    if (storage.processing_wake_count != UINT32_MAX)
    {
        ++storage.processing_wake_count;
    }

    work_selected =
        ((notification_bits & R3_WORKER_WAKE_WORK) != 0U) ? 1U : 0U;

    for (;;)
    {
        if (ReadControl(&view) != R3_WORKER_TASKS_OK)
        {
            return storage.first_fault;
        }
        if (ReconcileWorker(
                &storage.processing_contract,
                R3_WORKER_PROCESSING,
                &view) != R3_WORKER_TASKS_OK)
        {
            return storage.first_fault;
        }

        worker_status = R3WorkerContract_GetSnapshot(
            &storage.processing_contract, &worker_snapshot);
        if (worker_status != R3_WORKER_OK)
        {
            return LatchFault(R3_WORKER_TASKS_INVALID_STATE);
        }

        if (worker_snapshot.phase == R3_WORKER_PHASE_STOP_PENDING)
        {
            return QuiesceProcessing();
        }

        if (worker_snapshot.phase != R3_WORKER_PHASE_RUN_BOUND)
        {
            return R3_WORKER_TASKS_OK;
        }

        if (work_selected == 0U)
        {
            return R3_WORKER_TASKS_OK;
        }

        if (R3WorkerContract_NewWorkAllowed(
                &storage.processing_contract,
                view.processing_claim_allowed) == 0U)
        {
            return R3_WORKER_TASKS_OK;
        }

        if (storage.processing_ticket_valid == 0U)
        {
            return LatchFault(R3_WORKER_TASKS_INVALID_STATE);
        }

        authority_status = StreamRunAuthority_TakeReady(
            &storage.processing_ticket, &ownership);
        if (authority_status == STREAM_RUN_AUTHORITY_EMPTY)
        {
            return R3_WORKER_TASKS_OK;
        }
        if (authority_status != STREAM_RUN_AUTHORITY_OK)
        {
            return LatchFault(R3_WORKER_TASKS_AUTHORITY_ERROR);
        }

        (void)memset(&stop_from_gate, 0, sizeof(stop_from_gate));
        decision = storage.config.hooks.try_claim_held(
            storage.config.hooks.context,
            &worker_snapshot.run,
            &storage.processing_ticket,
            &stop_from_gate);

        if (decision == R3_WORKER_CLAIM_GRANTED)
        {
            storage.processing_current = ownership;
            storage.processing_current_valid = 1U;

            storage.config.hooks.process_block(
                storage.config.hooks.context,
                &storage.processing_current);

            /*
             * The block was legally claimed before STOP if the claim callback
             * returned GRANTED. Complete it unconditionally before any next
             * claim. A STOP that committed during bounded processing is
             * observed on the next control read and closes further claims.
             */
            if (CompleteProcessing() != R3_WORKER_TASKS_OK)
            {
                return storage.first_fault;
            }
            continue;
        }

        if (decision == R3_WORKER_CLAIM_STOP_CURRENT)
        {
            worker_status = R3WorkerContract_RequestStop(
                &storage.processing_contract, &stop_from_gate);
            if ((worker_status != R3_WORKER_OK) &&
                (worker_status != R3_WORKER_ALREADY_ACKED))
            {
                return LatchFault(R3_WORKER_TASKS_INVALID_STATE);
            }

            if (CancelHeldProcessing() != R3_WORKER_TASKS_OK)
            {
                return storage.first_fault;
            }
            return QuiesceProcessing();
        }

        if (decision == R3_WORKER_CLAIM_CLOSED)
        {
            if (CancelHeldProcessing() != R3_WORKER_TASKS_OK)
            {
                return storage.first_fault;
            }
            return R3_WORKER_TASKS_OK;
        }

        return LatchFault(
            decision == R3_WORKER_CLAIM_STALE ?
                R3_WORKER_TASKS_CONTROL_ERROR :
                R3_WORKER_TASKS_CALLBACK_ERROR);
    }
}

static R3WorkerTasksStatus InterferenceAck(void)
{
    R3WorkerQuiescedAck ack;
    R3WorkerStatus worker_status;

    if (AckPending(R3_WORKER_INTERFERENCE) != 0U)
    {
        return LatchFault(R3_WORKER_TASKS_ACK_BACKPRESSURE);
    }
    if (storage.interference_activity != R3_INTERFERENCE_ACTIVITY_NONE)
    {
        return LatchFault(R3_WORKER_TASKS_INVALID_STATE);
    }

    storage.interference_segment_more = 0U;
    worker_status = R3WorkerContract_BuildQuiescedAck(
        &storage.interference_contract, 1U, &ack);
    if (worker_status != R3_WORKER_OK)
    {
        return LatchFault(R3_WORKER_TASKS_INVALID_STATE);
    }

    return PublishAck(&ack);
}

static R3WorkerTasksStatus HandleInterferenceStop(void)
{
    R3InterferenceStopAction action;

    action = R3InterferenceWorker_StopAction(
        &storage.interference_contract,
        storage.interference_activity);

    switch (action)
    {
        case R3_INTERFERENCE_STOP_CANCEL_PENDING:
            storage.interference_activity = R3_INTERFERENCE_ACTIVITY_NONE;
            storage.interference_segment_more = 0U;
            return InterferenceAck();

        case R3_INTERFERENCE_STOP_FINISH_SEGMENT:
            storage.interference_activity = R3_INTERFERENCE_ACTIVITY_NONE;
            storage.interference_segment_more = 0U;
            return InterferenceAck();

        case R3_INTERFERENCE_STOP_EMIT_ACK:
            return InterferenceAck();

        default:
            return LatchFault(R3_WORKER_TASKS_INVALID_STATE);
    }
}

static R3WorkerTasksStatus ServiceInterference(
    uint32_t notification_bits)
{
    R3WorkerTaskControlView view;
    R3WorkerContractSnapshot worker_snapshot;
    R3InterferenceReleaseDecision decision;
    R3WorkerStopContext stop_from_gate;
    R3WorkerStatus worker_status;
    uint32_t work_bit =
        ((notification_bits & R3_WORKER_WAKE_WORK) != 0U) ? 1U : 0U;

    if (storage.faulted != 0U)
    {
        return R3_WORKER_TASKS_FAULTED;
    }

    if (storage.interference_wake_count != UINT32_MAX)
    {
        ++storage.interference_wake_count;
    }

    for (;;)
    {
        if (ReadControl(&view) != R3_WORKER_TASKS_OK)
        {
            return storage.first_fault;
        }
        if (ReconcileWorker(
                &storage.interference_contract,
                R3_WORKER_INTERFERENCE,
                &view) != R3_WORKER_TASKS_OK)
        {
            return storage.first_fault;
        }

        worker_status = R3WorkerContract_GetSnapshot(
            &storage.interference_contract, &worker_snapshot);
        if (worker_status != R3_WORKER_OK)
        {
            return LatchFault(R3_WORKER_TASKS_INVALID_STATE);
        }

        if (worker_snapshot.phase == R3_WORKER_PHASE_STOP_PENDING)
        {
            return HandleInterferenceStop();
        }

        if (worker_snapshot.phase != R3_WORKER_PHASE_RUN_BOUND)
        {
            return R3_WORKER_TASKS_OK;
        }

        if ((work_bit != 0U) &&
            (storage.interference_activity == R3_INTERFERENCE_ACTIVITY_NONE))
        {
            storage.interference_activity = R3_INTERFERENCE_ACTIVITY_PENDING;
            work_bit = 0U;

            /*
             * Re-read control after publishing PENDING locally. This is the
             * deterministic W2-T05-A race point: STOP may become current
             * before the segment-release linearization callback.
             */
            continue;
        }

        if (storage.interference_activity == R3_INTERFERENCE_ACTIVITY_RUNNING)
        {
            /*
             * A bounded segment already returned. STOP may have committed
             * during that segment; the control re-read above gets first say.
             * No STOP means transition according to the callback result.
             */
            storage.interference_activity =
                (storage.interference_segment_more != 0U) ?
                    R3_INTERFERENCE_ACTIVITY_PENDING :
                    R3_INTERFERENCE_ACTIVITY_NONE;
            storage.interference_segment_more = 0U;

            if (storage.interference_activity == R3_INTERFERENCE_ACTIVITY_NONE)
            {
                return R3_WORKER_TASKS_OK;
            }
            continue;
        }

        if (storage.interference_activity != R3_INTERFERENCE_ACTIVITY_PENDING)
        {
            return R3_WORKER_TASKS_OK;
        }

        if (R3WorkerContract_NewWorkAllowed(
                &storage.interference_contract,
                view.interference_release_allowed) == 0U)
        {
            return R3_WORKER_TASKS_OK;
        }

        (void)memset(&stop_from_gate, 0, sizeof(stop_from_gate));
        decision = storage.config.hooks.try_begin_interference(
            storage.config.hooks.context,
            &worker_snapshot.run,
            &stop_from_gate);

        if (decision == R3_INTERFERENCE_RELEASE_GRANTED)
        {
            storage.interference_activity = R3_INTERFERENCE_ACTIVITY_RUNNING;
            storage.interference_segment_more =
                storage.config.hooks.interference_segment(
                    storage.config.hooks.context) != 0U ? 1U : 0U;
            if (storage.interference_segment_count != UINT32_MAX)
            {
                ++storage.interference_segment_count;
            }
            /*
             * Do not collapse RUNNING here. The next loop iteration re-reads
             * control while activity is still RUNNING, so a STOP committed
             * during the segment maps to FINISH_SEGMENT before ACK.
             */
            continue;
        }

        if (decision == R3_INTERFERENCE_RELEASE_STOP_CURRENT)
        {
            worker_status = R3WorkerContract_RequestStop(
                &storage.interference_contract, &stop_from_gate);
            if ((worker_status != R3_WORKER_OK) &&
                (worker_status != R3_WORKER_ALREADY_ACKED))
            {
                return LatchFault(R3_WORKER_TASKS_INVALID_STATE);
            }
            return HandleInterferenceStop();
        }

        if (decision == R3_INTERFERENCE_RELEASE_CLOSED)
        {
            return R3_WORKER_TASKS_OK;
        }

        return LatchFault(
            decision == R3_INTERFERENCE_RELEASE_STALE ?
                R3_WORKER_TASKS_CONTROL_ERROR :
                R3_WORKER_TASKS_CALLBACK_ERROR);
    }
}

static void ProcessingTask(void *argument)
{
    uint32_t notification_bits;
    (void)argument;

    if (R3WorkerContract_Initialize(
            &storage.processing_contract,
            R3_WORKER_PROCESSING) != R3_WORKER_OK)
    {
        (void)LatchFault(R3_WORKER_TASKS_INVALID_STATE);
    }

    taskENTER_CRITICAL();
    storage.processing_ready = 1U;
    taskEXIT_CRITICAL();

    for (;;)
    {
        notification_bits = 0U;
        (void)xTaskNotifyWait(
            0U,
            R3_WORKER_WAKE_MASK,
            &notification_bits,
            portMAX_DELAY);
        (void)ServiceProcessing(notification_bits);
    }
}

static void InterferenceTask(void *argument)
{
    uint32_t notification_bits;
    (void)argument;

    if (R3WorkerContract_Initialize(
            &storage.interference_contract,
            R3_WORKER_INTERFERENCE) != R3_WORKER_OK)
    {
        (void)LatchFault(R3_WORKER_TASKS_INVALID_STATE);
    }

    taskENTER_CRITICAL();
    storage.interference_ready = 1U;
    taskEXIT_CRITICAL();

    for (;;)
    {
        notification_bits = 0U;
        (void)xTaskNotifyWait(
            0U,
            R3_WORKER_WAKE_MASK,
            &notification_bits,
            portMAX_DELAY);
        (void)ServiceInterference(notification_bits);
    }
}

R3WorkerTasksStatus R3WorkerTasks_Create(
    const R3WorkerTasksConfig *config)
{
    if (!ValidConfig(config))
    {
        return R3_WORKER_TASKS_INVALID_ARGUMENT;
    }
    if (storage.create_attempted != 0U)
    {
        return R3_WORKER_TASKS_INVALID_STATE;
    }

    (void)memset(&storage, 0, sizeof(storage));
    storage.create_attempted = 1U;
    storage.config = *config;
    storage.first_fault = R3_WORKER_TASKS_OK;
    storage.interference_activity = R3_INTERFERENCE_ACTIVITY_NONE;

    storage.processing_task = xTaskCreateStatic(
        ProcessingTask,
        "R3Proc",
        R3_WORKER_TASKS_PROCESS_STACK_WORDS,
        NULL,
        R3_WORKER_TASKS_PROCESS_PRIORITY,
        storage.processing_stack,
        &storage.processing_tcb);
    if (storage.processing_task == NULL)
    {
        return LatchFault(R3_WORKER_TASKS_FREERTOS_ERROR);
    }

    storage.interference_task = xTaskCreateStatic(
        InterferenceTask,
        "R3Intrf",
        R3_WORKER_TASKS_INTERFERENCE_STACK_WORDS,
        NULL,
        R3_WORKER_TASKS_INTERFERENCE_PRIORITY,
        storage.interference_stack,
        &storage.interference_tcb);
    if (storage.interference_task == NULL)
    {
        return LatchFault(R3_WORKER_TASKS_FREERTOS_ERROR);
    }

    storage.created = 1U;
    return R3_WORKER_TASKS_OK;
}

R3WorkerTasksStatus R3WorkerTasks_GetHandles(
    TaskHandle_t *out_processing,
    TaskHandle_t *out_interference)
{
    if ((out_processing == NULL) || (out_interference == NULL))
    {
        return R3_WORKER_TASKS_INVALID_ARGUMENT;
    }
    if (storage.created == 0U)
    {
        return R3_WORKER_TASKS_INVALID_STATE;
    }

    *out_processing = storage.processing_task;
    *out_interference = storage.interference_task;
    return R3_WORKER_TASKS_OK;
}

R3WorkerTasksStatus R3WorkerTasks_GetSnapshot(
    R3WorkerTasksSnapshot *out)
{
    if (out == NULL)
    {
        return R3_WORKER_TASKS_INVALID_ARGUMENT;
    }

    taskENTER_CRITICAL();
    (void)memset(out, 0, sizeof(*out));
    out->create_attempted = storage.create_attempted;
    out->created = storage.created;
    out->faulted = storage.faulted;
    out->first_fault = storage.first_fault;
    out->processing_ready = storage.processing_ready;
    out->interference_ready = storage.interference_ready;
    out->processing_task = storage.processing_task;
    out->interference_task = storage.interference_task;
    out->processing_ack_pending = storage.processing_ack.valid;
    out->interference_ack_pending = storage.interference_ack.valid;
    out->processing_wake_count = storage.processing_wake_count;
    out->interference_wake_count = storage.interference_wake_count;
    out->processing_complete_count = storage.processing_complete_count;
    out->processing_cancel_count = storage.processing_cancel_count;
    out->interference_segment_count = storage.interference_segment_count;
    out->interference_activity = storage.interference_activity;
    (void)R3WorkerContract_GetSnapshot(
        &storage.processing_contract, &out->processing_contract);
    (void)R3WorkerContract_GetSnapshot(
        &storage.interference_contract, &out->interference_contract);
    taskEXIT_CRITICAL();

    return R3_WORKER_TASKS_OK;
}

static R3WorkerTasksStatus RequireCommunicationCaller(void)
{
    if (storage.created == 0U)
    {
        return R3_WORKER_TASKS_INVALID_STATE;
    }
    if (xTaskGetCurrentTaskHandle() !=
        storage.config.communication_task)
    {
        return R3_WORKER_TASKS_INVALID_STATE;
    }
    if (storage.faulted != 0U)
    {
        return R3_WORKER_TASKS_FAULTED;
    }
    return R3_WORKER_TASKS_OK;
}

static R3WorkerTasksStatus NotifyBoth(uint32_t bit)
{
    R3WorkerTasksStatus status = RequireCommunicationCaller();

    if (status != R3_WORKER_TASKS_OK)
    {
        return status;
    }
    if ((bit != R3_WORKER_WAKE_START) &&
        (bit != R3_WORKER_WAKE_STOP))
    {
        return R3_WORKER_TASKS_INVALID_ARGUMENT;
    }

    if (xTaskNotify(
            storage.processing_task,
            bit,
            eSetBits) != pdPASS)
    {
        return LatchFault(R3_WORKER_TASKS_FREERTOS_ERROR);
    }
    if (xTaskNotify(
            storage.interference_task,
            bit,
            eSetBits) != pdPASS)
    {
        return LatchFault(R3_WORKER_TASKS_FREERTOS_ERROR);
    }

    return R3_WORKER_TASKS_OK;
}

R3WorkerTasksStatus R3WorkerTasks_NotifyStart(void)
{
    return NotifyBoth(R3_WORKER_WAKE_START);
}

R3WorkerTasksStatus R3WorkerTasks_NotifyStop(void)
{
    return NotifyBoth(R3_WORKER_WAKE_STOP);
}

R3WorkerTasksStatus R3WorkerTasks_NotifyInterferenceWork(void)
{
    R3WorkerTasksStatus status = RequireCommunicationCaller();

    if (status != R3_WORKER_TASKS_OK)
    {
        return status;
    }

    if (xTaskNotify(
            storage.interference_task,
            R3_WORKER_WAKE_WORK,
            eSetBits) != pdPASS)
    {
        return LatchFault(R3_WORKER_TASKS_FREERTOS_ERROR);
    }
    return R3_WORKER_TASKS_OK;
}

R3WorkerTasksStatus R3WorkerTasks_NotifyProcessingWorkFromISR(
    BaseType_t *higher_priority_task_woken)
{
    if (higher_priority_task_woken == NULL)
    {
        return R3_WORKER_TASKS_INVALID_ARGUMENT;
    }
    if (storage.created == 0U)
    {
        return R3_WORKER_TASKS_INVALID_STATE;
    }
    if (storage.faulted != 0U)
    {
        return R3_WORKER_TASKS_FAULTED;
    }

    if (xTaskNotifyFromISR(
            storage.processing_task,
            R3_WORKER_WAKE_WORK,
            eSetBits,
            higher_priority_task_woken) != pdPASS)
    {
        return LatchFaultFromISR(R3_WORKER_TASKS_FREERTOS_ERROR);
    }
    return R3_WORKER_TASKS_OK;
}

R3WorkerTasksStatus R3WorkerTasks_TakeAck(
    R3WorkerId worker_id,
    R3WorkerQuiescedAck *out_ack)
{
    R3WorkerAckSlot *slot;
    R3WorkerTasksStatus status = RequireCommunicationCaller();

    if (status != R3_WORKER_TASKS_OK)
    {
        return status;
    }
    if (out_ack == NULL)
    {
        return R3_WORKER_TASKS_INVALID_ARGUMENT;
    }

    if (worker_id == R3_WORKER_PROCESSING)
    {
        slot = &storage.processing_ack;
    }
    else if (worker_id == R3_WORKER_INTERFERENCE)
    {
        slot = &storage.interference_ack;
    }
    else
    {
        return R3_WORKER_TASKS_INVALID_ARGUMENT;
    }

    taskENTER_CRITICAL();
    if (slot->valid == 0U)
    {
        taskEXIT_CRITICAL();
        return R3_WORKER_TASKS_EMPTY;
    }
    *out_ack = slot->ack;
    (void)memset(&slot->ack, 0, sizeof(slot->ack));
    slot->valid = 0U;
    taskEXIT_CRITICAL();

    return R3_WORKER_TASKS_OK;
}

#ifdef R3_WORKER_TASKS_TESTING
R3WorkerTasksStatus R3WorkerTasks_TestInstall(
    const R3WorkerTasksConfig *config,
    TaskHandle_t processing_task,
    TaskHandle_t interference_task)
{
    if (!ValidConfig(config) ||
        (processing_task == NULL) ||
        (interference_task == NULL))
    {
        return R3_WORKER_TASKS_INVALID_ARGUMENT;
    }

    (void)memset(&storage, 0, sizeof(storage));
    storage.create_attempted = 1U;
    storage.config = *config;
    storage.processing_task = processing_task;
    storage.interference_task = interference_task;
    storage.created = 1U;
    storage.first_fault = R3_WORKER_TASKS_OK;
    storage.interference_activity = R3_INTERFERENCE_ACTIVITY_NONE;

    if ((R3WorkerContract_Initialize(
             &storage.processing_contract,
             R3_WORKER_PROCESSING) != R3_WORKER_OK) ||
        (R3WorkerContract_Initialize(
             &storage.interference_contract,
             R3_WORKER_INTERFERENCE) != R3_WORKER_OK))
    {
        return LatchFault(R3_WORKER_TASKS_INVALID_STATE);
    }

    storage.processing_ready = 1U;
    storage.interference_ready = 1U;
    return R3_WORKER_TASKS_OK;
}

R3WorkerTasksStatus R3WorkerTasks_TestServiceProcessing(
    uint32_t notification_bits)
{
    return ServiceProcessing(notification_bits);
}

R3WorkerTasksStatus R3WorkerTasks_TestServiceInterference(
    uint32_t notification_bits)
{
    return ServiceInterference(notification_bits);
}
#endif
