#include "stream_queue_adapter.h"

#include "queue.h"

#include "stm32f4xx.h"

#include <stddef.h>
#include <string.h>

typedef enum
{
    ADAPTER_PHASE_RESET = 0,
    ADAPTER_PHASE_INITIALIZING,
    ADAPTER_PHASE_READY,
    ADAPTER_PHASE_FAULTED
} AdapterPhase;

typedef struct
{
    uint32_t active;
    StreamQueueAdapterOperation operation;
    R2_BufferId buffer_id;
    TaskHandle_t caller_task;
    StreamQueueAdapterPermit permit;
    uint32_t have_ready_descriptor;
    StreamQueueAdapterReadyDescriptor ready_descriptor;
    uint32_t hook_seen;
    StreamQueueAdapterStatus hook_status;
    uint32_t commit_serial;
} FreeSendContext;

typedef struct
{
    volatile AdapterPhase phase;
    uint32_t k;
    StreamQueueAdapterAuthorizeFn authorizer;
    void *authorizer_context;
    TaskHandle_t processing_task;

    QueueHandle_t free_queue;
    QueueHandle_t ready_queue;
    StaticQueue_t free_queue_control;
    StaticQueue_t ready_queue_control;
    uint8_t free_queue_storage[
        STREAM_QUEUE_ADAPTER_MAX_K * sizeof(R2_BufferId)];
    uint8_t ready_queue_storage[
        STREAM_QUEUE_ADAPTER_MAX_K *
        sizeof(StreamQueueAdapterReadyDescriptor)];

    FreeSendContext send_context;

    uint32_t held_ready_valid;
    StreamQueueAdapterReadyDescriptor held_ready_descriptor;

    uint32_t processing_lease_valid;
    StreamQueueAdapterReadyDescriptor processing_lease_descriptor;

    StreamQueueAdapterStatus first_fault;

    uint32_t init_commit_count;
    uint32_t cancel_commit_count;
    uint32_t complete_commit_count;
    uint32_t ready_publish_count;
    uint32_t free_take_count;
    uint32_t ready_take_count;
    uint32_t ready_claim_count;

    uint32_t hook_call_count;
    uint32_t illegal_hook_count;
    uint32_t queue_failure_count;
    uint32_t auth_failure_count;
    uint32_t owner_failure_count;
    uint32_t ledger_failure_count;
    uint32_t failure_count;
    uint32_t last_commit_serial;
} StreamQueueAdapterStorage;

static StreamQueueAdapterStorage adapter;

static int SupportedK(uint32_t k)
{
    return (k == 1U) || (k == 2U) || (k == 4U) || (k == 8U);
}

static StreamQueueAdapterStatus LatchFault(
    StreamQueueAdapterStatus status)
{
    if (adapter.phase != ADAPTER_PHASE_FAULTED)
    {
        adapter.first_fault = status;
        adapter.phase = ADAPTER_PHASE_FAULTED;
    }

    if (adapter.failure_count != UINT32_MAX)
    {
        ++adapter.failure_count;
    }

    return status;
}

static StreamQueueAdapterStatus RequireReady(void)
{
    if (adapter.phase == ADAPTER_PHASE_FAULTED)
    {
        return STREAM_QUEUE_ADAPTER_FAULTED;
    }
    if (adapter.phase != ADAPTER_PHASE_READY)
    {
        return STREAM_QUEUE_ADAPTER_INVALID_STATE;
    }
    return STREAM_QUEUE_ADAPTER_OK;
}

static int ProcessingCaller(void)
{
    return (adapter.processing_task != NULL) &&
        (xTaskGetCurrentTaskHandle() == adapter.processing_task);
}

static int Authorize(
    StreamQueueAdapterOperation operation,
    R2_BufferId id,
    const StreamQueueAdapterReadyDescriptor *ready_descriptor,
    const StreamQueueAdapterPermit *permit)
{
    if ((adapter.authorizer == NULL) || (permit == NULL))
    {
        return 0;
    }

    return adapter.authorizer(
        operation,
        id,
        ready_descriptor,
        permit,
        adapter.authorizer_context) != 0;
}

static void ClearSendContext(void)
{
    (void)memset(&adapter.send_context, 0, sizeof(adapter.send_context));
    adapter.send_context.buffer_id = R2_BUFFER_POOL_INVALID_ID;
    adapter.send_context.hook_status = STREAM_QUEUE_ADAPTER_INVALID_STATE;
}

static void ClearHeldReady(void)
{
    (void)memset(
        &adapter.held_ready_descriptor,
        0,
        sizeof(adapter.held_ready_descriptor));
    adapter.held_ready_descriptor.ownership.buffer_id =
        R2_BUFFER_POOL_INVALID_ID;
    adapter.held_ready_valid = 0U;
}

