#include "stream_ownership_core.h"

#include <stddef.h>
#include <string.h>

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
} StreamOwnershipStorage;

static StreamOwnershipStorage ownership;

static int SupportedK(uint32_t k)
{
    return (k == 1U) || (k == 2U) || (k == 4U) || (k == 8U);
}

static StreamOwnershipStatus RecordFailure(StreamOwnershipStatus status)
{
    if (ownership.initialized != 0U)
    {
        ownership.faulted = 1U;
        if (ownership.failure_count != UINT32_MAX)
        {
            ++ownership.failure_count;
        }
    }
    ownership.last_status = status;
    return status;
}

static StreamOwnershipStatus RequireHealthyInitialized(void)
{
    if ((ownership.initialized == 0U) || (ownership.faulted != 0U))
    {
        return STREAM_OWNERSHIP_INVALID_STATE;
    }
    return STREAM_OWNERSHIP_OK;
}

static StreamOwnershipStatus ReadHealthySnapshots(
    R2_BufferPoolSnapshot *pool,
    R2_DmaSlotsSnapshot *slots)
{
    if ((ownership.keep_commit_count > ownership.last_committed_sequence) ||
        (ownership.rebind_commit_count > ownership.last_committed_sequence) ||
        (ownership.keep_commit_count + ownership.rebind_commit_count !=
            ownership.last_committed_sequence))
    {
        return STREAM_OWNERSHIP_MODEL_ERROR;
    }
    if ((R2_BufferPool_GetSnapshot(pool) != R2_BUFFER_POOL_OK) ||
        (R2_DmaSlots_GetSnapshot(slots) != R2_DMA_SLOTS_OK) ||
        (R2_BufferPool_Validate() != R2_BUFFER_POOL_OK) ||
        (R2_DmaSlots_Validate() != R2_DMA_SLOTS_OK))
    {
        return STREAM_OWNERSHIP_MODEL_ERROR;
    }

    if ((pool->activated != 1U) ||
        (pool->k != ownership.k) ||
        (pool->active_count != ownership.k + 2U) ||
        (pool->dma_owned_count != 2U) ||
        ((pool->free_count + pool->ready_count +
          pool->processing_count) != ownership.k) ||
        (pool->violation_count != 0U) ||
        (slots->initialized != 1U) ||
        (slots->mapping_epoch == 0U) ||
        (slots->mapping_epoch != ownership.rebind_commit_count + 1U) ||
        (slots->violation_count != 0U) ||
        (slots->m0_buffer == slots->m1_buffer) ||
        (pool->states[slots->m0_buffer] != R2_BUFFER_STATE_DMA_OWNED) ||
        (pool->states[slots->m1_buffer] != R2_BUFFER_STATE_DMA_OWNED))
    {
        return STREAM_OWNERSHIP_MODEL_ERROR;
    }

    return STREAM_OWNERSHIP_OK;
}

static int PlanShapeValid(const StreamOwnershipCompletionPlan *plan)
{
    if ((plan == NULL) ||
        (plan->valid != 1U) ||
        (plan->sequence == 0U) ||
        (plan->completed_slot > 1U) ||
        (plan->current_ct > 1U) ||
        (plan->current_ct != (plan->completed_slot ^ 1U)) ||
        (plan->current_ct != (plan->sequence & 1U)) ||
        ((plan->action != STREAM_OWNERSHIP_KEEP) &&
         (plan->action != STREAM_OWNERSHIP_REBIND)) ||
        ((uint32_t)plan->completed_buffer >= R2_BUFFER_POOL_MAX_BUFFERS) ||
        ((uint32_t)plan->active_buffer >= R2_BUFFER_POOL_MAX_BUFFERS) ||
        (plan->completed_buffer == plan->active_buffer) ||
        (plan->mapping_epoch_before == 0U))
    {
        return 0;
    }

    if (plan->action == STREAM_OWNERSHIP_KEEP)
    {
        return plan->replacement_buffer == R2_BUFFER_POOL_INVALID_ID;
    }

    if (((uint32_t)plan->replacement_buffer >=
            R2_BUFFER_POOL_MAX_BUFFERS) ||
        (plan->replacement_buffer == plan->completed_buffer) ||
        (plan->replacement_buffer == plan->active_buffer) ||
        (plan->rebind_plan.valid != 1U) ||
        (plan->rebind_plan.ct_snapshot != plan->current_ct) ||
        (plan->rebind_plan.mapping_epoch != plan->mapping_epoch_before) ||
        ((uint32_t)plan->rebind_plan.active_slot != plan->current_ct) ||
        ((uint32_t)plan->rebind_plan.inactive_slot != plan->completed_slot) ||
        (plan->rebind_plan.completed_buffer != plan->completed_buffer) ||
        (plan->rebind_plan.replacement_buffer != plan->replacement_buffer))
    {
        return 0;
    }

    return 1;
}

