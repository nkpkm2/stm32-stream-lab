#include "r3_w3_runtime.h"

#include <stddef.h>
#include <string.h>

#include "adc_dbm_driver.h"
#include "r3_worker_tasks.h"
#include "stream_ownership_core.h"

#define R3_W3_ACK_PROCESSING_BIT (1UL << 8)
#define R3_W3_ACK_INTERFERENCE_BIT (1UL << 9)
#define R3_W3_ACK_MASK (R3_W3_ACK_PROCESSING_BIT | R3_W3_ACK_INTERFERENCE_BIT)
#define R3_W3_WORKER_WAIT_TICKS 200U

typedef struct
{
    R3W3RuntimeConfig config;
    TaskHandle_t coordinator;
    R3WorkerTaskControlView control;
    StreamRunTicket stream_ticket;
    uint32_t authority_active;
    uint32_t workers_created;
    uint32_t runtime_fault;
    uint32_t stop_serial;
    uint32_t rollback_ack_mask;
    uint32_t processing_entered;
    uint32_t stop_report_valid;
    AdcDbmDriverStopReport stop_report;
} R3W3RuntimeStorage;

static R3W3RuntimeStorage runtime;
/* DMA-visible storage is deliberately static SRAM, never task stack memory. */
static uint16_t sample_blocks[R2_BUFFER_POOL_MAX_BUFFERS][R3_W3_RUNTIME_BLOCK_SAMPLES];

static int CoordinatorCaller(void)
{
    return (runtime.coordinator != NULL) &&
        (xTaskGetCurrentTaskHandle() == runtime.coordinator);
}

static void LatchFault(void)
{
    runtime.runtime_fault = 1U;
}

static R3LifecycleStatus LifecycleFromDriver(AdcDbmDriverStatus status)
{
    return status == ADC_DBM_DRIVER_OK ? R3_LIFECYCLE_OK : R3_LIFECYCLE_ARM_FAILED;
}

static R3WorkerTasksStatus ReadControl(void *context, R3WorkerTaskControlView *out_view)
{
    R3LifecycleSnapshot lifecycle;
    (void)context;
    if ((out_view == NULL) ||
        (R3Lifecycle_GetSnapshot(&lifecycle) != R3_LIFECYCLE_OK))
    {
        return R3_WORKER_TASKS_CONTROL_ERROR;
    }
    taskENTER_CRITICAL();
    *out_view = runtime.control;
    out_view->processing_claim_allowed = lifecycle.processing_claim_allowed;
    out_view->interference_release_allowed = lifecycle.interference_release_allowed;
    taskEXIT_CRITICAL();
    return R3_WORKER_TASKS_OK;
}

static R3WorkerClaimDecision TryClaimHeld(
    void *context,
    const R3WorkerRunKey *run,
    const StreamRunTicket *ticket,
    R3WorkerStopContext *out_stop)
{
    R3WorkerTaskControlView view;
    StreamRunAuthorityStatus authority_status;
    (void)context;
    if ((run == NULL) || (ticket == NULL) || (out_stop == NULL) ||
        (ReadControl(NULL, &view) != R3_WORKER_TASKS_OK))
    {
        return R3_WORKER_CLAIM_ERROR;
    }
    taskENTER_CRITICAL();
    if ((view.run_valid == 0U) ||
        (view.run.boot_id != run->boot_id) ||
        (view.run.run_id != run->run_id) ||
        (view.run.generation != run->generation) ||
        (view.stream_ticket.identity.boot_id != ticket->identity.boot_id) ||
        (view.stream_ticket.identity.run_id != ticket->identity.run_id) ||
        (view.stream_ticket.identity.generation != ticket->identity.generation))
    {
        taskEXIT_CRITICAL();
        return R3_WORKER_CLAIM_STALE;
    }
    if (view.stop_valid != 0U)
    {
        *out_stop = view.stop;
        taskEXIT_CRITICAL();
        return R3_WORKER_CLAIM_STOP_CURRENT;
    }
    if (view.processing_claim_allowed == 0U)
    {
        taskEXIT_CRITICAL();
        return R3_WORKER_CLAIM_CLOSED;
    }
    authority_status = StreamRunAuthority_ClaimHeldReady(ticket);
    taskEXIT_CRITICAL();
    return authority_status == STREAM_RUN_AUTHORITY_OK ?
        R3_WORKER_CLAIM_GRANTED : R3_WORKER_CLAIM_ERROR;
}