static void ClearProcessingLease(void)
{
    (void)memset(
        &adapter.processing_lease_descriptor,
        0,
        sizeof(adapter.processing_lease_descriptor));
    adapter.processing_lease_descriptor.ownership.buffer_id =
        R2_BUFFER_POOL_INVALID_ID;
    adapter.processing_lease_valid = 0U;
}

static int OrdinaryTaskContext(void)
{
    if ((__get_IPSR() != 0U) ||
        (__get_PRIMASK() != 0U) ||
        (__get_BASEPRI() != 0U))
    {
        return 0;
    }

#if ( INCLUDE_xTaskGetSchedulerState == 1 )
    return xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
#else
    return 0;
#endif
}

static StreamQueueAdapterStatus BeginAuthorizedTaskOperation(
    StreamQueueAdapterOperation operation,
    R2_BufferId id,
    const StreamQueueAdapterReadyDescriptor *ready_descriptor,
    const StreamQueueAdapterPermit *permit)
{
    StreamQueueAdapterStatus status = RequireReady();

    if (status != STREAM_QUEUE_ADAPTER_OK)
    {
        return status;
    }
    if (!OrdinaryTaskContext())
    {
        ++adapter.auth_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_CALLER_CONTEXT_ERROR);
    }
    if (!ProcessingCaller())
    {
        ++adapter.auth_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_WRONG_TASK);
    }
    if (!Authorize(operation, id, ready_descriptor, permit))
    {
        ++adapter.auth_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_AUTH_REJECTED);
    }
    return STREAM_QUEUE_ADAPTER_OK;
}

static StreamQueueAdapterStatus CommitSerial(uint32_t *out_serial)
{
    uint32_t next = adapter.last_commit_serial + 1U;

    if (next == 0U)
    {
        return LatchFault(STREAM_QUEUE_ADAPTER_HOOK_REJECTED);
    }

    adapter.last_commit_serial = next;
    *out_serial = next;
    return STREAM_QUEUE_ADAPTER_OK;
}

static StreamQueueAdapterStatus SendFreeInternal(
    StreamQueueAdapterOperation operation,
    R2_BufferId id,
    const StreamQueueAdapterReadyDescriptor *ready_descriptor,
    const StreamQueueAdapterPermit *permit,
    StreamQueueAdapterReceipt *out_receipt)
{
    BaseType_t queue_result;
    StreamQueueAdapterStatus status;
    TaskHandle_t caller = NULL;

    if (adapter.free_queue == NULL)
    {
        return LatchFault(STREAM_QUEUE_ADAPTER_INVALID_STATE);
    }
    if (adapter.send_context.active != 0U)
    {
        ++adapter.illegal_hook_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_INVALID_STATE);
    }

    if (operation == STREAM_QUEUE_ADAPTER_OP_INIT)
    {
        if (adapter.phase != ADAPTER_PHASE_INITIALIZING)
        {
            return LatchFault(STREAM_QUEUE_ADAPTER_INVALID_STATE);
        }
    }
    else
    {
        status = BeginAuthorizedTaskOperation(
            operation, id, ready_descriptor, permit);
        if (status != STREAM_QUEUE_ADAPTER_OK)
        {
            return status;
        }
        caller = xTaskGetCurrentTaskHandle();
    }

    ClearSendContext();
    adapter.send_context.active = 1U;
    adapter.send_context.operation = operation;
    adapter.send_context.buffer_id = id;
    adapter.send_context.caller_task = caller;
    if (permit != NULL)
    {
        adapter.send_context.permit = *permit;
    }
    if (ready_descriptor != NULL)
    {
        adapter.send_context.have_ready_descriptor = 1U;
        adapter.send_context.ready_descriptor = *ready_descriptor;
    }

    queue_result = xQueueSend(adapter.free_queue, &id, 0U);

    status = adapter.send_context.hook_status;
    if (queue_result != pdPASS)
    {
        ++adapter.queue_failure_count;
        status = LatchFault(STREAM_QUEUE_ADAPTER_QUEUE_ERROR);
    }
    else if (adapter.send_context.hook_seen == 0U)
    {
        status = LatchFault(STREAM_QUEUE_ADAPTER_HOOK_MISSING);
    }
    else if (status != STREAM_QUEUE_ADAPTER_OK)
    {
        status = LatchFault(STREAM_QUEUE_ADAPTER_HOOK_REJECTED);
    }
    else if ((adapter.phase != ADAPTER_PHASE_INITIALIZING) &&
             (adapter.phase != ADAPTER_PHASE_READY))
    {
        status = STREAM_QUEUE_ADAPTER_FAULTED;
    }

    if ((status == STREAM_QUEUE_ADAPTER_OK) &&
        (out_receipt != NULL))
    {
        out_receipt->commit_serial = adapter.send_context.commit_serial;
        out_receipt->operation = operation;
        out_receipt->buffer_id = id;
    }

    ClearSendContext();
    return status;
}

