#include "r3_worker_tasks.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        (void)fprintf(stderr, "CHECK failed: %s (%s:%d)\n", \
            #condition, __FILE__, __LINE__); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

#define PROC_HANDLE ((TaskHandle_t)(uintptr_t)0x101U)
#define INTF_HANDLE ((TaskHandle_t)(uintptr_t)0x202U)
#define COMM_HANDLE ((TaskHandle_t)(uintptr_t)0x303U)
#define OTHER_HANDLE ((TaskHandle_t)(uintptr_t)0x404U)

#define ACK_PROC_BIT (1UL << 8)
#define ACK_INTF_BIT (1UL << 9)

typedef struct
{
    R3WorkerTaskControlView control;
    uint32_t read_calls;
    uint32_t inject_stop_on_read_call;
    uint32_t inject_stop_on_claim;
    uint32_t inject_stop_during_process;
    uint32_t inject_stop_on_interference_release;
    uint32_t inject_stop_during_segment;
    uint32_t segment_more;

    uint32_t process_calls;
    uint32_t segment_calls;
    uint32_t claim_calls;
    uint32_t release_calls;

    uint32_t ready_count;
    uint32_t held_valid;
    uint32_t processing_valid;
    StreamOwnershipDescriptor held;
    StreamOwnershipDescriptor processing;

    uint32_t take_count;
    uint32_t cancel_count;
    uint32_t complete_count;

    TaskHandle_t current_task;
    uint32_t critical_depth;
    uint32_t task_critical_enter_count;
    uint32_t isr_critical_enter_count;
    uint32_t isr_critical_exit_count;
    uint32_t fail_notify_from_isr;
    uint32_t communication_notify_bits;
    uint32_t communication_notify_count;
    uint32_t processing_notify_bits;
    uint32_t interference_notify_bits;
    uint32_t create_calls;
    uint32_t fail_create_call;
} Fake;

static Fake fake;

static R3WorkerRunKey Run(uint32_t run_id, uint32_t generation)
{
    R3WorkerRunKey run;

    run.boot_id = 7U;
    run.run_id = run_id;
    run.generation = generation;
    return run;
}

static void ApplyRunToControl(
    uint32_t run_id,
    uint32_t generation)
{
    R3WorkerRunKey run = Run(run_id, generation);

    (void)memset(&fake.control, 0, sizeof(fake.control));
    fake.control.run_valid = 1U;
    fake.control.run = run;
    fake.control.stream_ticket.identity.boot_id = run.boot_id;
    fake.control.stream_ticket.identity.run_id = run.run_id;
    fake.control.stream_ticket.identity.generation = run.generation;
    fake.control.processing_claim_allowed = 1U;
    fake.control.interference_release_allowed = 1U;
}

static void CommitStop(uint32_t stop_id)
{
    fake.control.processing_claim_allowed = 0U;
    fake.control.interference_release_allowed = 0U;
    fake.control.stop_valid = 1U;
    fake.control.stop.run = fake.control.run;
    fake.control.stop.stop_id = stop_id;
}

static R3WorkerTasksStatus ReadControl(
    void *context,
    R3WorkerTaskControlView *out_view)
{
    (void)context;
    ++fake.read_calls;

    if ((fake.inject_stop_on_read_call != 0U) &&
        (fake.read_calls == fake.inject_stop_on_read_call))
    {
        CommitStop(91U);
    }

    *out_view = fake.control;
    return R3_WORKER_TASKS_OK;
}

static R3WorkerClaimDecision TryClaimHeld(
    void *context,
    const R3WorkerRunKey *run,
    const StreamRunTicket *ticket,
    R3WorkerStopContext *out_stop)
{
    (void)context;
    ++fake.claim_calls;

    CHECK(run != NULL);
    CHECK(ticket != NULL);
    CHECK(out_stop != NULL);

    if (fake.inject_stop_on_claim != 0U)
    {
        CommitStop(92U);
    }

    if (fake.control.stop_valid != 0U)
    {
        *out_stop = fake.control.stop;
        return R3_WORKER_CLAIM_STOP_CURRENT;
    }
    if (fake.control.processing_claim_allowed == 0U)
    {
        return R3_WORKER_CLAIM_CLOSED;
    }
    if ((fake.held_valid == 0U) ||
        (fake.processing_valid != 0U))
    {
        return R3_WORKER_CLAIM_ERROR;
    }
    if ((run->boot_id != fake.control.run.boot_id) ||
        (run->run_id != fake.control.run.run_id) ||
        (run->generation != fake.control.run.generation) ||
        (ticket->identity.boot_id != run->boot_id) ||
        (ticket->identity.run_id != run->run_id) ||
        (ticket->identity.generation != run->generation))
    {
        return R3_WORKER_CLAIM_STALE;
    }

    fake.processing = fake.held;
    fake.processing_valid = 1U;
    fake.held_valid = 0U;
    return R3_WORKER_CLAIM_GRANTED;
}