static R3InterferenceReleaseDecision TryBeginInterference(
    void *context,
    const R3WorkerRunKey *run,
    R3WorkerStopContext *out_stop)
{
    R3WorkerTaskControlView view;
    (void)context;
    if ((run == NULL) || (out_stop == NULL) ||
        (ReadControl(NULL, &view) != R3_WORKER_TASKS_OK))
    {
        return R3_INTERFERENCE_RELEASE_ERROR;
    }
    if ((view.run_valid == 0U) ||
        (view.run.boot_id != run->boot_id) ||
        (view.run.run_id != run->run_id) ||
        (view.run.generation != run->generation))
    {
        return R3_INTERFERENCE_RELEASE_STALE;
    }
    if (view.stop_valid != 0U)
    {
        *out_stop = view.stop;
        return R3_INTERFERENCE_RELEASE_STOP_CURRENT;
    }
    return view.interference_release_allowed != 0U ?
        R3_INTERFERENCE_RELEASE_GRANTED : R3_INTERFERENCE_RELEASE_CLOSED;
}

static void ProcessBlock(void *context, const StreamOwnershipDescriptor *ownership)
{
    (void)context;
    (void)ownership;
    runtime.processing_entered = 1U;
    if (runtime.config.processing_hold_ticks != 0U)
    {
        vTaskDelay(runtime.config.processing_hold_ticks);
    }
}

static uint32_t InterferenceSegment(void *context)
{
    (void)context;
    return 0U;
}