static StreamQueueAdapterStatus ValidateInitialStable(void)
{
    R2_BufferPoolSnapshot pool;
    StreamTokenStatus token_status;

    if (R2_BufferPool_GetSnapshot(&pool) != R2_BUFFER_POOL_OK)
    {
        ++adapter.owner_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_OWNER_ERROR);
    }

    token_status = StreamTokenLedger_ValidateStable(&pool);
    if (token_status != STREAM_TOKEN_OK)
    {
        ++adapter.ledger_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_LEDGER_ERROR);
    }

    if ((adapter.free_queue == NULL) ||
        (adapter.ready_queue == NULL) ||
        ((uint32_t)uxQueueMessagesWaiting(adapter.free_queue) != pool.free_count) ||
        ((uint32_t)uxQueueMessagesWaiting(adapter.ready_queue) != pool.ready_count))
    {
        return LatchFault(STREAM_QUEUE_ADAPTER_RECONCILE_ERROR);
    }

    return STREAM_QUEUE_ADAPTER_OK;
}

StreamQueueAdapterStatus StreamQueueAdapter_Initialize(
    uint32_t k,
    StreamQueueAdapterAuthorizeFn authorizer,
    void *authorizer_context)
{
    StreamOwnershipSnapshot ownership;
    uint32_t id;

    if ((adapter.phase != ADAPTER_PHASE_RESET) ||
        !SupportedK(k) ||
        (authorizer == NULL))
    {
        return STREAM_QUEUE_ADAPTER_INVALID_ARGUMENT;
    }

    if ((StreamOwnership_GetSnapshot(&ownership) != STREAM_OWNERSHIP_OK) ||
        (ownership.initialized != 1U) ||
        (ownership.faulted != 0U) ||
        (ownership.k != k))
    {
        return STREAM_QUEUE_ADAPTER_INVALID_STATE;
    }

    (void)memset(&adapter, 0, sizeof(adapter));
    adapter.k = k;
    adapter.authorizer = authorizer;
    adapter.authorizer_context = authorizer_context;
    adapter.first_fault = STREAM_QUEUE_ADAPTER_OK;
    adapter.send_context.buffer_id = R2_BUFFER_POOL_INVALID_ID;
    ClearHeldReady();
    ClearProcessingLease();

    adapter.free_queue = xQueueCreateStatic(
        k,
        sizeof(R2_BufferId),
        adapter.free_queue_storage,
        &adapter.free_queue_control);

    adapter.ready_queue = xQueueCreateStatic(
        k,
        sizeof(StreamQueueAdapterReadyDescriptor),
        adapter.ready_queue_storage,
        &adapter.ready_queue_control);

    if ((adapter.free_queue == NULL) || (adapter.ready_queue == NULL))
    {
        ++adapter.queue_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_QUEUE_ERROR);
    }

    vQueueAddToRegistry(adapter.free_queue, "F0Free");
    vQueueAddToRegistry(adapter.ready_queue, "F0Ready");

    StreamTokenLedger_ResetOffline();
    if (StreamTokenLedger_Initialize(k) != STREAM_TOKEN_OK)
    {
        ++adapter.ledger_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_LEDGER_ERROR);
    }

    adapter.phase = ADAPTER_PHASE_INITIALIZING;

    for (id = 2U; id < k + 2U; ++id)
    {
        if (SendFreeInternal(
                STREAM_QUEUE_ADAPTER_OP_INIT,
                (R2_BufferId)id,
                NULL,
                NULL,
                NULL) != STREAM_QUEUE_ADAPTER_OK)
        {
            return STREAM_QUEUE_ADAPTER_FAULTED;
        }
    }

    if (StreamTokenLedger_SealInitialization() != STREAM_TOKEN_OK)
    {
        ++adapter.ledger_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_LEDGER_ERROR);
    }

    if (ValidateInitialStable() != STREAM_QUEUE_ADAPTER_OK)
    {
        return STREAM_QUEUE_ADAPTER_FAULTED;
    }

    adapter.phase = ADAPTER_PHASE_READY;
    return STREAM_QUEUE_ADAPTER_OK;
}

