#include "r2_buffer_pool.h"

#include <stddef.h>
#include <string.h>

typedef struct
{
    uint32_t activated;
    uint32_t k;
    uint32_t violation_count;
    R2_BufferState states[R2_BUFFER_POOL_MAX_BUFFERS];
} R2_BufferPoolStorage;

/* The all-zero static initial state is the defined reset state. */
static R2_BufferPoolStorage pool;

static int IsSupportedK(uint32_t k)
{
    return (k == 1U) || (k == 2U) || (k == 4U) || (k == 8U);
}

static R2_BufferPoolStatus Violation(R2_BufferPoolStatus status)
{
    if (pool.violation_count != UINT32_MAX)
    {
        ++pool.violation_count;
    }
    return status;
}

/* Pure check: callers are responsible for recording one violation per call. */
static int InvariantsHold(void)
{
    uint32_t id;
    uint32_t dma_count = 0U;
    uint32_t nondma_count = 0U;

    if (pool.activated == 0U)
    {
        if (pool.k != 0U)
        {
            return 0;
        }
        for (id = 0U; id < R2_BUFFER_POOL_MAX_BUFFERS; ++id)
        {
            if (pool.states[id] != R2_BUFFER_STATE_INACTIVE)
            {
                return 0;
            }
        }
        return 1;
    }

    if ((pool.activated != 1U) || !IsSupportedK(pool.k))
    {
        return 0;
    }
    for (id = 0U; id < R2_BUFFER_POOL_MAX_BUFFERS; ++id)
    {
        if (id >= pool.k + 2U)
        {
            if (pool.states[id] != R2_BUFFER_STATE_INACTIVE)
            {
                return 0;
            }
        }
        else
        {
            switch (pool.states[id])
            {
                case R2_BUFFER_STATE_DMA_OWNED:
                    ++dma_count;
                    break;
                case R2_BUFFER_STATE_FREE:
                case R2_BUFFER_STATE_READY:
                case R2_BUFFER_STATE_PROCESSING:
                    ++nondma_count;
                    break;
                default:
                    return 0;
            }
        }
    }
    return (dma_count == 2U) && (nondma_count == pool.k);
}

static R2_BufferPoolStatus RequireActive(void)
{
    if (!InvariantsHold())
    {
        return Violation(R2_BUFFER_POOL_INVARIANT_ERROR);
    }
    if (pool.activated == 0U)
    {
        return Violation(R2_BUFFER_POOL_NOT_ACTIVE);
    }
    return R2_BUFFER_POOL_OK;
}

static R2_BufferPoolStatus Transition(
    R2_BufferId id,
    R2_BufferState expected,
    R2_BufferState next)
{
    R2_BufferPoolStatus status = RequireActive();
    if (status != R2_BUFFER_POOL_OK)
    {
        return status;
    }
    if ((uint32_t)id >= R2_BUFFER_POOL_MAX_BUFFERS)
    {
        return Violation(R2_BUFFER_POOL_ID_OUT_OF_RANGE);
    }
    if (((uint32_t)id >= pool.k + 2U) || (pool.states[id] != expected))
    {
        return Violation(R2_BUFFER_POOL_ILLEGAL_TRANSITION);
    }
    pool.states[id] = next;
    return R2_BUFFER_POOL_OK;
}

void R2_BufferPool_Reset(void)
{
    uint32_t id;
    pool.activated = 0U;
    pool.k = 0U;
    pool.violation_count = 0U;
    for (id = 0U; id < R2_BUFFER_POOL_MAX_BUFFERS; ++id)
    {
        pool.states[id] = R2_BUFFER_STATE_INACTIVE;
    }
}

R2_BufferPoolStatus R2_BufferPool_Activate(uint32_t k)
{
    uint32_t id;
    if (!IsSupportedK(k))
    {
        return Violation(R2_BUFFER_POOL_INVALID_K);
    }
    if (!InvariantsHold())
    {
        return Violation(R2_BUFFER_POOL_INVARIANT_ERROR);
    }
    if (pool.activated != 0U)
    {
        return Violation(R2_BUFFER_POOL_ALREADY_ACTIVE);
    }

    /* No subsequent failure path can leave a partially activated model. */
    for (id = 0U; id < R2_BUFFER_POOL_MAX_BUFFERS; ++id)
    {
        if (id < 2U)
        {
            pool.states[id] = R2_BUFFER_STATE_DMA_OWNED;
        }
        else if (id < k + 2U)
        {
            pool.states[id] = R2_BUFFER_STATE_FREE;
        }
        else
        {
            pool.states[id] = R2_BUFFER_STATE_INACTIVE;
        }
    }
    pool.k = k;
    pool.activated = 1U;
    return R2_BUFFER_POOL_OK;
}