static int IsNextSequence(uint32_t sequence)
{
    if (ownership.last_committed_sequence == UINT32_MAX)
    {
        return 0;
    }
    return sequence == ownership.last_committed_sequence + 1U;
}

void StreamOwnership_ResetOffline(void)
{
    R2_BufferPool_Reset();
    R2_DmaSlots_Reset();
    (void)memset(&ownership, 0, sizeof(ownership));
    ownership.last_status = STREAM_OWNERSHIP_OK;
}

StreamOwnershipStatus StreamOwnership_Initialize(uint32_t k)
{
    R2_BufferPoolSnapshot pool;
    R2_DmaSlotsSnapshot slots;

    if (ownership.initialized != 0U)
    {
        return RecordFailure(STREAM_OWNERSHIP_INVALID_STATE);
    }

    if (!SupportedK(k))
    {
        ownership.last_status = STREAM_OWNERSHIP_INVALID_ARGUMENT;
        return STREAM_OWNERSHIP_INVALID_ARGUMENT;
    }

    R2_BufferPool_Reset();
    R2_DmaSlots_Reset();

    if ((R2_BufferPool_Activate(k) != R2_BUFFER_POOL_OK) ||
        (R2_DmaSlots_Initialize((R2_BufferId)0U,
            (R2_BufferId)1U) != R2_DMA_SLOTS_OK))
    {
        R2_BufferPool_Reset();
        R2_DmaSlots_Reset();
        return RecordFailure(STREAM_OWNERSHIP_MODEL_ERROR);
    }

    ownership.initialized = 1U;
    ownership.faulted = 0U;
    ownership.k = k;
    ownership.last_committed_sequence = 0U;
    ownership.keep_commit_count = 0U;
    ownership.rebind_commit_count = 0U;
    ownership.failure_count = 0U;
    ownership.last_status = STREAM_OWNERSHIP_OK;

    if (ReadHealthySnapshots(&pool, &slots) != STREAM_OWNERSHIP_OK)
    {
        return RecordFailure(STREAM_OWNERSHIP_MODEL_ERROR);
    }

    return STREAM_OWNERSHIP_OK;
}

StreamOwnershipStatus StreamOwnership_PrepareCompletion(
    uint32_t sequence,
    uint32_t completed_slot,
    StreamOwnershipAction action,
    R2_BufferId replacement_buffer,
    StreamOwnershipCompletionPlan *out_plan)
{
    StreamOwnershipCompletionPlan plan;
    R2_BufferPoolSnapshot pool;
    R2_DmaSlotsSnapshot slots;
    R2_DmaSlotsObservation observation;
    R2_BufferState replacement_state;
    uint32_t current_ct;

    if ((out_plan == NULL) ||
        (sequence == 0U) ||
        (completed_slot > 1U) ||
        ((action != STREAM_OWNERSHIP_KEEP) &&
         (action != STREAM_OWNERSHIP_REBIND)))
    {
        return RecordFailure(STREAM_OWNERSHIP_INVALID_ARGUMENT);
    }

    if (RequireHealthyInitialized() != STREAM_OWNERSHIP_OK)
    {
        return STREAM_OWNERSHIP_INVALID_STATE;
    }

    if (!IsNextSequence(sequence))
    {
        return RecordFailure(STREAM_OWNERSHIP_STALE_TRANSACTION);
    }

    current_ct = completed_slot ^ 1U;
    if (current_ct != (sequence & 1U))
    {
        return RecordFailure(STREAM_OWNERSHIP_STALE_TRANSACTION);
    }

    if (ReadHealthySnapshots(&pool, &slots) != STREAM_OWNERSHIP_OK)
    {
        return RecordFailure(STREAM_OWNERSHIP_MODEL_ERROR);
    }

    if ((R2_DmaSlots_ObserveCt(current_ct, &observation) !=
            R2_DMA_SLOTS_OK) ||
        ((uint32_t)observation.inactive_slot != completed_slot) ||
        (observation.mapping_epoch != slots.mapping_epoch) ||
        (observation.completed_buffer !=
            (completed_slot == 0U ? slots.m0_buffer : slots.m1_buffer)) ||
        (observation.active_buffer !=
            (completed_slot == 0U ? slots.m1_buffer : slots.m0_buffer)) ||
        (pool.states[observation.completed_buffer] !=
            R2_BUFFER_STATE_DMA_OWNED) ||
        (pool.states[observation.active_buffer] !=
            R2_BUFFER_STATE_DMA_OWNED))
    {
        return RecordFailure(STREAM_OWNERSHIP_MODEL_ERROR);
    }

    (void)memset(&plan, 0, sizeof(plan));
    plan.valid = 1U;
    plan.sequence = sequence;
    plan.completed_slot = completed_slot;
    plan.current_ct = current_ct;
    plan.action = action;
    plan.completed_buffer = observation.completed_buffer;
    plan.active_buffer = observation.active_buffer;
    plan.replacement_buffer = replacement_buffer;
    plan.mapping_epoch_before = observation.mapping_epoch;

    if (action == STREAM_OWNERSHIP_KEEP)
    {
        if (replacement_buffer != R2_BUFFER_POOL_INVALID_ID)
        {
            return RecordFailure(STREAM_OWNERSHIP_INVALID_ARGUMENT);
        }
    }
    else
    {
        if ((slots.mapping_epoch == UINT32_MAX) ||
            ((uint32_t)replacement_buffer >=
                R2_BUFFER_POOL_MAX_BUFFERS) ||
            (R2_BufferPool_GetState(replacement_buffer,
                &replacement_state) != R2_BUFFER_POOL_OK) ||
            (replacement_state != R2_BUFFER_STATE_FREE) ||
            (R2_DmaSlots_PrepareInactiveRebind(
                current_ct,
                replacement_buffer,
                &plan.rebind_plan) != R2_DMA_SLOTS_OK) ||
            (R2_DmaSlots_CheckPlanCt(
                &plan.rebind_plan,
                current_ct) != R2_DMA_SLOTS_OK) ||
            (plan.rebind_plan.completed_buffer !=
                observation.completed_buffer) ||
            (plan.rebind_plan.replacement_buffer !=
                replacement_buffer) ||
            (plan.rebind_plan.mapping_epoch !=
                observation.mapping_epoch))
        {
            return RecordFailure(STREAM_OWNERSHIP_MODEL_ERROR);
        }
    }

    *out_plan = plan;
    ownership.last_status = STREAM_OWNERSHIP_OK;
    return STREAM_OWNERSHIP_OK;
}