StreamQueueAdapterStatus StreamQueueAdapter_ResetOffline(void)
{
    R2_BufferPoolSnapshot pool;
    StreamQueueAdapterStatus status;

    if (adapter.phase == ADAPTER_PHASE_RESET)
    {
        return STREAM_QUEUE_ADAPTER_OK;
    }
    if (adapter.phase == ADAPTER_PHASE_FAULTED)
    {
        return STREAM_QUEUE_ADAPTER_FAULTED;
    }
    if (adapter.phase != ADAPTER_PHASE_READY)
    {
        return STREAM_QUEUE_ADAPTER_INVALID_STATE;
    }
    if ((adapter.send_context.active != 0U) ||
        (adapter.held_ready_valid != 0U) ||
        (adapter.processing_lease_valid != 0U))
    {
        return LatchFault(STREAM_QUEUE_ADAPTER_INVALID_STATE);
    }

    status = StreamQueueAdapter_ValidateStable();
    if (status != STREAM_QUEUE_ADAPTER_OK)
    {
        return status;
    }

    if ((R2_BufferPool_GetSnapshot(&pool) != R2_BUFFER_POOL_OK) ||
        (pool.k != adapter.k) ||
        (pool.free_count != adapter.k) ||
        (pool.ready_count != 0U) ||
        (pool.processing_count != 0U) ||
        (pool.dma_owned_count != 2U) ||
        ((uint32_t)uxQueueMessagesWaiting(adapter.free_queue) != adapter.k) ||
        (uxQueueMessagesWaiting(adapter.ready_queue) != 0U))
    {
        return LatchFault(STREAM_QUEUE_ADAPTER_RECONCILE_ERROR);
    }

    vQueueDelete(adapter.free_queue);
    vQueueDelete(adapter.ready_queue);
    StreamTokenLedger_ResetOffline();

    (void)memset(&adapter, 0, sizeof(adapter));
    adapter.phase = ADAPTER_PHASE_RESET;
    adapter.first_fault = STREAM_QUEUE_ADAPTER_OK;
    adapter.send_context.buffer_id = R2_BUFFER_POOL_INVALID_ID;
    ClearHeldReady();
    ClearProcessingLease();
    return STREAM_QUEUE_ADAPTER_OK;
}

StreamQueueAdapterStatus StreamQueueAdapter_BindProcessingTask(
    TaskHandle_t processing_task)
{
    StreamQueueAdapterStatus status = RequireReady();

    if (status != STREAM_QUEUE_ADAPTER_OK)
    {
        return status;
    }
    if ((processing_task == NULL) || (adapter.processing_task != NULL))
    {
        return LatchFault(STREAM_QUEUE_ADAPTER_INVALID_STATE);
    }

    adapter.processing_task = processing_task;
    return STREAM_QUEUE_ADAPTER_OK;
}

StreamQueueAdapterStatus StreamQueueAdapter_TakeFreeFromISR(
    R2_BufferId *out_id,
    BaseType_t *higher_priority_task_woken)
{
    R2_BufferId id;
    BaseType_t result;
    StreamTokenStatus token_status;
    StreamQueueAdapterStatus status = RequireReady();

    if (status != STREAM_QUEUE_ADAPTER_OK)
    {
        return status;
    }
    if ((out_id == NULL) || (higher_priority_task_woken == NULL))
    {
        return STREAM_QUEUE_ADAPTER_INVALID_ARGUMENT;
    }

    result = xQueueReceiveFromISR(
        adapter.free_queue,
        &id,
        higher_priority_task_woken);

    if (result != pdPASS)
    {
        return STREAM_QUEUE_ADAPTER_EMPTY;
    }

    token_status = StreamTokenLedger_TakeFreeForAdmission(id);
    if (token_status != STREAM_TOKEN_OK)
    {
        ++adapter.ledger_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_LEDGER_ERROR);
    }

    ++adapter.free_take_count;
    *out_id = id;
    return STREAM_QUEUE_ADAPTER_OK;
}