static R3InterferenceReleaseDecision TryBeginInterference(
    void *context,
    const R3WorkerRunKey *run,
    R3WorkerStopContext *out_stop)
{
    (void)context;
    ++fake.release_calls;

    CHECK(run != NULL);
    CHECK(out_stop != NULL);

    if (fake.inject_stop_on_interference_release != 0U)
    {
        CommitStop(93U);
    }

    if (fake.control.stop_valid != 0U)
    {
        *out_stop = fake.control.stop;
        return R3_INTERFERENCE_RELEASE_STOP_CURRENT;
    }
    if (fake.control.interference_release_allowed == 0U)
    {
        return R3_INTERFERENCE_RELEASE_CLOSED;
    }
    if ((run->boot_id != fake.control.run.boot_id) ||
        (run->run_id != fake.control.run.run_id) ||
        (run->generation != fake.control.run.generation))
    {
        return R3_INTERFERENCE_RELEASE_STALE;
    }
    return R3_INTERFERENCE_RELEASE_GRANTED;
}

static void ProcessBlock(
    void *context,
    const StreamOwnershipDescriptor *ownership)
{
    (void)context;
    CHECK(ownership != NULL);
    ++fake.process_calls;

    if (fake.inject_stop_during_process != 0U)
    {
        CommitStop(94U);
    }
}

static uint32_t InterferenceSegment(void *context)
{
    (void)context;
    ++fake.segment_calls;

    if (fake.inject_stop_during_segment != 0U)
    {
        CommitStop(95U);
    }
    return fake.segment_more;
}

static R3WorkerTasksConfig Config(void)
{
    R3WorkerTasksConfig config;

    (void)memset(&config, 0, sizeof(config));
    config.communication_task = COMM_HANDLE;
    config.processing_ack_notify_bit = ACK_PROC_BIT;
    config.interference_ack_notify_bit = ACK_INTF_BIT;
    config.hooks.context = NULL;
    config.hooks.read_control = ReadControl;
    config.hooks.try_claim_held = TryClaimHeld;
    config.hooks.try_begin_interference = TryBeginInterference;
    config.hooks.process_block = ProcessBlock;
    config.hooks.interference_segment = InterferenceSegment;
    return config;
}

static void Reset(void)
{
    R3WorkerTasksConfig config = Config();

    (void)memset(&fake, 0, sizeof(fake));
    ApplyRunToControl(11U, 21U);
    fake.current_task = PROC_HANDLE;
    CHECK(R3WorkerTasks_TestInstall(
        &config, PROC_HANDLE, INTF_HANDLE) == R3_WORKER_TASKS_OK);
}

static void BindProcessing(void)
{
    fake.current_task = PROC_HANDLE;
    CHECK(R3WorkerTasks_TestServiceProcessing(
        R3_WORKER_WAKE_START) == R3_WORKER_TASKS_OK);
}

static void BindInterference(void)
{
    fake.current_task = INTF_HANDLE;
    CHECK(R3WorkerTasks_TestServiceInterference(
        R3_WORKER_WAKE_START) == R3_WORKER_TASKS_OK);
}

static R3WorkerQuiescedAck TakeAck(R3WorkerId worker_id)
{
    R3WorkerQuiescedAck ack;

    fake.current_task = COMM_HANDLE;
    CHECK(R3WorkerTasks_TakeAck(worker_id, &ack) ==
        R3_WORKER_TASKS_OK);
    return ack;
}

static void CheckAck(
    const R3WorkerQuiescedAck *ack,
    R3WorkerId worker_id,
    uint32_t stop_id)
{
    CHECK(ack->boot_id == fake.control.run.boot_id);
    CHECK(ack->run_id == fake.control.run.run_id);
    CHECK(ack->generation == fake.control.run.generation);
    CHECK(ack->stop_id == stop_id);
    CHECK(ack->worker_id == worker_id);
}