StreamOwnershipStatus StreamOwnership_CommitKeep(
    const StreamOwnershipCompletionPlan *plan)
{
    R2_BufferPoolSnapshot pool;
    R2_DmaSlotsSnapshot slots;

    if (RequireHealthyInitialized() != STREAM_OWNERSHIP_OK)
    {
        return STREAM_OWNERSHIP_INVALID_STATE;
    }

    if (plan == NULL)
    {
        return RecordFailure(STREAM_OWNERSHIP_INVALID_ARGUMENT);
    }

    if (!PlanShapeValid(plan) ||
        (plan->action != STREAM_OWNERSHIP_KEEP) ||
        !IsNextSequence(plan->sequence))
    {
        return RecordFailure(STREAM_OWNERSHIP_STALE_TRANSACTION);
    }

    if (ReadHealthySnapshots(&pool, &slots) != STREAM_OWNERSHIP_OK)
    {
        return RecordFailure(STREAM_OWNERSHIP_MODEL_ERROR);
    }

    if ((slots.mapping_epoch != plan->mapping_epoch_before) ||
        (slots.m0_buffer !=
            (plan->completed_slot == 0U ?
                plan->completed_buffer : plan->active_buffer)) ||
        (slots.m1_buffer !=
            (plan->completed_slot == 0U ?
                plan->active_buffer : plan->completed_buffer)) ||
        (pool.states[plan->completed_buffer] !=
            R2_BUFFER_STATE_DMA_OWNED) ||
        (pool.states[plan->active_buffer] !=
            R2_BUFFER_STATE_DMA_OWNED))
    {
        return RecordFailure(STREAM_OWNERSHIP_STALE_TRANSACTION);
    }

    ownership.last_committed_sequence = plan->sequence;
    ++ownership.keep_commit_count;
    ownership.last_status = STREAM_OWNERSHIP_OK;
    return STREAM_OWNERSHIP_OK;
}