StreamQueueAdapterStatus StreamQueueAdapter_PublishReadyFromISR(
    const StreamQueueAdapterReadyDescriptor *descriptor,
    BaseType_t *higher_priority_task_woken)
{
    const StreamOwnershipDescriptor *ownership;
    BaseType_t result;
    StreamTokenStatus token_status;
    StreamTokenSnapshot token_snapshot;
    R2_BufferState state;
    uint32_t bit;
    StreamQueueAdapterStatus status = RequireReady();

    if (status != STREAM_QUEUE_ADAPTER_OK)
    {
        return status;
    }
    if ((descriptor == NULL) || (higher_priority_task_woken == NULL))
    {
        return STREAM_QUEUE_ADAPTER_INVALID_ARGUMENT;
    }

    ownership = &descriptor->ownership;
    if ((uint32_t)ownership->buffer_id >= R2_BUFFER_POOL_MAX_BUFFERS)
    {
        return STREAM_QUEUE_ADAPTER_INVALID_ARGUMENT;
    }

    if ((R2_BufferPool_GetState(ownership->buffer_id, &state) !=
            R2_BUFFER_POOL_OK) ||
        (state != R2_BUFFER_STATE_READY))
    {
        ++adapter.owner_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_OWNER_ERROR);
    }

    if (StreamTokenLedger_GetSnapshot(&token_snapshot) != STREAM_TOKEN_OK)
    {
        ++adapter.ledger_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_LEDGER_ERROR);
    }

    bit = 1UL << (uint32_t)ownership->buffer_id;
    if ((token_snapshot.faulted != 0U) ||
        (token_snapshot.admission_active == 0U) ||
        (token_snapshot.admission_replacement == ownership->buffer_id) ||
        ((token_snapshot.free_mask | token_snapshot.ready_mask) & bit) != 0U ||
        ((token_snapshot.ready_hold_active != 0U) &&
         (token_snapshot.ready_hold_buffer == ownership->buffer_id)))
    {
        ++adapter.ledger_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_LEDGER_ERROR);
    }

    result = xQueueSendFromISR(
        adapter.ready_queue,
        descriptor,
        higher_priority_task_woken);

    if (result != pdPASS)
    {
        ++adapter.queue_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_QUEUE_ERROR);
    }

    token_status = StreamTokenLedger_PublishReady(ownership->buffer_id);
    if (token_status != STREAM_TOKEN_OK)
    {
        ++adapter.ledger_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_LEDGER_ERROR);
    }

    ++adapter.ready_publish_count;
    return STREAM_QUEUE_ADAPTER_OK;
}

StreamQueueAdapterStatus StreamQueueAdapter_TakeReady(
    StreamQueueAdapterReadyDescriptor *out_descriptor)
{
    BaseType_t result;
    StreamTokenStatus token_status;
    StreamQueueAdapterStatus status = RequireReady();

    if (status != STREAM_QUEUE_ADAPTER_OK)
    {
        return status;
    }
    if (out_descriptor == NULL)
    {
        return STREAM_QUEUE_ADAPTER_INVALID_ARGUMENT;
    }
    if (!OrdinaryTaskContext())
    {
        ++adapter.auth_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_CALLER_CONTEXT_ERROR);
    }
    if (!ProcessingCaller())
    {
        ++adapter.auth_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_WRONG_TASK);
    }
    if (adapter.processing_lease_valid != 0U)
    {
        ++adapter.owner_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_OWNER_ERROR);
    }

    /*
     * Keep Queue removal and ready_hold ledger open atomic relative to the
     * DMA ISR. xQueueReceive() is zero-wait and FreeRTOS critical sections are
     * nestable on the fixed ARM_CM4F port.
     */
    taskENTER_CRITICAL();

    if (adapter.phase != ADAPTER_PHASE_READY)
    {
        taskEXIT_CRITICAL();
        return STREAM_QUEUE_ADAPTER_FAULTED;
    }
    if (adapter.held_ready_valid != 0U)
    {
        taskEXIT_CRITICAL();
        ++adapter.ledger_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_LEDGER_ERROR);
    }

    {
        StreamTokenSnapshot token_snapshot;

        if ((StreamTokenLedger_GetSnapshot(&token_snapshot) !=
                STREAM_TOKEN_OK) ||
            (token_snapshot.faulted != 0U) ||
            (token_snapshot.ready_hold_active != 0U))
        {
            taskEXIT_CRITICAL();
            ++adapter.ledger_failure_count;
            return LatchFault(STREAM_QUEUE_ADAPTER_LEDGER_ERROR);
        }
    }

    result = xQueueReceive(
        adapter.ready_queue,
        out_descriptor,
        0U);

    if (result != pdPASS)
    {
        taskEXIT_CRITICAL();
        return STREAM_QUEUE_ADAPTER_EMPTY;
    }

    token_status = StreamTokenLedger_TakeReady(
        out_descriptor->ownership.buffer_id);
    if (token_status != STREAM_TOKEN_OK)
    {
        taskEXIT_CRITICAL();
        ++adapter.ledger_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_LEDGER_ERROR);
    }

    adapter.held_ready_descriptor = *out_descriptor;
    adapter.held_ready_valid = 1U;
    ++adapter.ready_take_count;
    taskEXIT_CRITICAL();
    return STREAM_QUEUE_ADAPTER_OK;
}