R2_BufferPoolStatus R2_BufferPool_FindFree(R2_BufferId *out_id)
{
    uint32_t id;
    R2_BufferPoolStatus status;
    if (out_id == NULL)
    {
        return Violation(R2_BUFFER_POOL_INVALID_ARGUMENT);
    }
    status = RequireActive();
    if (status != R2_BUFFER_POOL_OK)
    {
        return status;
    }
    for (id = 0U; id < pool.k + 2U; ++id)
    {
        if (pool.states[id] == R2_BUFFER_STATE_FREE)
        {
            *out_id = (R2_BufferId)id;
            return R2_BUFFER_POOL_OK;
        }
    }
    return R2_BUFFER_POOL_NO_FREE_BUFFER;
}

R2_BufferPoolStatus R2_BufferPool_CommitDmaRotation(
    R2_BufferId completed_id,
    R2_BufferId replacement_id)
{
    R2_BufferPoolStatus status = RequireActive();
    if (status != R2_BUFFER_POOL_OK)
    {
        return status;
    }
    if (((uint32_t)completed_id >= R2_BUFFER_POOL_MAX_BUFFERS) ||
        ((uint32_t)replacement_id >= R2_BUFFER_POOL_MAX_BUFFERS))
    {
        return Violation(R2_BUFFER_POOL_ID_OUT_OF_RANGE);
    }
    if ((completed_id == replacement_id) ||
        ((uint32_t)completed_id >= pool.k + 2U) ||
        ((uint32_t)replacement_id >= pool.k + 2U) ||
        (pool.states[completed_id] != R2_BUFFER_STATE_DMA_OWNED) ||
        (pool.states[replacement_id] != R2_BUFFER_STATE_FREE))
    {
        return Violation(R2_BUFFER_POOL_ILLEGAL_TRANSITION);
    }

    /* Failure-atomic under the declared caller-side serialization contract. */
    pool.states[completed_id] = R2_BUFFER_STATE_READY;
    pool.states[replacement_id] = R2_BUFFER_STATE_DMA_OWNED;
    return R2_BUFFER_POOL_OK;
}

R2_BufferPoolStatus R2_BufferPool_ClaimReady(R2_BufferId id)
{
    return Transition(id, R2_BUFFER_STATE_READY, R2_BUFFER_STATE_PROCESSING);
}

R2_BufferPoolStatus R2_BufferPool_CancelReady(R2_BufferId id)
{
    return Transition(id, R2_BUFFER_STATE_READY, R2_BUFFER_STATE_FREE);
}

R2_BufferPoolStatus R2_BufferPool_ReleaseProcessing(R2_BufferId id)
{
    return Transition(id, R2_BUFFER_STATE_PROCESSING, R2_BUFFER_STATE_FREE);
}

R2_BufferPoolStatus R2_BufferPool_GetState(
    R2_BufferId id,
    R2_BufferState *out_state)
{
    if (out_state == NULL)
    {
        return Violation(R2_BUFFER_POOL_INVALID_ARGUMENT);
    }
    if ((uint32_t)id >= R2_BUFFER_POOL_MAX_BUFFERS)
    {
        return Violation(R2_BUFFER_POOL_ID_OUT_OF_RANGE);
    }
    if (!InvariantsHold())
    {
        return Violation(R2_BUFFER_POOL_INVARIANT_ERROR);
    }
    *out_state = pool.states[id];
    return R2_BUFFER_POOL_OK;
}

R2_BufferPoolStatus R2_BufferPool_GetSnapshot(R2_BufferPoolSnapshot *out)
{
    uint32_t id;
    R2_BufferPoolSnapshot snapshot;
    if (out == NULL)
    {
        return Violation(R2_BUFFER_POOL_INVALID_ARGUMENT);
    }
    if (!InvariantsHold())
    {
        return Violation(R2_BUFFER_POOL_INVARIANT_ERROR);
    }
    (void)memset(&snapshot, 0, sizeof(snapshot));
    snapshot.activated = pool.activated;
    snapshot.k = pool.k;
    snapshot.active_count = (pool.activated != 0U) ? pool.k + 2U : 0U;
    snapshot.violation_count = pool.violation_count;
    for (id = 0U; id < R2_BUFFER_POOL_MAX_BUFFERS; ++id)
    {
        snapshot.states[id] = pool.states[id];
        switch (pool.states[id])
        {
            case R2_BUFFER_STATE_INACTIVE:   ++snapshot.inactive_count; break;
            case R2_BUFFER_STATE_FREE:       ++snapshot.free_count; break;
            case R2_BUFFER_STATE_DMA_OWNED:  ++snapshot.dma_owned_count; break;
            case R2_BUFFER_STATE_READY:      ++snapshot.ready_count; break;
            case R2_BUFFER_STATE_PROCESSING: ++snapshot.processing_count; break;
            default: return Violation(R2_BUFFER_POOL_INVARIANT_ERROR);
        }
    }
    *out = snapshot;
    return R2_BUFFER_POOL_OK;
}

R2_BufferPoolStatus R2_BufferPool_Validate(void)
{
    return InvariantsHold() ? R2_BUFFER_POOL_OK :
        Violation(R2_BUFFER_POOL_INVARIANT_ERROR);
}