static void CompleteCallback(const AdcDbmDriverCompletionEvent *event, void *context)
{
    AdcDbmDriverInactiveRequest request;
    AdcDbmDriverStatus driver_status;
    StreamOwnershipCompletionPlan plan;
    StreamOwnershipDescriptor descriptor;
    StreamOwnershipStatus ownership_status;
    StreamRunAuthorityStatus authority_status;
    R2_BufferId replacement;
    BaseType_t higher_priority_task_woken = pdFALSE;
    (void)context;
    if ((event == NULL) || (runtime.authority_active == 0U))
    {
        LatchFault();
        return;
    }

    authority_status = StreamRunAuthority_TakeFreeFromISR(
        &runtime.stream_ticket, &replacement, &higher_priority_task_woken);
    if (authority_status == STREAM_RUN_AUTHORITY_EMPTY)
    {
        /* No token is a controlled capacity drop.  KEEP makes the no-MxAR
         * write explicit and preserves the current K+2 mapping. */
        ownership_status = StreamOwnership_PrepareCompletion(
            event->sequence, (uint32_t)event->completed_slot,
            STREAM_OWNERSHIP_KEEP, R2_BUFFER_POOL_INVALID_ID, &plan);
    }
    else if (authority_status == STREAM_RUN_AUTHORITY_OK)
    {
        ownership_status = StreamOwnership_PrepareCompletion(
            event->sequence, (uint32_t)event->completed_slot,
            STREAM_OWNERSHIP_REBIND, replacement, &plan);
    }
    else
    {
        (void)AdcDbmDriver_FailActiveCompletion(event);
        LatchFault();
        return;
    }
    if (ownership_status != STREAM_OWNERSHIP_OK)
    {
        (void)AdcDbmDriver_FailActiveCompletion(event);
        LatchFault();
        return;
    }
    (void)memset(&request, 0, sizeof(request));
    request.sequence = event->sequence;
    request.completed_slot = event->completed_slot;
    request.action = plan.action == STREAM_OWNERSHIP_REBIND ?
        ADC_DBM_DRIVER_INACTIVE_REBIND : ADC_DBM_DRIVER_INACTIVE_KEEP;
    request.expected_completed_address =
        (uint32_t)(uintptr_t)sample_blocks[plan.completed_buffer];
    request.expected_active_address =
        (uint32_t)(uintptr_t)sample_blocks[plan.active_buffer];
    if (plan.action == STREAM_OWNERSHIP_REBIND)
    {
        request.replacement_address =
            (uint32_t)(uintptr_t)sample_blocks[plan.replacement_buffer];
    }
    driver_status = AdcDbmDriver_ApplyInactiveAction(event, &request, NULL);
    if (driver_status != ADC_DBM_DRIVER_OK)
    {
        (void)AdcDbmDriver_FailActiveCompletion(event);
        LatchFault();
        return;
    }
    if (plan.action == STREAM_OWNERSHIP_KEEP)
    {
        ownership_status = StreamOwnership_CommitKeep(&plan);
    }
    else
    {
        ownership_status = StreamOwnership_CommitRebind(&plan, &descriptor);
        if (ownership_status == STREAM_OWNERSHIP_OK)
        {
            authority_status = StreamRunAuthority_PublishReadyFromISR(
                &runtime.stream_ticket, &descriptor, &higher_priority_task_woken);
            if ((authority_status == STREAM_RUN_AUTHORITY_OK) &&
                (runtime.config.suppress_processing_notify == 0U))
            {
                if (R3WorkerTasks_NotifyProcessingWorkFromISR(
                        &higher_priority_task_woken) != R3_WORKER_TASKS_OK)
                {
                    authority_status = STREAM_RUN_AUTHORITY_ADAPTER_ERROR;
                }
            }
        }
        if (authority_status != STREAM_RUN_AUTHORITY_OK)
        {
            ownership_status = STREAM_OWNERSHIP_POST_COMMIT_INVARIANT;
        }
    }
    if (ownership_status != STREAM_OWNERSHIP_OK)
    {
        (void)AdcDbmDriver_FailActiveCompletion(event);
        LatchFault();
    }
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static void ErrorCallback(uint32_t dma_error, uint32_t adc_status, void *context)
{
    (void)dma_error;
    (void)adc_status;
    (void)context;
    LatchFault();
}

static int WaitForWorkersBound(void)
{
    R3WorkerTasksSnapshot workers;
    uint32_t tick;
    for (tick = 0U; tick < R3_W3_WORKER_WAIT_TICKS; ++tick)
    {
        if ((R3WorkerTasks_GetSnapshot(&workers) == R3_WORKER_TASKS_OK) &&
            (workers.processing_contract.phase == R3_WORKER_PHASE_RUN_BOUND) &&
            (workers.interference_contract.phase == R3_WORKER_PHASE_RUN_BOUND))
        {
            return 1;
        }
        vTaskDelay(1U);
    }
    return 0;
}

static int WaitForRollbackAcks(void)
{
    uint32_t tick;
    uint32_t bits;
    R3WorkerQuiescedAck ack;
    for (tick = 0U; tick < R3_W3_WORKER_WAIT_TICKS; ++tick)
    {
        bits = 0U;
        (void)xTaskNotifyWait(0U, R3_W3_ACK_MASK, &bits, 1U);
        runtime.rollback_ack_mask |= bits & R3_W3_ACK_MASK;
        if (runtime.rollback_ack_mask == R3_W3_ACK_MASK)
        {
            break;
        }
    }
    if (runtime.rollback_ack_mask != R3_W3_ACK_MASK)
    {
        return 0;
    }
    if ((R3WorkerTasks_TakeAck(R3_WORKER_PROCESSING, &ack) != R3_WORKER_TASKS_OK) ||
        (ack.boot_id != runtime.control.run.boot_id) ||
        (ack.run_id != runtime.control.run.run_id) ||
        (ack.generation != runtime.control.run.generation) ||
        (ack.stop_id != runtime.control.stop.stop_id))
    {
        return 0;
    }
    if ((R3WorkerTasks_TakeAck(R3_WORKER_INTERFERENCE, &ack) != R3_WORKER_TASKS_OK) ||
        (ack.boot_id != runtime.control.run.boot_id) ||
        (ack.run_id != runtime.control.run.run_id) ||
        (ack.generation != runtime.control.run.generation) ||
        (ack.stop_id != runtime.control.stop.stop_id))
    {
        return 0;
    }
    return 1;
}

static R3LifecycleStatus HookValidate(void *context, const R3LifecycleStartRequest *request)
{
    (void)context;
    if ((request == NULL) || (request->configuration_id != runtime.config.k) ||
        (runtime.runtime_fault != 0U))
    {
        return R3_LIFECYCLE_INVALID_CONFIG;
    }
    return R3_LIFECYCLE_OK;
}

static R3LifecycleStatus HookPrepare(
    void *context, const R3LifecycleStartRequest *request, const StreamRunTicket *ticket)
{
    R3WorkerTasksSnapshot workers;
    StreamRunIdentity identity;
    (void)context;
    if ((request == NULL) || (ticket == NULL) ||
        (StreamOwnership_Initialize(runtime.config.k) != STREAM_OWNERSHIP_OK))
    {
        return R3_LIFECYCLE_PREPARE_FAILED;
    }
    identity = ticket->identity;
    if (StreamRunAuthority_Initialize(runtime.config.k, &identity, &runtime.stream_ticket) !=
        STREAM_RUN_AUTHORITY_OK)
    {
        StreamOwnership_ResetOffline();
        return R3_LIFECYCLE_PREPARE_FAILED;
    }
    runtime.authority_active = 1U;
    if ((R3WorkerTasks_GetSnapshot(&workers) != R3_WORKER_TASKS_OK) ||
        (StreamRunAuthority_BindProcessingTask(&runtime.stream_ticket,
            workers.processing_task) != STREAM_RUN_AUTHORITY_OK))
    {
        return R3_LIFECYCLE_PREPARE_FAILED;
    }
    taskENTER_CRITICAL();
    runtime.control.run_valid = 1U;
    runtime.control.run.boot_id = request->boot_id;
    runtime.control.run.run_id = request->request_id;
    runtime.control.run.generation = request->generation;
    runtime.control.stream_ticket = runtime.stream_ticket;
    runtime.control.stop_valid = 0U;
    taskEXIT_CRITICAL();
    if ((R3WorkerTasks_NotifyStart() != R3_WORKER_TASKS_OK) || !WaitForWorkersBound())
    {
        return R3_LIFECYCLE_PREPARE_FAILED;
    }
    return R3_LIFECYCLE_OK;
}

static R3LifecycleStatus HookArm(
    void *context, const R3LifecycleStartRequest *request, const StreamRunTicket *ticket)
{
    AdcDbmDriverArmConfig arm;
    (void)context;
    (void)request;
    (void)ticket;
    AdcDbmDriver_Init();
    if (runtime.config.inject_arm_failure != 0U)
    {
        return R3_LIFECYCLE_ARM_FAILED;
    }
    (void)memset(&arm, 0, sizeof(arm));
    arm.m0_address = (uint32_t)(uintptr_t)sample_blocks[0];
    arm.m1_address = (uint32_t)(uintptr_t)sample_blocks[1];
    arm.block_samples = R3_W3_RUNTIME_BLOCK_SAMPLES;
    arm.complete_callback = CompleteCallback;
    arm.error_callback = ErrorCallback;
    arm.callback_context = NULL;
    return LifecycleFromDriver(AdcDbmDriver_Arm(&arm));
}

static R3LifecycleStatus HookCommit(void *context, const StreamRunTicket *ticket)
{
    (void)context;
    (void)ticket;
    if (runtime.config.inject_commit_failure != 0U)
    {
        return R3_LIFECYCLE_COMMIT_FAILED;
    }
    return AdcDbmDriver_CommitStart() == ADC_DBM_DRIVER_OK ?
        R3_LIFECYCLE_OK : R3_LIFECYCLE_COMMIT_FAILED;
}

static R3LifecycleStatus HookRollback(void *context, const StreamRunTicket *ticket)
{
    AdcDbmDriverSnapshot driver;
    (void)context;
    (void)ticket;
    if (AdcDbmDriver_GetSnapshot(&driver) != ADC_DBM_DRIVER_OK)
    {
        return R3_LIFECYCLE_ROLLBACK_FAILED;
    }
    if (driver.hardware_owned != 0U &&
        AdcDbmDriver_Stop(&runtime.stop_report) != ADC_DBM_DRIVER_OK)
    {
        return R3_LIFECYCLE_ROLLBACK_FAILED;
    }
    runtime.stop_report_valid = driver.hardware_owned != 0U;
    if (runtime.control.run_valid != 0U)
    {
        taskENTER_CRITICAL();
        ++runtime.stop_serial;
        if (runtime.stop_serial == 0U) ++runtime.stop_serial;
        runtime.control.stop_valid = 1U;
        runtime.control.stop.run = runtime.control.run;
        runtime.control.stop.stop_id = runtime.stop_serial;
        runtime.rollback_ack_mask = 0U;
        taskEXIT_CRITICAL();
        if ((R3WorkerTasks_NotifyStop() != R3_WORKER_TASKS_OK) ||
            !WaitForRollbackAcks())
        {
            return R3_LIFECYCLE_ROLLBACK_FAILED;
        }
    }
    if (runtime.authority_active != 0U)
    {
        if (StreamRunAuthority_ResetOffline(&runtime.stream_ticket) !=
            STREAM_RUN_AUTHORITY_OK)
        {
            return R3_LIFECYCLE_ROLLBACK_FAILED;
        }
        runtime.authority_active = 0U;
    }
    StreamOwnership_ResetOffline();
    taskENTER_CRITICAL();
    (void)memset(&runtime.control, 0, sizeof(runtime.control));
    taskEXIT_CRITICAL();
    return R3_LIFECYCLE_OK;
}

R3W3RuntimeStatus R3W3Runtime_Initialize(const R3W3RuntimeConfig *config)
{
    R3WorkerTasksConfig workers;
    R3LifecycleHooks hooks;
    if ((config == NULL) || (xTaskGetCurrentTaskHandle() == NULL) ||
        ((config->k != 1U) && (config->k != 2U) &&
         (config->k != 4U) && (config->k != 8U)))
    {
        return R3_W3_RUNTIME_INVALID_ARGUMENT;
    }
    if (runtime.config.boot_id != 0U)
    {
        return R3_W3_RUNTIME_INVALID_STATE;
    }
    (void)memset(&runtime, 0, sizeof(runtime));
    runtime.config = *config;
    runtime.coordinator = xTaskGetCurrentTaskHandle();
    (void)memset(&workers, 0, sizeof(workers));
    workers.communication_task = runtime.coordinator;
    workers.processing_ack_notify_bit = R3_W3_ACK_PROCESSING_BIT;
    workers.interference_ack_notify_bit = R3_W3_ACK_INTERFERENCE_BIT;
    workers.hooks.read_control = ReadControl;
    workers.hooks.try_claim_held = TryClaimHeld;
    workers.hooks.try_begin_interference = TryBeginInterference;
    workers.hooks.process_block = ProcessBlock;
    workers.hooks.interference_segment = InterferenceSegment;
    if (R3WorkerTasks_Create(&workers) != R3_WORKER_TASKS_OK)
    {
        return R3_W3_RUNTIME_WORKER_ERROR;
    }
    runtime.workers_created = 1U;
    (void)memset(&hooks, 0, sizeof(hooks));
    hooks.validate = HookValidate;
    hooks.prepare = HookPrepare;
    hooks.arm = HookArm;
    hooks.commit_timer = HookCommit;
    hooks.rollback = HookRollback;
    R3Lifecycle_Init(config->boot_id, &hooks);
    return R3_W3_RUNTIME_OK;
}

R3W3RuntimeStatus R3W3Runtime_PrepareStart(
    const R3LifecycleStartRequest *request, R3LifecycleStartTicket *out_ticket)
{
    R3LifecycleStatus status;
    if (!CoordinatorCaller()) return R3_W3_RUNTIME_INVALID_STATE;
    status = R3Lifecycle_PrepareStart(request, out_ticket);
    return status == R3_LIFECYCLE_OK ? R3_W3_RUNTIME_OK :
        (status == R3_LIFECYCLE_STATUS_RESET_REQUIRED ?
         R3_W3_RUNTIME_RESET_REQUIRED : R3_W3_RUNTIME_LIFECYCLE_ERROR);
}

R3W3RuntimeStatus R3W3Runtime_CommitStart(const R3LifecycleStartTicket *ticket)
{
    R3LifecycleStatus status;
    if (!CoordinatorCaller()) return R3_W3_RUNTIME_INVALID_STATE;
    status = R3Lifecycle_CommitStart(ticket);
    return status == R3_LIFECYCLE_OK ? R3_W3_RUNTIME_OK :
        (status == R3_LIFECYCLE_STATUS_RESET_REQUIRED ?
         R3_W3_RUNTIME_RESET_REQUIRED : R3_W3_RUNTIME_LIFECYCLE_ERROR);
}

R3W3RuntimeStatus R3W3Runtime_StopBeforeCommit(const R3LifecycleStartTicket *ticket)
{
    R3LifecycleStatus status;
    if (!CoordinatorCaller()) return R3_W3_RUNTIME_INVALID_STATE;
    status = R3Lifecycle_RequestStopBeforeCommit(ticket);
    return status == R3_LIFECYCLE_STOPPED ? R3_W3_RUNTIME_OK :
        (status == R3_LIFECYCLE_STATUS_RESET_REQUIRED ?
         R3_W3_RUNTIME_RESET_REQUIRED : R3_W3_RUNTIME_LIFECYCLE_ERROR);
}

R3W3RuntimeStatus R3W3Runtime_StopRunning(uint32_t stop_id)
{
    R3LifecycleStopRequest request;
    R3LifecycleStatus status;
    if (!CoordinatorCaller() || (stop_id == 0U) || (runtime.control.run_valid == 0U))
    {
        return R3_W3_RUNTIME_INVALID_STATE;
    }
    (void)memset(&request, 0, sizeof(request));
    request.stream_ticket = runtime.stream_ticket;
    request.stop_id = stop_id;
    status = R3Lifecycle_RequestStop(&request);
    return status == R3_LIFECYCLE_STOPPED ? R3_W3_RUNTIME_OK :
        (status == R3_LIFECYCLE_STATUS_RESET_REQUIRED ?
         R3_W3_RUNTIME_RESET_REQUIRED : R3_W3_RUNTIME_LIFECYCLE_ERROR);
}

R3W3RuntimeStatus R3W3Runtime_GetSnapshot(R3W3RuntimeSnapshot *out)
{
    if ((out == NULL) || (runtime.config.boot_id == 0U))
    {
        return R3_W3_RUNTIME_INVALID_ARGUMENT;
    }
    (void)memset(out, 0, sizeof(*out));
    out->initialized = 1U;
    out->coordinator_valid = runtime.coordinator != NULL;
    out->current_run_valid = runtime.control.run_valid;
    out->current_stop_valid = runtime.control.stop_valid;
    out->rollback_ack_mask = runtime.rollback_ack_mask;
    out->runtime_fault = runtime.runtime_fault;
    out->processing_entered = runtime.processing_entered;
    out->stop_report_valid = runtime.stop_report_valid;
    out->stop_report = runtime.stop_report;
    if ((R3Lifecycle_GetSnapshot(&out->lifecycle) != R3_LIFECYCLE_OK) ||
        (AdcDbmDriver_GetSnapshot(&out->driver) != ADC_DBM_DRIVER_OK) ||
        (StreamOwnership_GetSnapshot(&out->ownership) != STREAM_OWNERSHIP_OK) ||
        (R3WorkerTasks_GetSnapshot(&out->workers) != R3_WORKER_TASKS_OK))
    {
        return R3_W3_RUNTIME_INVALID_STATE;
    }
    if (runtime.authority_active != 0U &&
        StreamRunAuthority_GetSnapshot(&out->authority) != STREAM_RUN_AUTHORITY_OK)
    {
        return R3_W3_RUNTIME_INVALID_STATE;
    }
    return R3_W3_RUNTIME_OK;
}