StreamQueueAdapterStatus StreamQueueAdapter_ClaimHeldReady(
    const StreamQueueAdapterPermit *permit)
{
    const StreamQueueAdapterReadyDescriptor *descriptor;
    const StreamOwnershipDescriptor *ownership;
    R2_BufferState state;
    StreamTokenStatus token_status;
    StreamTokenSnapshot token_snapshot;
    StreamQueueAdapterStatus status;

    if (permit == NULL)
    {
        return STREAM_QUEUE_ADAPTER_INVALID_ARGUMENT;
    }
    if (adapter.held_ready_valid == 0U)
    {
        ++adapter.ledger_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_LEDGER_ERROR);
    }
    if (adapter.processing_lease_valid != 0U)
    {
        ++adapter.owner_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_OWNER_ERROR);
    }

    descriptor = &adapter.held_ready_descriptor;
    ownership = &descriptor->ownership;

    status = BeginAuthorizedTaskOperation(
        STREAM_QUEUE_ADAPTER_OP_READY_CLAIM,
        ownership->buffer_id,
        descriptor,
        permit);
    if (status != STREAM_QUEUE_ADAPTER_OK)
    {
        return status;
    }

    taskENTER_CRITICAL();

    if (adapter.phase != ADAPTER_PHASE_READY)
    {
        taskEXIT_CRITICAL();
        return STREAM_QUEUE_ADAPTER_FAULTED;
    }

    if ((adapter.held_ready_valid == 0U) ||
        (StreamTokenLedger_GetSnapshot(&token_snapshot) != STREAM_TOKEN_OK) ||
        (token_snapshot.faulted != 0U) ||
        (token_snapshot.ready_hold_active == 0U) ||
        (token_snapshot.ready_hold_buffer != ownership->buffer_id))
    {
        taskEXIT_CRITICAL();
        ++adapter.ledger_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_LEDGER_ERROR);
    }

    if ((R2_BufferPool_GetState(ownership->buffer_id, &state) !=
            R2_BUFFER_POOL_OK) ||
        (state != R2_BUFFER_STATE_READY))
    {
        taskEXIT_CRITICAL();
        ++adapter.owner_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_OWNER_ERROR);
    }

    if (!Authorize(
            STREAM_QUEUE_ADAPTER_OP_READY_CLAIM,
            ownership->buffer_id,
            descriptor,
            permit))
    {
        taskEXIT_CRITICAL();
        ++adapter.auth_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_AUTH_REJECTED);
    }

    if (R2_BufferPool_ClaimReady(ownership->buffer_id) != R2_BUFFER_POOL_OK)
    {
        taskEXIT_CRITICAL();
        ++adapter.owner_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_OWNER_ERROR);
    }

    token_status = StreamTokenLedger_CommitReadyClaim(
        ownership->buffer_id);
    if (token_status != STREAM_TOKEN_OK)
    {
        taskEXIT_CRITICAL();
        ++adapter.ledger_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_LEDGER_ERROR);
    }

    adapter.processing_lease_descriptor = adapter.held_ready_descriptor;
    adapter.processing_lease_valid = 1U;
    ClearHeldReady();
    ++adapter.ready_claim_count;
    taskEXIT_CRITICAL();
    return STREAM_QUEUE_ADAPTER_OK;
}

StreamQueueAdapterStatus StreamQueueAdapter_CancelHeldReady(
    const StreamQueueAdapterPermit *permit,
    StreamQueueAdapterReceipt *out_receipt)
{
    if ((permit == NULL) || (out_receipt == NULL))
    {
        return STREAM_QUEUE_ADAPTER_INVALID_ARGUMENT;
    }
    if (adapter.held_ready_valid == 0U)
    {
        ++adapter.ledger_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_LEDGER_ERROR);
    }

    return SendFreeInternal(
        STREAM_QUEUE_ADAPTER_OP_CANCEL,
        adapter.held_ready_descriptor.ownership.buffer_id,
        &adapter.held_ready_descriptor,
        permit,
        out_receipt);
}

StreamQueueAdapterStatus StreamQueueAdapter_CompleteAndReleaseBlock(
    const StreamQueueAdapterPermit *permit,
    StreamQueueAdapterReceipt *out_receipt)
{
    if ((permit == NULL) || (out_receipt == NULL))
    {
        return STREAM_QUEUE_ADAPTER_INVALID_ARGUMENT;
    }
    if (adapter.processing_lease_valid == 0U)
    {
        ++adapter.owner_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_OWNER_ERROR);
    }

    return SendFreeInternal(
        STREAM_QUEUE_ADAPTER_OP_COMPLETE,
        adapter.processing_lease_descriptor.ownership.buffer_id,
        &adapter.processing_lease_descriptor,
        permit,
        out_receipt);
}

