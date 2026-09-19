#ifndef STREAM_QUEUE_ADAPTER_H
#define STREAM_QUEUE_ADAPTER_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "stream_ownership_core.h"
#include "stream_token_ledger.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * R2->R3 Runtime Foundation Consolidation, F0-3 Stage 2B-1.
 *
 * Real FreeRTOS queue transport adapter.
 *
 * Responsibility split:
 *   - R2_BufferPool: semantic buffer owner.
 *   - StreamOwnershipCore: DMA/logical ownership transaction composition.
 *   - StreamTokenLedger: FREE/READY token validation mirror.
 *   - StreamQueueAdapter: the only transport authority for FreeQueue and
 *     ReadyQueue plus the traceQUEUE_SEND protected-commit bridge.
 *
 * FreeQueue and ReadyQueue handles are intentionally private and are never
 * returned to callers. This makes direct/bypass FreeQueue sends structurally
 * unavailable outside this translation unit.
 *
 * traceQUEUE_SEND contract:
 *   FreeRTOS V11.1.0 calls the hook inside the successful task-send critical
 *   section before prvCopyDataToQueue() copies the token. The hook therefore
 *   commits the semantic owner transition and token-ledger mirror before the
 *   new FREE token can become visible to a DMA-side receive.
 *
 * The hook cannot veto the kernel copy. Any hook violation latches adapter
 * FAULTED state. All normal adapter receive/claim/publish entry points refuse
 * service once faulted, so a token copied after an illegal hook path cannot be
 * consumed by normal acquisition.
 *
 * Stage 2B-1 deliberately does not implement R3 RunContext/generation policy.
 * Instead, CANCEL/CLAIM/COMPLETE carry an opaque permit. A caller-supplied
 * bounded authorizer is run both before the operation and again at the
 * protected commit point. R3 may later encode run_id/generation/lease into
 * that policy without changing this adapter.
 *
 * The authorizer MUST be bounded, read-only with respect to the adapter,
 * callable from inside traceQUEUE_SEND/task critical sections, and MUST NOT
 * call FreeRTOS APIs, block, allocate, log, or recursively enter this adapter.
 *
 * Processing-side queue/claim/cancel/complete entry points require ordinary
 * task context. The adapter rejects ISR callers, caller-owned PRIMASK/BASEPRI
 * masking, and scheduler suspension before entering the protected queue-send
 * path, matching the v3.2.2 completion-adapter precondition.
 */

#define STREAM_QUEUE_ADAPTER_MAX_K 8U
#define STREAM_QUEUE_ADAPTER_PERMIT_WORDS 4U

typedef enum
{
    STREAM_QUEUE_ADAPTER_OK = 0,
    STREAM_QUEUE_ADAPTER_EMPTY,
    STREAM_QUEUE_ADAPTER_INVALID_ARGUMENT,
    STREAM_QUEUE_ADAPTER_INVALID_STATE,
    STREAM_QUEUE_ADAPTER_FAULTED,
    STREAM_QUEUE_ADAPTER_WRONG_TASK,
    STREAM_QUEUE_ADAPTER_CALLER_CONTEXT_ERROR,
    STREAM_QUEUE_ADAPTER_AUTH_REJECTED,
    STREAM_QUEUE_ADAPTER_QUEUE_ERROR,
    STREAM_QUEUE_ADAPTER_HOOK_MISSING,
    STREAM_QUEUE_ADAPTER_HOOK_REJECTED,
    STREAM_QUEUE_ADAPTER_OWNER_ERROR,
    STREAM_QUEUE_ADAPTER_LEDGER_ERROR,
    STREAM_QUEUE_ADAPTER_RECONCILE_ERROR
} StreamQueueAdapterStatus;

typedef enum
{
    STREAM_QUEUE_ADAPTER_OP_INIT = 0,
    STREAM_QUEUE_ADAPTER_OP_READY_CLAIM,
    STREAM_QUEUE_ADAPTER_OP_CANCEL,
    STREAM_QUEUE_ADAPTER_OP_COMPLETE
} StreamQueueAdapterOperation;

typedef struct
{
    uint32_t words[STREAM_QUEUE_ADAPTER_PERMIT_WORDS];
} StreamQueueAdapterPermit;