static void AddReady(uint32_t sequence, R2_BufferId id)
{
    CHECK(fake.ready_count == 0U);
    fake.ready_count = 1U;
    fake.held.sequence = sequence;
    fake.held.buffer_id = id;
    fake.held.completed_slot = 0U;
    fake.held.mapping_epoch = sequence;
}

/* ---- FreeRTOS stubs ---- */

void R3Test_EnterCritical(void)
{
    ++fake.task_critical_enter_count;
    ++fake.critical_depth;
}

void R3Test_ExitCritical(void)
{
    CHECK(fake.critical_depth != 0U);
    --fake.critical_depth;
}

UBaseType_t R3Test_EnterCriticalFromISR(void)
{
    ++fake.isr_critical_enter_count;
    return (UBaseType_t)0x5AU;
}

void R3Test_ExitCriticalFromISR(UBaseType_t saved_interrupt_status)
{
    CHECK(saved_interrupt_status == (UBaseType_t)0x5AU);
    ++fake.isr_critical_exit_count;
}

TaskHandle_t xTaskCreateStatic(
    TaskFunction_t task_code,
    const char *name,
    uint32_t stack_depth,
    void *parameters,
    UBaseType_t priority,
    StackType_t *stack_buffer,
    StaticTask_t *task_buffer)
{
    (void)task_code;
    (void)name;
    (void)stack_depth;
    (void)parameters;
    (void)priority;
    (void)stack_buffer;
    (void)task_buffer;
    ++fake.create_calls;
    if ((fake.fail_create_call != 0U) &&
        (fake.create_calls == fake.fail_create_call))
    {
        return NULL;
    }
    return (fake.create_calls == 1U) ? PROC_HANDLE : INTF_HANDLE;
}

BaseType_t xTaskNotify(
    TaskHandle_t task,
    uint32_t value,
    eNotifyAction action)
{
    CHECK(action == eSetBits);

    if (task == COMM_HANDLE)
    {
        fake.communication_notify_bits |= value;
        ++fake.communication_notify_count;
    }
    else if (task == PROC_HANDLE)
    {
        fake.processing_notify_bits |= value;
    }
    else if (task == INTF_HANDLE)
    {
        fake.interference_notify_bits |= value;
    }
    else
    {
        return pdFAIL;
    }
    return pdPASS;
}

BaseType_t xTaskNotifyFromISR(
    TaskHandle_t task,
    uint32_t value,
    eNotifyAction action,
    BaseType_t *higher_priority_task_woken)
{
    CHECK(higher_priority_task_woken != NULL);
    *higher_priority_task_woken = pdFALSE;

    if (fake.fail_notify_from_isr != 0U)
    {
        return pdFAIL;
    }

    return xTaskNotify(task, value, action);
}

BaseType_t xTaskNotifyWait(
    uint32_t clear_on_entry,
    uint32_t clear_on_exit,
    uint32_t *value,
    TickType_t ticks_to_wait)
{
    (void)clear_on_entry;
    (void)clear_on_exit;
    (void)value;
    (void)ticks_to_wait;
    return pdFAIL;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return fake.current_task;
}

/* ---- StreamRunAuthority stubs ---- */

StreamRunAuthorityStatus StreamRunAuthority_TakeReady(
    const StreamRunTicket *ticket,
    StreamOwnershipDescriptor *out_ownership)
{
    CHECK(ticket != NULL);
    CHECK(out_ownership != NULL);
    CHECK(fake.current_task == PROC_HANDLE);

    if ((fake.held_valid != 0U) ||
        (fake.processing_valid != 0U))
    {
        return STREAM_RUN_AUTHORITY_INVALID_STATE;
    }
    if (fake.ready_count == 0U)
    {
        return STREAM_RUN_AUTHORITY_EMPTY;
    }

    --fake.ready_count;
    fake.held_valid = 1U;
    *out_ownership = fake.held;
    ++fake.take_count;
    return STREAM_RUN_AUTHORITY_OK;
}