StreamQueueAdapterStatus StreamQueueAdapter_ValidateStable(void)
{
    R2_BufferPoolSnapshot pool;
    StreamTokenStatus token_status;
    StreamQueueAdapterStatus status = RequireReady();

    if (status != STREAM_QUEUE_ADAPTER_OK)
    {
        return status;
    }

    if (R2_BufferPool_GetSnapshot(&pool) != R2_BUFFER_POOL_OK)
    {
        ++adapter.owner_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_OWNER_ERROR);
    }

    token_status = StreamTokenLedger_ValidateStable(&pool);
    if (token_status != STREAM_TOKEN_OK)
    {
        ++adapter.ledger_failure_count;
        return LatchFault(STREAM_QUEUE_ADAPTER_LEDGER_ERROR);
    }

    if (((uint32_t)uxQueueMessagesWaiting(adapter.free_queue) !=
            pool.free_count) ||
        ((uint32_t)uxQueueMessagesWaiting(adapter.ready_queue) !=
            pool.ready_count))
    {
        return LatchFault(STREAM_QUEUE_ADAPTER_RECONCILE_ERROR);
    }

    return STREAM_QUEUE_ADAPTER_OK;
}

StreamQueueAdapterStatus StreamQueueAdapter_GetSnapshot(
    StreamQueueAdapterSnapshot *out)
{
    if (out == NULL)
    {
        return STREAM_QUEUE_ADAPTER_INVALID_ARGUMENT;
    }

    (void)memset(out, 0, sizeof(*out));
    out->initialized = adapter.phase != ADAPTER_PHASE_RESET ? 1U : 0U;
    out->ready = adapter.phase == ADAPTER_PHASE_READY ? 1U : 0U;
    out->faulted = adapter.phase == ADAPTER_PHASE_FAULTED ? 1U : 0U;
    out->k = adapter.k;
    out->first_fault = adapter.first_fault;
    out->init_commit_count = adapter.init_commit_count;
    out->cancel_commit_count = adapter.cancel_commit_count;
    out->complete_commit_count = adapter.complete_commit_count;
    out->ready_publish_count = adapter.ready_publish_count;
    out->free_take_count = adapter.free_take_count;
    out->ready_take_count = adapter.ready_take_count;
    out->ready_claim_count = adapter.ready_claim_count;
    out->held_ready_valid = adapter.held_ready_valid;
    out->held_ready_buffer =
        adapter.held_ready_valid != 0U ?
            adapter.held_ready_descriptor.ownership.buffer_id :
            R2_BUFFER_POOL_INVALID_ID;
    out->processing_lease_valid = adapter.processing_lease_valid;
    out->processing_lease_buffer =
        adapter.processing_lease_valid != 0U ?
            adapter.processing_lease_descriptor.ownership.buffer_id :
            R2_BUFFER_POOL_INVALID_ID;
    out->hook_call_count = adapter.hook_call_count;
    out->illegal_hook_count = adapter.illegal_hook_count;
    out->queue_failure_count = adapter.queue_failure_count;
    out->auth_failure_count = adapter.auth_failure_count;
    out->owner_failure_count = adapter.owner_failure_count;
    out->ledger_failure_count = adapter.ledger_failure_count;
    out->failure_count = adapter.failure_count;
    out->last_commit_serial = adapter.last_commit_serial;

    if (adapter.free_queue != NULL)
    {
        out->free_depth = (uint32_t)uxQueueMessagesWaiting(adapter.free_queue);
    }
    if (adapter.ready_queue != NULL)
    {
        out->ready_depth = (uint32_t)uxQueueMessagesWaiting(adapter.ready_queue);
    }

    return STREAM_QUEUE_ADAPTER_OK;
}