#define STREAM_QUEUE_ADAPTER_PROVENANCE_WORDS 4U

/*
 * Opaque cross-run provenance transported with every READY descriptor.
 * Stage 2B-1 deliberately assigns no lifecycle meaning to these words.
 * Stage 2C/R3 MUST encode at least run identity/generation here before READY
 * publication and the authorizer MUST validate that provenance before CLAIM
 * or CANCEL. This reserves the transport shape now without coupling the queue
 * layer to RunContext policy.
 */
typedef struct
{
    uint32_t words[STREAM_QUEUE_ADAPTER_PROVENANCE_WORDS];
} StreamQueueAdapterProvenance;

typedef struct
{
    StreamOwnershipDescriptor ownership;
    StreamQueueAdapterProvenance provenance;
} StreamQueueAdapterReadyDescriptor;

typedef int (*StreamQueueAdapterAuthorizeFn)(
    StreamQueueAdapterOperation operation,
    R2_BufferId id,
    const StreamQueueAdapterReadyDescriptor *ready_descriptor,
    const StreamQueueAdapterPermit *permit,
    void *context);

typedef struct
{
    uint32_t commit_serial;
    StreamQueueAdapterOperation operation;
    R2_BufferId buffer_id;
} StreamQueueAdapterReceipt;

typedef struct
{
    uint32_t initialized;
    uint32_t ready;
    uint32_t faulted;
    uint32_t k;
    StreamQueueAdapterStatus first_fault;

    uint32_t free_depth;
    uint32_t ready_depth;

    uint32_t init_commit_count;
    uint32_t cancel_commit_count;
    uint32_t complete_commit_count;
    uint32_t ready_publish_count;
    uint32_t free_take_count;
    uint32_t ready_take_count;
    uint32_t ready_claim_count;

    uint32_t held_ready_valid;
    R2_BufferId held_ready_buffer;

    uint32_t processing_lease_valid;
    R2_BufferId processing_lease_buffer;

    uint32_t hook_call_count;
    uint32_t illegal_hook_count;
    uint32_t queue_failure_count;
    uint32_t auth_failure_count;
    uint32_t owner_failure_count;
    uint32_t ledger_failure_count;
    uint32_t failure_count;
    uint32_t last_commit_serial;
} StreamQueueAdapterSnapshot;

/*
 * Per-run offline initialization.
 * Preconditions:
 *   - adapter is RESET (boot or successful ResetOffline);
 *   - StreamOwnershipCore already initialized for the same K and healthy;
 *   - no prior live queue/token state;
 *   - authorizer is non-NULL.
 *
 * Reconstructs the static queue objects with logical capacity K,
 * resets/initializes StreamTokenLedger, seeds B2..B(K+1) through the protected
 * INIT send path, seals the ledger, and reconciles the initial stable state.
 * Reconstructing on every run permits K to change among 1/2/4/8.
 */
StreamQueueAdapterStatus StreamQueueAdapter_Initialize(
    uint32_t k,
    StreamQueueAdapterAuthorizeFn authorizer,
    void *authorizer_context);

/*
 * Normal-run offline teardown/rearm boundary.
 *
 * Caller must already have stopped DMA access and obtained worker quiescence.
 * This function additionally requires stable queue/ledger reconciliation,
 * ReadyQueue empty, all K non-DMA buffers FREE, no PROCESSING owner, and no
 * active send transaction, held READY descriptor, or active PROCESSING lease.
 * On success both static
 * queue objects are passed through vQueueDelete() (which performs the kernel's
 * queue-delete/registry teardown without freeing static storage), token-ledger
 * state is reset, and the adapter returns to RESET so the next run may
 * Initialize() with a different K.
 *
 * After success the lifecycle coordinator may reset/reinitialize
 * StreamOwnershipCore for the next K, then call StreamQueueAdapter_Initialize()
 * for the new run.
 *
 * FAULTED adapters are deliberately not reset here; infrastructure-fault
 * recovery remains the architecture's controlled/full-reset path.
 */
StreamQueueAdapterStatus StreamQueueAdapter_ResetOffline(void);

/* Bind exactly one Processing task. Required before READY take/claim/CANCEL/
 * COMPLETE operations. The task handle is compared without calling FreeRTOS
 * from inside traceQUEUE_SEND. */