StreamRunAuthorityStatus StreamRunAuthority_CancelHeldReady(
    const StreamRunTicket *ticket,
    StreamQueueAdapterReceipt *out_receipt)
{
    CHECK(ticket != NULL);
    CHECK(out_receipt != NULL);
    CHECK(fake.current_task == PROC_HANDLE);

    if (fake.held_valid == 0U)
    {
        return STREAM_RUN_AUTHORITY_INVALID_STATE;
    }

    out_receipt->commit_serial = fake.cancel_count + 1U;
    out_receipt->operation = STREAM_QUEUE_ADAPTER_OP_CANCEL;
    out_receipt->buffer_id = fake.held.buffer_id;
    fake.held_valid = 0U;
    ++fake.cancel_count;
    return STREAM_RUN_AUTHORITY_OK;
}

StreamRunAuthorityStatus StreamRunAuthority_CompleteAndReleaseBlock(
    const StreamRunTicket *ticket,
    StreamQueueAdapterReceipt *out_receipt)
{
    CHECK(ticket != NULL);
    CHECK(out_receipt != NULL);
    CHECK(fake.current_task == PROC_HANDLE);

    if (fake.processing_valid == 0U)
    {
        return STREAM_RUN_AUTHORITY_INVALID_STATE;
    }

    out_receipt->commit_serial = fake.complete_count + 1U;
    out_receipt->operation = STREAM_QUEUE_ADAPTER_OP_COMPLETE;
    out_receipt->buffer_id = fake.processing.buffer_id;
    fake.processing_valid = 0U;
    ++fake.complete_count;
    return STREAM_RUN_AUTHORITY_OK;
}

StreamRunAuthorityStatus StreamRunAuthority_GetSnapshot(
    StreamRunAuthoritySnapshot *out)
{
    CHECK(out != NULL);

    (void)memset(out, 0, sizeof(*out));
    out->initialized = 1U;
    out->identity = fake.control.stream_ticket.identity;
    out->held_valid = fake.held_valid;
    out->held_buffer = fake.held_valid != 0U ?
        fake.held.buffer_id : R2_BUFFER_POOL_INVALID_ID;
    out->processing_valid = fake.processing_valid;
    out->processing_buffer = fake.processing_valid != 0U ?
        fake.processing.buffer_id : R2_BUFFER_POOL_INVALID_ID;
    out->live_lease_count =
        fake.ready_count + fake.held_valid + fake.processing_valid;
    return STREAM_RUN_AUTHORITY_OK;
}

/* Not called by W2-B glue; supplied so link-time mistakes are obvious if added. */
StreamRunAuthorityStatus StreamRunAuthority_ClaimHeldReady(
    const StreamRunTicket *ticket)
{
    (void)ticket;
    return STREAM_RUN_AUTHORITY_INVALID_STATE;
}

/* ---- cases ---- */

static void CaseStartBinds(void)
{
    R3WorkerTasksSnapshot snapshot;

    Reset();
    BindProcessing();
    BindInterference();

    CHECK(R3WorkerTasks_GetSnapshot(&snapshot) == R3_WORKER_TASKS_OK);
    CHECK(snapshot.processing_contract.phase ==
        R3_WORKER_PHASE_RUN_BOUND);
    CHECK(snapshot.interference_contract.phase ==
        R3_WORKER_PHASE_RUN_BOUND);
    CHECK(snapshot.faulted == 0U);
}

static void CaseT03AEmptyStop(void)
{
    R3WorkerQuiescedAck ack;

    Reset();
    BindProcessing();
    CommitStop(31U);

    fake.current_task = PROC_HANDLE;
    CHECK(R3WorkerTasks_TestServiceProcessing(
        R3_WORKER_WAKE_STOP) == R3_WORKER_TASKS_OK);
    CHECK(fake.take_count == 0U);
    CHECK(fake.cancel_count == 0U);
    CHECK(fake.complete_count == 0U);

    ack = TakeAck(R3_WORKER_PROCESSING);
    CheckAck(&ack, R3_WORKER_PROCESSING, 31U);
}

static void CaseT03BWorkStopRace(void)
{
    R3WorkerQuiescedAck ack;

    Reset();
    BindProcessing();
    AddReady(1U, (R2_BufferId)2U);
    CommitStop(32U);

    fake.current_task = PROC_HANDLE;
    CHECK(R3WorkerTasks_TestServiceProcessing(
        R3_WORKER_WAKE_WORK | R3_WORKER_WAKE_STOP) ==
        R3_WORKER_TASKS_OK);
    CHECK(fake.claim_calls == 0U);
    CHECK(fake.take_count == 1U);
    CHECK(fake.cancel_count == 1U);
    CHECK(fake.complete_count == 0U);

    ack = TakeAck(R3_WORKER_PROCESSING);
    CheckAck(&ack, R3_WORKER_PROCESSING, 32U);
}