StreamOwnershipStatus StreamOwnership_CommitRebind(
    const StreamOwnershipCompletionPlan *plan,
    StreamOwnershipDescriptor *out_descriptor)
{
    R2_BufferPoolSnapshot pool;
    R2_DmaSlotsSnapshot slots;
    StreamOwnershipDescriptor descriptor;

    if (RequireHealthyInitialized() != STREAM_OWNERSHIP_OK)
    {
        return STREAM_OWNERSHIP_INVALID_STATE;
    }

    if ((plan == NULL) || (out_descriptor == NULL))
    {
        return RecordFailure(STREAM_OWNERSHIP_INVALID_ARGUMENT);
    }

    if (!PlanShapeValid(plan) ||
        (plan->action != STREAM_OWNERSHIP_REBIND) ||
        !IsNextSequence(plan->sequence))
    {
        return RecordFailure(STREAM_OWNERSHIP_STALE_TRANSACTION);
    }

    if (ReadHealthySnapshots(&pool, &slots) != STREAM_OWNERSHIP_OK)
    {
        return RecordFailure(STREAM_OWNERSHIP_MODEL_ERROR);
    }

    if ((slots.mapping_epoch != plan->mapping_epoch_before) ||
        (slots.m0_buffer !=
            (plan->completed_slot == 0U ?
                plan->completed_buffer : plan->active_buffer)) ||
        (slots.m1_buffer !=
            (plan->completed_slot == 0U ?
                plan->active_buffer : plan->completed_buffer)) ||
        (pool.states[plan->completed_buffer] !=
            R2_BUFFER_STATE_DMA_OWNED) ||
        (pool.states[plan->active_buffer] !=
            R2_BUFFER_STATE_DMA_OWNED) ||
        (pool.states[plan->replacement_buffer] !=
            R2_BUFFER_STATE_FREE) ||
        (R2_DmaSlots_CheckPlanCt(
            &plan->rebind_plan,
            plan->current_ct) != R2_DMA_SLOTS_OK))
    {
        return RecordFailure(STREAM_OWNERSHIP_STALE_TRANSACTION);
    }

    /*
     * Historical R2 ordering is binding: mapping first, ownership second.
     * Hardware has already committed before this API is entered.
     */
    if (R2_DmaSlots_CommitPreparedRebind(
            &plan->rebind_plan) != R2_DMA_SLOTS_OK)
    {
        return RecordFailure(STREAM_OWNERSHIP_STALE_TRANSACTION);
    }

    if (R2_BufferPool_CommitDmaRotation(
            plan->completed_buffer,
            plan->replacement_buffer) != R2_BUFFER_POOL_OK)
    {
        return RecordFailure(STREAM_OWNERSHIP_PARTIAL_LOGICAL_COMMIT);
    }

    /* Both low-level logical records have committed. Advance core counters
     * before validating the composed post-state so cross-model invariants
     * describe the new committed transaction. */
    ownership.last_committed_sequence = plan->sequence;
    ++ownership.rebind_commit_count;

    if (ReadHealthySnapshots(&pool, &slots) != STREAM_OWNERSHIP_OK)
    {
        return RecordFailure(STREAM_OWNERSHIP_POST_COMMIT_INVARIANT);
    }

    if ((slots.mapping_epoch != plan->mapping_epoch_before + 1U) ||
        (slots.m0_buffer !=
            (plan->completed_slot == 0U ?
                plan->replacement_buffer : plan->active_buffer)) ||
        (slots.m1_buffer !=
            (plan->completed_slot == 0U ?
                plan->active_buffer : plan->replacement_buffer)) ||
        (pool.states[plan->completed_buffer] !=
            R2_BUFFER_STATE_READY) ||
        (pool.states[plan->replacement_buffer] !=
            R2_BUFFER_STATE_DMA_OWNED) ||
        (pool.states[plan->active_buffer] !=
            R2_BUFFER_STATE_DMA_OWNED))
    {
        return RecordFailure(STREAM_OWNERSHIP_POST_COMMIT_INVARIANT);
    }

    descriptor.sequence = plan->sequence;
    descriptor.buffer_id = plan->completed_buffer;
    descriptor.completed_slot = plan->completed_slot;
    descriptor.mapping_epoch = slots.mapping_epoch;

    ownership.last_status = STREAM_OWNERSHIP_OK;
    *out_descriptor = descriptor;
    return STREAM_OWNERSHIP_OK;
}

StreamOwnershipStatus StreamOwnership_GetSnapshot(
    StreamOwnershipSnapshot *out)
{
    if (out == NULL)
    {
        return STREAM_OWNERSHIP_INVALID_ARGUMENT;
    }

    (void)memset(out, 0, sizeof(*out));
    out->initialized = ownership.initialized;
    out->faulted = ownership.faulted;
    out->k = ownership.k;
    out->last_committed_sequence = ownership.last_committed_sequence;
    out->keep_commit_count = ownership.keep_commit_count;
    out->rebind_commit_count = ownership.rebind_commit_count;
    out->failure_count = ownership.failure_count;
    out->last_status = ownership.last_status;

    if (ownership.initialized == 0U)
    {
        return STREAM_OWNERSHIP_OK;
    }

    if ((R2_BufferPool_GetSnapshot(&out->pool) != R2_BUFFER_POOL_OK) ||
        (R2_DmaSlots_GetSnapshot(&out->slots) != R2_DMA_SLOTS_OK))
    {
        return STREAM_OWNERSHIP_MODEL_ERROR;
    }

    return STREAM_OWNERSHIP_OK;
}