StreamQueueAdapterStatus StreamQueueAdapter_BindProcessingTask(
    TaskHandle_t processing_task);

/*
 * DMA-side FreeQueue receive. EMPTY is a normal capacity condition and does
 * not fault the adapter. On successful queue removal, the ledger opens the
 * admission transaction before the token is returned to the caller.
 */
StreamQueueAdapterStatus StreamQueueAdapter_TakeFreeFromISR(
    R2_BufferId *out_id,
    BaseType_t *higher_priority_task_woken);

/*
 * Publish a logically committed READY descriptor from ISR.
 * Queue publication occurs first; only after the FromISR send succeeds does
 * the ledger commit READY visibility. If the post-copy ledger commit fails,
 * the adapter latches FAULTED and normal READY consumption is closed.
 */
StreamQueueAdapterStatus StreamQueueAdapter_PublishReadyFromISR(
    const StreamQueueAdapterReadyDescriptor *descriptor,
    BaseType_t *higher_priority_task_woken);

/*
 * Processing-task-only zero-wait READY receive. Queue removal, opening the
 * ready_hold ledger transaction, and binding the exact dequeued descriptor
 * (including opaque provenance) to the adapter occur in one short task
 * critical section. Only one descriptor may be held at a time, and no new
 * READY descriptor may be taken while this Processing task still owns an
 * unresolved PROCESSING lease from a prior successful claim.
 */
StreamQueueAdapterStatus StreamQueueAdapter_TakeReady(
    StreamQueueAdapterReadyDescriptor *out_descriptor);

/*
 * Processing-task-only READY -> PROCESSING claim for the exact descriptor
 * previously bound by TakeReady(). On success that exact descriptor/provenance
 * becomes the adapter's one active PROCESSING lease; the caller cannot claim a
 * second block until the current lease is completed and returned.
 */
StreamQueueAdapterStatus StreamQueueAdapter_ClaimHeldReady(
    const StreamQueueAdapterPermit *permit);

/*
 * Processing-task-only STOP/CANCEL return of the exact dequeued-but-unclaimed
 * READY descriptor held by the adapter. READY -> FREE and ledger CANCEL commit
 * happen inside traceQUEUE_SEND before the FreeQueue token copy. The caller
 * cannot substitute a different descriptor/provenance.
 */
StreamQueueAdapterStatus StreamQueueAdapter_CancelHeldReady(
    const StreamQueueAdapterPermit *permit,
    StreamQueueAdapterReceipt *out_receipt);

/*
 * Unique normal PROCESSING completion-return entry point.
 * The buffer/provenance being released is the exact active PROCESSING lease
 * previously established by ClaimHeldReady(); callers cannot substitute a
 * different buffer ID. PROCESSING -> FREE and ledger COMPLETE commit happen
 * inside traceQUEUE_SEND before token copy. After this returns OK, caller must
 * not access the released buffer or reusable shared metadata.
 *
 * Entry also enforces ordinary Processing task context: not ISR context, no
 * caller-owned PRIMASK/BASEPRI critical section, and scheduler not suspended.
 *
 * R4 later supplies the architecture's Clock64 t_commit/t_unlock accounting;
 * Stage 2B-1 proves protected commit semantics only and makes no <=1800-cycle
 * CompleteAndReleaseBlock claim.
 */
StreamQueueAdapterStatus StreamQueueAdapter_CompleteAndReleaseBlock(
    const StreamQueueAdapterPermit *permit,
    StreamQueueAdapterReceipt *out_receipt);

/* Stable/quiescent reconciliation only. Must not race ISR/task queue traffic. */
StreamQueueAdapterStatus StreamQueueAdapter_ValidateStable(void);

/* Diagnostic snapshot. Queue depths/counters are transactionally stable only
 * when the caller has externally quiesced queue/ISR traffic. */
StreamQueueAdapterStatus StreamQueueAdapter_GetSnapshot(
    StreamQueueAdapterSnapshot *out);

/* Sole project hook bound from FreeRTOSConfig.h when the foundation queue
 * adapter profile is enabled. Called by FreeRTOS queue.c; never call directly. */
void StreamQueueAdapter_TraceQueueSend(void *queue_handle);

#ifdef __cplusplus
}
#endif

#endif