static void CaseT03CStopBetweenTakeClaim(void)
{
    R3WorkerQuiescedAck ack;

    Reset();
    BindProcessing();
    AddReady(2U, (R2_BufferId)3U);
    fake.inject_stop_on_claim = 1U;

    fake.current_task = PROC_HANDLE;
    CHECK(R3WorkerTasks_TestServiceProcessing(
        R3_WORKER_WAKE_WORK) == R3_WORKER_TASKS_OK);
    CHECK(fake.take_count == 1U);
    CHECK(fake.claim_calls == 1U);
    CHECK(fake.cancel_count == 1U);
    CHECK(fake.complete_count == 0U);
    CHECK(fake.process_calls == 0U);

    ack = TakeAck(R3_WORKER_PROCESSING);
    CheckAck(&ack, R3_WORKER_PROCESSING, 92U);
}

static void CaseT03DStopDuringProcessing(void)
{
    R3WorkerQuiescedAck ack;

    Reset();
    BindProcessing();
    AddReady(3U, (R2_BufferId)4U);
    fake.inject_stop_during_process = 1U;

    fake.current_task = PROC_HANDLE;
    CHECK(R3WorkerTasks_TestServiceProcessing(
        R3_WORKER_WAKE_WORK) == R3_WORKER_TASKS_OK);
    CHECK(fake.claim_calls == 1U);
    CHECK(fake.process_calls == 1U);
    CHECK(fake.complete_count == 1U);
    CHECK(fake.cancel_count == 0U);
    CHECK(fake.ready_count == 0U);
    CHECK(fake.held_valid == 0U);
    CHECK(fake.processing_valid == 0U);

    ack = TakeAck(R3_WORKER_PROCESSING);
    CheckAck(&ack, R3_WORKER_PROCESSING, 94U);
}

static void CaseT05APendingStop(void)
{
    R3WorkerQuiescedAck ack;

    Reset();
    BindInterference();
    fake.read_calls = 0U;
    fake.inject_stop_on_read_call = 2U;

    fake.current_task = INTF_HANDLE;
    CHECK(R3WorkerTasks_TestServiceInterference(
        R3_WORKER_WAKE_WORK) == R3_WORKER_TASKS_OK);
    CHECK(fake.release_calls == 0U);
    CHECK(fake.segment_calls == 0U);

    ack = TakeAck(R3_WORKER_INTERFERENCE);
    CheckAck(&ack, R3_WORKER_INTERFERENCE, 91U);
}

static void CaseT05BRunningStop(void)
{
    R3WorkerQuiescedAck ack;

    Reset();
    BindInterference();
    fake.inject_stop_during_segment = 1U;

    fake.current_task = INTF_HANDLE;
    CHECK(R3WorkerTasks_TestServiceInterference(
        R3_WORKER_WAKE_WORK) == R3_WORKER_TASKS_OK);
    CHECK(fake.release_calls == 1U);
    CHECK(fake.segment_calls == 1U);

    ack = TakeAck(R3_WORKER_INTERFERENCE);
    CheckAck(&ack, R3_WORKER_INTERFERENCE, 95U);
}

static void CaseAckExactlyOnce(void)
{
    R3WorkerQuiescedAck ack;

    Reset();
    BindProcessing();
    CommitStop(41U);

    fake.current_task = PROC_HANDLE;
    CHECK(R3WorkerTasks_TestServiceProcessing(
        R3_WORKER_WAKE_STOP) == R3_WORKER_TASKS_OK);
    CHECK(fake.communication_notify_count == 1U);

    CHECK(R3WorkerTasks_TestServiceProcessing(
        R3_WORKER_WAKE_STOP | R3_WORKER_WAKE_WORK) ==
        R3_WORKER_TASKS_OK);
    CHECK(fake.communication_notify_count == 1U);

    ack = TakeAck(R3_WORKER_PROCESSING);
    CheckAck(&ack, R3_WORKER_PROCESSING, 41U);
    CHECK(R3WorkerTasks_TakeAck(
        R3_WORKER_PROCESSING, &ack) == R3_WORKER_TASKS_EMPTY);
}

