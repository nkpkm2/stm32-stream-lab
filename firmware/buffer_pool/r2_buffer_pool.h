#ifndef R2_BUFFER_POOL_H
#define R2_BUFFER_POOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* R2-W1: ownership bookkeeping only; no samples, DMA, RTOS, or queues. */
#define R2_BUFFER_POOL_MAX_BUFFERS 10U
#define R2_BUFFER_POOL_INVALID_ID  0xFFU

typedef uint8_t R2_BufferId;

typedef enum
{
    R2_BUFFER_STATE_INACTIVE = 0,
    R2_BUFFER_STATE_FREE,
    R2_BUFFER_STATE_DMA_OWNED,
    R2_BUFFER_STATE_READY,
    R2_BUFFER_STATE_PROCESSING
} R2_BufferState;

typedef enum
{
    R2_BUFFER_POOL_OK = 0,
    R2_BUFFER_POOL_NO_FREE_BUFFER,
    R2_BUFFER_POOL_INVALID_ARGUMENT,
    R2_BUFFER_POOL_INVALID_K,
    R2_BUFFER_POOL_NOT_ACTIVE,
    R2_BUFFER_POOL_ALREADY_ACTIVE,
    R2_BUFFER_POOL_ID_OUT_OF_RANGE,
    R2_BUFFER_POOL_ILLEGAL_TRANSITION,
    R2_BUFFER_POOL_INVARIANT_ERROR
} R2_BufferPoolStatus;

typedef struct
{
    uint32_t activated;
    uint32_t k;
    uint32_t active_count;
    uint32_t inactive_count;
    uint32_t free_count;
    uint32_t dma_owned_count;
    uint32_t ready_count;
    uint32_t processing_count;
    uint32_t violation_count;
    R2_BufferState states[R2_BUFFER_POOL_MAX_BUFFERS];
} R2_BufferPoolSnapshot;

/*
 * Concurrency contract:
 *   All calls are externally serialized. This module has NO synchronization.
 *   A successful rotation is failure-atomic at the API boundary, NOT a CPU
 *   atomic operation and NOT proof of a hardware MxAR/queue commit.
 *
 * Integration boundary:
 *   FindFree is a deterministic read-only model query, not an allocation or
 *   a reservation. In W3/W4 the approved FreeBufferQueue/adapter is the token
 *   authority. Do not substitute a pool scan for that queue transaction.
 *   ReleaseProcessing is a low-level bookkeeping transition; it is NOT a
 *   replacement for the architecture's CompleteAndReleaseBlock adapter.
 *   IDs have no generation/lease yet. Reuse of an old ID after a full cycle
 *   is not detectable here; W1 must not be advertised as lease validation.
 *
 * Failure contract:
 *   Illegal operations leave ownership unchanged and increment the saturating
 *   violation count once. Caller-owned output arguments are unchanged on
 *   failure. NO_FREE_BUFFER is normal and does not increment the count.
 *   Errors are reported, never silently repaired. Native tests deliberately
 *   continue after expected errors; future hardware callers must latch and
 *   handle infrastructure failures rather than ignore them.
 */

/* Offline/quiescent operation only. Invalidates all prior IDs and resets
 * diagnostics. It is NOT a runtime STOP, hardware recovery, or queue drain. */
void R2_BufferPool_Reset(void);

/* Requires a reset/unactivated pool. Valid K: 1, 2, 4, 8. B0/B1 become the
 * model's two DMA-owned buffers; B2..B(K+1) become FREE. No M0/M1 mapping here. */
R2_BufferPoolStatus R2_BufferPool_Activate(uint32_t k);

/* Returns the lowest FREE ID without reserving it; output untouched on error. */
R2_BufferPoolStatus R2_BufferPool_FindFree(R2_BufferId *out_id);

/* Validate both IDs/states first, then completed DMA->READY, FREE->DMA.
 * Any rejected call changes diagnostics only, never half the ownership pair. */
R2_BufferPoolStatus R2_BufferPool_CommitDmaRotation(
    R2_BufferId completed_id,
    R2_BufferId replacement_id);

R2_BufferPoolStatus R2_BufferPool_ClaimReady(R2_BufferId id);
R2_BufferPoolStatus R2_BufferPool_ReleaseProcessing(R2_BufferId id);

/* GetState can inspect INACTIVE IDs, including in the reset state. */
R2_BufferPoolStatus R2_BufferPool_GetState(
    R2_BufferId id,
    R2_BufferState *out_state);
R2_BufferPoolStatus R2_BufferPool_GetSnapshot(R2_BufferPoolSnapshot *out);
R2_BufferPoolStatus R2_BufferPool_Validate(void);

#ifdef __cplusplus
}
#endif

#endif