void StreamQueueAdapter_TraceQueueSend(void *queue_handle)
{
    R2_BufferState state;
    StreamTokenStatus token_status;
    StreamQueueAdapterStatus hook_status = STREAM_QUEUE_ADAPTER_OK;
    uint32_t commit_serial = 0U;
    R2_BufferId id;

    if (queue_handle != (void *)adapter.free_queue)
    {
        return;
    }

    ++adapter.hook_call_count;

    if (adapter.send_context.active == 0U)
    {
        ++adapter.illegal_hook_count;
        (void)LatchFault(STREAM_QUEUE_ADAPTER_HOOK_REJECTED);
        return;
    }

    adapter.send_context.hook_seen = 1U;
    id = adapter.send_context.buffer_id;

    if ((uint32_t)id >= R2_BUFFER_POOL_MAX_BUFFERS)
    {
        ++adapter.illegal_hook_count;
        hook_status = STREAM_QUEUE_ADAPTER_HOOK_REJECTED;
    }
    else if (adapter.send_context.operation == STREAM_QUEUE_ADAPTER_OP_INIT)
    {
        if ((adapter.phase != ADAPTER_PHASE_INITIALIZING) ||
            (R2_BufferPool_GetState(id, &state) != R2_BUFFER_POOL_OK) ||
            (state != R2_BUFFER_STATE_FREE) ||
            ((uint32_t)id < 2U))
        {
            ++adapter.owner_failure_count;
            hook_status = STREAM_QUEUE_ADAPTER_OWNER_ERROR;
        }
        else
        {
            token_status = StreamTokenLedger_RecordFreeSend(
                STREAM_TOKEN_FREE_INIT, id);
            if (token_status != STREAM_TOKEN_OK)
            {
                ++adapter.ledger_failure_count;
                hook_status = STREAM_QUEUE_ADAPTER_LEDGER_ERROR;
            }
        }

        if (hook_status == STREAM_QUEUE_ADAPTER_OK)
        {
            ++adapter.init_commit_count;
        }
    }
    else if ((adapter.send_context.operation ==
                STREAM_QUEUE_ADAPTER_OP_CANCEL) ||
             (adapter.send_context.operation ==
                STREAM_QUEUE_ADAPTER_OP_COMPLETE))
    {
        if ((adapter.phase != ADAPTER_PHASE_READY) ||
            (adapter.processing_task == NULL) ||
            (adapter.send_context.caller_task != adapter.processing_task) ||
            !Authorize(
                adapter.send_context.operation,
                id,
                adapter.send_context.have_ready_descriptor != 0U ?
                    &adapter.send_context.ready_descriptor : NULL,
                &adapter.send_context.permit))
        {
            ++adapter.auth_failure_count;
            hook_status = STREAM_QUEUE_ADAPTER_AUTH_REJECTED;
        }
        else if (R2_BufferPool_GetState(id, &state) != R2_BUFFER_POOL_OK)
        {
            ++adapter.owner_failure_count;
            hook_status = STREAM_QUEUE_ADAPTER_OWNER_ERROR;
        }
        else if (adapter.send_context.operation ==
                    STREAM_QUEUE_ADAPTER_OP_CANCEL)
        {
            if ((adapter.send_context.have_ready_descriptor == 0U) ||
                (adapter.send_context.ready_descriptor.ownership.buffer_id != id) ||
                (state != R2_BUFFER_STATE_READY) ||
                (R2_BufferPool_CancelReady(id) != R2_BUFFER_POOL_OK))
            {
                ++adapter.owner_failure_count;
                hook_status = STREAM_QUEUE_ADAPTER_OWNER_ERROR;
            }
            else
            {
                token_status = StreamTokenLedger_RecordFreeSend(
                    STREAM_TOKEN_FREE_CANCEL, id);
                if (token_status != STREAM_TOKEN_OK)
                {
                    ++adapter.ledger_failure_count;
                    hook_status = STREAM_QUEUE_ADAPTER_LEDGER_ERROR;
                }
                else
                {
                    ClearHeldReady();
                    ++adapter.cancel_commit_count;
                }
            }
        }
        else
        {
            if ((adapter.send_context.have_ready_descriptor == 0U) ||
                (adapter.processing_lease_valid == 0U) ||
                (adapter.send_context.ready_descriptor.ownership.buffer_id != id) ||
                (adapter.processing_lease_descriptor.ownership.buffer_id != id) ||
                (state != R2_BUFFER_STATE_PROCESSING) ||
                (R2_BufferPool_ReleaseProcessing(id) != R2_BUFFER_POOL_OK))
            {
                ++adapter.owner_failure_count;
                hook_status = STREAM_QUEUE_ADAPTER_OWNER_ERROR;
            }
            else
            {
                token_status = StreamTokenLedger_RecordFreeSend(
                    STREAM_TOKEN_FREE_COMPLETE, id);
                if (token_status != STREAM_TOKEN_OK)
                {
                    ++adapter.ledger_failure_count;
                    hook_status = STREAM_QUEUE_ADAPTER_LEDGER_ERROR;
                }
                else
                {
                    ClearProcessingLease();
                    ++adapter.complete_commit_count;
                }
            }
        }
    }
    else
    {
        ++adapter.illegal_hook_count;
        hook_status = STREAM_QUEUE_ADAPTER_HOOK_REJECTED;
    }

    if ((hook_status == STREAM_QUEUE_ADAPTER_OK) &&
        (CommitSerial(&commit_serial) != STREAM_QUEUE_ADAPTER_OK))
    {
        hook_status = STREAM_QUEUE_ADAPTER_HOOK_REJECTED;
    }

    if (hook_status != STREAM_QUEUE_ADAPTER_OK)
    {
        (void)LatchFault(hook_status);
    }

    adapter.send_context.hook_status = hook_status;
    adapter.send_context.commit_serial = commit_serial;
}