static void CaseAckWakeBits(void)
{
    R3WorkerQuiescedAck ack;

    Reset();
    BindProcessing();
    BindInterference();
    CommitStop(42U);

    fake.current_task = PROC_HANDLE;
    CHECK(R3WorkerTasks_TestServiceProcessing(
        R3_WORKER_WAKE_STOP) == R3_WORKER_TASKS_OK);
    fake.current_task = INTF_HANDLE;
    CHECK(R3WorkerTasks_TestServiceInterference(
        R3_WORKER_WAKE_STOP) == R3_WORKER_TASKS_OK);

    CHECK((fake.communication_notify_bits & ACK_PROC_BIT) != 0U);
    CHECK((fake.communication_notify_bits & ACK_INTF_BIT) != 0U);
    CHECK(fake.communication_notify_count == 2U);

    ack = TakeAck(R3_WORKER_PROCESSING);
    CheckAck(&ack, R3_WORKER_PROCESSING, 42U);
    ack = TakeAck(R3_WORKER_INTERFERENCE);
    CheckAck(&ack, R3_WORKER_INTERFERENCE, 42U);
}

static void CasePostAckOldWorkClosed(void)
{
    uint32_t take_before;
    uint32_t claim_before;

    Reset();
    BindProcessing();
    CommitStop(43U);

    fake.current_task = PROC_HANDLE;
    CHECK(R3WorkerTasks_TestServiceProcessing(
        R3_WORKER_WAKE_STOP) == R3_WORKER_TASKS_OK);
    (void)TakeAck(R3_WORKER_PROCESSING);

    AddReady(8U, (R2_BufferId)5U);
    take_before = fake.take_count;
    claim_before = fake.claim_calls;

    fake.current_task = PROC_HANDLE;
    CHECK(R3WorkerTasks_TestServiceProcessing(
        R3_WORKER_WAKE_WORK) == R3_WORKER_TASKS_OK);
    CHECK(fake.take_count == take_before);
    CHECK(fake.claim_calls == claim_before);
    CHECK(fake.ready_count == 1U);
}

static void CaseNewRunRequiresAckConsumed(void)
{
    R3WorkerTasksSnapshot snapshot;

    Reset();
    BindProcessing();
    CommitStop(44U);

    fake.current_task = PROC_HANDLE;
    CHECK(R3WorkerTasks_TestServiceProcessing(
        R3_WORKER_WAKE_STOP) == R3_WORKER_TASKS_OK);

    ApplyRunToControl(12U, 22U);
    fake.current_task = PROC_HANDLE;
    CHECK(R3WorkerTasks_TestServiceProcessing(
        R3_WORKER_WAKE_START) == R3_WORKER_TASKS_ACK_BACKPRESSURE);

    CHECK(R3WorkerTasks_GetSnapshot(&snapshot) == R3_WORKER_TASKS_OK);
    CHECK(snapshot.faulted != 0U);
    CHECK(snapshot.first_fault == R3_WORKER_TASKS_ACK_BACKPRESSURE);
}

static void CaseNewRunAfterAckConsumed(void)
{
    R3WorkerTasksSnapshot snapshot;

    Reset();
    BindProcessing();
    CommitStop(45U);

    fake.current_task = PROC_HANDLE;
    CHECK(R3WorkerTasks_TestServiceProcessing(
        R3_WORKER_WAKE_STOP) == R3_WORKER_TASKS_OK);
    (void)TakeAck(R3_WORKER_PROCESSING);

    ApplyRunToControl(12U, 22U);
    fake.current_task = PROC_HANDLE;
    CHECK(R3WorkerTasks_TestServiceProcessing(
        R3_WORKER_WAKE_START) == R3_WORKER_TASKS_OK);

    CHECK(R3WorkerTasks_GetSnapshot(&snapshot) == R3_WORKER_TASKS_OK);
    CHECK(snapshot.processing_contract.phase ==
        R3_WORKER_PHASE_RUN_BOUND);
    CHECK(snapshot.processing_contract.run.run_id == 12U);
    CHECK(snapshot.processing_contract.run.generation == 22U);
    CHECK(snapshot.faulted == 0U);
}


