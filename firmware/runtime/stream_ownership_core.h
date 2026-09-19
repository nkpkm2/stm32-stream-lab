#ifndef STREAM_OWNERSHIP_CORE_H
#define STREAM_OWNERSHIP_CORE_H

#include <stdint.h>

#include "r2_buffer_pool.h"
#include "r2_dma_slots.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * R2->R3 Runtime Foundation Consolidation, F0-3 Stage 1.
 *
 * Pure logical ownership/mapping transaction layer.
 *
 * This module owns the approved composition of R2_BufferPool + R2_DmaSlots.
 * It has no register access, no FreeRTOS dependency, no queues, and no worker
 * logic. The hardware half of a completion remains owned by AdcDbmDriver.
 *
 * Required order for REBIND:
 *   1. StreamOwnership_PrepareCompletion()       (read-only models)
 *   2. AdcDbmDriver_ApplyInactiveAction(REBIND) (physical commit)
 *   3. StreamOwnership_CommitRebind()            (logical commit)
 *
 * If step 3 fails after hardware success, the caller MUST invoke
 * AdcDbmDriver_FailActiveCompletion() before returning from the DMA completion
 * callback. There is no logical rollback after a physical MxAR commit.
 *
 * KEEP is the logical half of a controlled DROP and never advances mapping or
 * ownership. Queue/token authority is intentionally deferred to F0-3 Stage 2.
 *
 * Concurrency / transaction contract:
 *   - this Stage-1 core has no locks or atomics; all calls are externally
 *     serialized;
 *   - production runtime code prepares at most one logical plan for a DMA
 *     completion and uses that same plan to drive the matching hardware action
 *     and logical commit;
 *   - GetSnapshot() must not race a mutating ownership call;
 *   - after AdcDbmDriver has accepted KEEP or REBIND, ANY non-OK logical commit
 *     result requires AdcDbmDriver_FailActiveCompletion() before returning from
 *     that DMA completion callback.
 *
 * ResetOffline() is permitted only while acquisition is quiescent and no queue
 * tokens or worker ownership from an earlier run remain live. It is not a
 * runtime STOP operation.
 */

typedef enum
{
    STREAM_OWNERSHIP_OK = 0,
    STREAM_OWNERSHIP_INVALID_ARGUMENT,
    STREAM_OWNERSHIP_INVALID_STATE,
    STREAM_OWNERSHIP_MODEL_ERROR,
    STREAM_OWNERSHIP_STALE_TRANSACTION,
    STREAM_OWNERSHIP_PARTIAL_LOGICAL_COMMIT,
    STREAM_OWNERSHIP_POST_COMMIT_INVARIANT
} StreamOwnershipStatus;

typedef enum
{
    STREAM_OWNERSHIP_KEEP = 0,
    STREAM_OWNERSHIP_REBIND = 1
} StreamOwnershipAction;

typedef struct
{
    uint32_t valid;
    uint32_t sequence;
    uint32_t completed_slot;
    uint32_t current_ct;
    StreamOwnershipAction action;
    R2_BufferId completed_buffer;
    R2_BufferId active_buffer;
    R2_BufferId replacement_buffer;
    uint32_t mapping_epoch_before;
    R2_DmaSlotsRebindPlan rebind_plan;
} StreamOwnershipCompletionPlan;

typedef struct
{
    uint32_t sequence;
    R2_BufferId buffer_id;
    uint32_t completed_slot;
    uint32_t mapping_epoch;
} StreamOwnershipDescriptor;

typedef struct
{
    uint32_t initialized;
    uint32_t faulted;
    uint32_t k;
    uint32_t last_committed_sequence;
    uint32_t keep_commit_count;
    uint32_t rebind_commit_count;
    uint32_t failure_count;
    StreamOwnershipStatus last_status;
    R2_BufferPoolSnapshot pool;
    R2_DmaSlotsSnapshot slots;
} StreamOwnershipSnapshot;

void StreamOwnership_ResetOffline(void);

StreamOwnershipStatus StreamOwnership_Initialize(uint32_t k);

StreamOwnershipStatus StreamOwnership_PrepareCompletion(
    uint32_t sequence,
    uint32_t completed_slot,
    StreamOwnershipAction action,
    R2_BufferId replacement_buffer,
    StreamOwnershipCompletionPlan *out_plan);

/*
 * Call only after AdcDbmDriver KEEP returned OK for this completion.
 * This is a no-mutation logical commit and verifies that mapping/ownership
 * remained exactly in the expected DMA-owned state.
 */
StreamOwnershipStatus StreamOwnership_CommitKeep(
    const StreamOwnershipCompletionPlan *plan);

/*
 * Call only after AdcDbmDriver REBIND returned OK for this completion.
 *
 * DmaSlots is committed first, then BufferPool, preserving historical R2
 * ordering. PARTIAL_LOGICAL_COMMIT means the DmaSlots mapping advanced but
 * BufferPool could not complete the paired ownership transition. The caller
 * must fail-stop the active hardware completion; no rollback is attempted.
 */
StreamOwnershipStatus StreamOwnership_CommitRebind(
    const StreamOwnershipCompletionPlan *plan,
    StreamOwnershipDescriptor *out_descriptor);

StreamOwnershipStatus StreamOwnership_GetSnapshot(
    StreamOwnershipSnapshot *out);

#ifdef __cplusplus
}
#endif

#endif