static void CaseCreatePartialFailureNoRetry(void)
{
    R3WorkerTasksConfig config;

    (void)memset(&fake, 0, sizeof(fake));
    ApplyRunToControl(11U, 21U);
    config = Config();
    fake.fail_create_call = 2U;

    CHECK(R3WorkerTasks_Create(&config) ==
        R3_WORKER_TASKS_FREERTOS_ERROR);
    CHECK(fake.create_calls == 2U);

    fake.fail_create_call = 0U;
    CHECK(R3WorkerTasks_Create(&config) ==
        R3_WORKER_TASKS_INVALID_STATE);
    CHECK(fake.create_calls == 2U);
}

static void CaseNotifyCallerGuard(void)
{
    Reset();

    fake.current_task = OTHER_HANDLE;
    CHECK(R3WorkerTasks_NotifyStart() ==
        R3_WORKER_TASKS_INVALID_STATE);
    CHECK(R3WorkerTasks_NotifyStop() ==
        R3_WORKER_TASKS_INVALID_STATE);
    CHECK(R3WorkerTasks_NotifyInterferenceWork() ==
        R3_WORKER_TASKS_INVALID_STATE);

    fake.current_task = COMM_HANDLE;
    CHECK(R3WorkerTasks_NotifyStart() == R3_WORKER_TASKS_OK);
    CHECK((fake.processing_notify_bits & R3_WORKER_WAKE_START) != 0U);
    CHECK((fake.interference_notify_bits & R3_WORKER_WAKE_START) != 0U);
}

static void CaseIsrNotifyFailureUsesIsrCritical(void)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    uint32_t task_critical_before;

    Reset();
    fake.fail_notify_from_isr = 1U;
    task_critical_before = fake.task_critical_enter_count;

    CHECK(R3WorkerTasks_NotifyProcessingWorkFromISR(
        &higher_priority_task_woken) == R3_WORKER_TASKS_FREERTOS_ERROR);
    CHECK(fake.task_critical_enter_count == task_critical_before);
    CHECK(fake.isr_critical_enter_count == 1U);
    CHECK(fake.isr_critical_exit_count == 1U);
    CHECK(fake.critical_depth == 0U);
}

static void RunCase(const char *name)
{
    if (strcmp(name, "start_binds") == 0)
    {
        CaseStartBinds();
    }
    else if (strcmp(name, "t03a_empty_stop") == 0)
    {
        CaseT03AEmptyStop();
    }
    else if (strcmp(name, "t03b_work_stop_race") == 0)
    {
        CaseT03BWorkStopRace();
    }
    else if (strcmp(name, "t03c_stop_between_take_claim") == 0)
    {
        CaseT03CStopBetweenTakeClaim();
    }
    else if (strcmp(name, "t03d_stop_during_processing") == 0)
    {
        CaseT03DStopDuringProcessing();
    }
    else if (strcmp(name, "t05a_pending_stop") == 0)
    {
        CaseT05APendingStop();
    }
    else if (strcmp(name, "t05b_running_stop") == 0)
    {
        CaseT05BRunningStop();
    }
    else if (strcmp(name, "ack_exactly_once") == 0)
    {
        CaseAckExactlyOnce();
    }
    else if (strcmp(name, "ack_wake_bits") == 0)
    {
        CaseAckWakeBits();
    }
    else if (strcmp(name, "post_ack_old_work_closed") == 0)
    {
        CasePostAckOldWorkClosed();
    }
    else if (strcmp(name, "new_run_requires_ack_consumed") == 0)
    {
        CaseNewRunRequiresAckConsumed();
    }
    else if (strcmp(name, "new_run_after_ack_consumed") == 0)
    {
        CaseNewRunAfterAckConsumed();
    }
    else if (strcmp(name, "notify_caller_guard") == 0)
    {
        CaseNotifyCallerGuard();
    }
    else if (strcmp(name, "create_partial_failure_no_retry") == 0)
    {
        CaseCreatePartialFailureNoRetry();
    }
    else if (strcmp(name, "isr_notify_failure_uses_isr_critical") == 0)
    {
        CaseIsrNotifyFailureUsesIsrCritical();
    }
    else
    {
        (void)fprintf(stderr, "unknown case: %s\n", name);
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        (void)fprintf(stderr, "usage: %s <case>\n", argv[0]);
        return EXIT_FAILURE;
    }

    RunCase(argv[1]);
    (void)printf("PASS %s\n", argv[1]);
    return EXIT_SUCCESS;
}
