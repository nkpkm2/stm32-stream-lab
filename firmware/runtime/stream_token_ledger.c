#include "stream_token_ledger.h"

#include <stddef.h>
#include <string.h>

typedef struct
{
    StreamTokenSnapshot snapshot;
} StreamTokenStorage;

static StreamTokenStorage ledger;

static int SupportedK(uint32_t k)
{
    return (k == 1U) || (k == 2U) || (k == 4U) || (k == 8U);
}

static uint32_t BitFor(R2_BufferId id)
{
    return 1UL << (uint32_t)id;
}

static uint32_t Popcount32(uint32_t value)
{
    uint32_t count = 0U;

    while (value != 0U)
    {
        value &= value - 1U;
        ++count;
    }
    return count;
}

static int ActiveId(R2_BufferId id)
{
    return ((uint32_t)id < ledger.snapshot.active_count) &&
        ((uint32_t)id < R2_BUFFER_POOL_MAX_BUFFERS);
}

static StreamTokenStatus Failure(StreamTokenStatus status)
{
    if (ledger.snapshot.initialized != 0U)
    {
        ledger.snapshot.faulted = 1U;
        if (ledger.snapshot.failure_count != UINT32_MAX)
        {
            ++ledger.snapshot.failure_count;
        }
    }
    ledger.snapshot.last_status = status;
    return status;
}

static StreamTokenStatus RequireHealthyInitialized(void)
{
    if ((ledger.snapshot.initialized == 0U) ||
        (ledger.snapshot.faulted != 0U))
    {
        return STREAM_TOKEN_INVALID_STATE;
    }
    return STREAM_TOKEN_OK;
}

static StreamTokenStatus RequireSealed(void)
{
    StreamTokenStatus status = RequireHealthyInitialized();

    if (status != STREAM_TOKEN_OK)
    {
        return status;
    }
    if (ledger.snapshot.sealed == 0U)
    {
        return STREAM_TOKEN_INVALID_STATE;
    }
    return STREAM_TOKEN_OK;
}

static int HasAnyToken(R2_BufferId id)
{
    uint32_t bit = BitFor(id);
    return ((ledger.snapshot.free_mask | ledger.snapshot.ready_mask) & bit) != 0U;
}

void StreamTokenLedger_ResetOffline(void)
{
    (void)memset(&ledger, 0, sizeof(ledger));
    ledger.snapshot.admission_replacement = R2_BUFFER_POOL_INVALID_ID;
    ledger.snapshot.ready_hold_buffer = R2_BUFFER_POOL_INVALID_ID;
    ledger.snapshot.last_status = STREAM_TOKEN_OK;
}

StreamTokenStatus StreamTokenLedger_Initialize(uint32_t k)
{
    uint32_t active_count;

    if (ledger.snapshot.initialized != 0U)
    {
        return Failure(STREAM_TOKEN_INVALID_STATE);
    }
    if (!SupportedK(k))
    {
        ledger.snapshot.last_status = STREAM_TOKEN_INVALID_K;
        return STREAM_TOKEN_INVALID_K;
    }

    active_count = k + 2U;

    (void)memset(&ledger, 0, sizeof(ledger));
    ledger.snapshot.initialized = 1U;
    ledger.snapshot.k = k;
    ledger.snapshot.active_count = active_count;
    ledger.snapshot.active_mask = (1UL << active_count) - 1UL;
    ledger.snapshot.admission_replacement = R2_BUFFER_POOL_INVALID_ID;
    ledger.snapshot.ready_hold_buffer = R2_BUFFER_POOL_INVALID_ID;
    ledger.snapshot.last_status = STREAM_TOKEN_OK;
    return STREAM_TOKEN_OK;
}

StreamTokenStatus StreamTokenLedger_RecordFreeSend(
    StreamTokenFreeSource source,
    R2_BufferId id)
{
    uint32_t bit;
    StreamTokenStatus status = RequireHealthyInitialized();

    if (status != STREAM_TOKEN_OK)
    {
        return status;
    }
    if (!ActiveId(id))
    {
        return Failure(STREAM_TOKEN_ID_OUT_OF_RANGE);
    }

    bit = BitFor(id);

    /*
     * Authorization precedes uniqueness. WRONG_SOURCE means the caller is
     * not entitled to perform this FreeQueue-send role in the current
     * transaction context; DUPLICATE is meaningful only after that role has
     * been authorized.
     */
    if (source == STREAM_TOKEN_FREE_INIT)
    {
        if ((ledger.snapshot.sealed != 0U) || ((uint32_t)id < 2U) ||
            (ledger.snapshot.admission_active != 0U) ||
            (ledger.snapshot.ready_hold_active != 0U))
        {
            return Failure(STREAM_TOKEN_WRONG_SOURCE);
        }
        if (HasAnyToken(id))
        {
            return Failure(STREAM_TOKEN_DUPLICATE);
        }
        ledger.snapshot.free_mask |= bit;
        ++ledger.snapshot.init_send_count;
    }
    else if (source == STREAM_TOKEN_FREE_CANCEL)
    {
        if ((ledger.snapshot.sealed == 0U) ||
            (ledger.snapshot.ready_hold_active == 0U) ||
            (ledger.snapshot.ready_hold_buffer != id))
        {
            return Failure(STREAM_TOKEN_WRONG_SOURCE);
        }
        if (HasAnyToken(id))
        {
            return Failure(STREAM_TOKEN_DUPLICATE);
        }
        ledger.snapshot.free_mask |= bit;
        ledger.snapshot.ready_hold_active = 0U;
        ledger.snapshot.ready_hold_buffer = R2_BUFFER_POOL_INVALID_ID;
        ++ledger.snapshot.cancel_send_count;
    }
    else if (source == STREAM_TOKEN_FREE_COMPLETE)
    {
        if ((ledger.snapshot.sealed == 0U) ||
            ((ledger.snapshot.ready_hold_active != 0U) &&
             (ledger.snapshot.ready_hold_buffer == id)) ||
            ((ledger.snapshot.admission_active != 0U) &&
             (ledger.snapshot.admission_replacement == id)))
        {
            return Failure(STREAM_TOKEN_WRONG_SOURCE);
        }
        if (HasAnyToken(id))
        {
            return Failure(STREAM_TOKEN_DUPLICATE);
        }
        ledger.snapshot.free_mask |= bit;
        ++ledger.snapshot.complete_send_count;
    }
    else
    {
        return Failure(STREAM_TOKEN_INVALID_ARGUMENT);
    }

    ledger.snapshot.last_status = STREAM_TOKEN_OK;
    return STREAM_TOKEN_OK;
}

StreamTokenStatus StreamTokenLedger_SealInitialization(void)
{
    uint32_t expected_free;
    StreamTokenStatus status = RequireHealthyInitialized();

    if (status != STREAM_TOKEN_OK)
    {
        return status;
    }
    if (ledger.snapshot.sealed != 0U)
    {
        return Failure(STREAM_TOKEN_INVALID_STATE);
    }

    expected_free = ledger.snapshot.active_mask & ~0x3UL;
    if ((ledger.snapshot.free_mask != expected_free) ||
        (ledger.snapshot.ready_mask != 0U) ||
        (ledger.snapshot.init_send_count != ledger.snapshot.k) ||
        (ledger.snapshot.admission_active != 0U) ||
        (ledger.snapshot.ready_hold_active != 0U))
    {
        return Failure(STREAM_TOKEN_RECONCILE_ERROR);
    }

    ledger.snapshot.sealed = 1U;
    ledger.snapshot.last_status = STREAM_TOKEN_OK;
    return STREAM_TOKEN_OK;
}

StreamTokenStatus StreamTokenLedger_TakeFreeForAdmission(
    R2_BufferId replacement_id)
{
    uint32_t bit;
    StreamTokenStatus status = RequireSealed();

    if (status != STREAM_TOKEN_OK)
    {
        return status;
    }
    if (!ActiveId(replacement_id))
    {
        return Failure(STREAM_TOKEN_ID_OUT_OF_RANGE);
    }
    if (ledger.snapshot.admission_active != 0U)
    {
        return Failure(STREAM_TOKEN_TRANSACTION_ACTIVE);
    }

    bit = BitFor(replacement_id);
    if ((ledger.snapshot.free_mask & bit) == 0U)
    {
        return Failure(STREAM_TOKEN_MISSING);
    }
    if ((ledger.snapshot.ready_mask & bit) != 0U)
    {
        return Failure(STREAM_TOKEN_DUPLICATE);
    }

    ledger.snapshot.free_mask &= ~bit;
    ledger.snapshot.admission_active = 1U;
    ledger.snapshot.admission_replacement = replacement_id;
    ++ledger.snapshot.free_take_count;
    ledger.snapshot.last_status = STREAM_TOKEN_OK;
    return STREAM_TOKEN_OK;
}

StreamTokenStatus StreamTokenLedger_PublishReady(
    R2_BufferId completed_id)
{
    uint32_t bit;
    StreamTokenStatus status = RequireSealed();

    if (status != STREAM_TOKEN_OK)
    {
        return status;
    }
    if (!ActiveId(completed_id))
    {
        return Failure(STREAM_TOKEN_ID_OUT_OF_RANGE);
    }
    if (ledger.snapshot.admission_active == 0U)
    {
        return Failure(STREAM_TOKEN_TRANSACTION_MISSING);
    }
    if (completed_id == ledger.snapshot.admission_replacement)
    {
        return Failure(STREAM_TOKEN_INVALID_ARGUMENT);
    }
    if ((ledger.snapshot.ready_hold_active != 0U) &&
        (ledger.snapshot.ready_hold_buffer == completed_id))
    {
        return Failure(STREAM_TOKEN_DUPLICATE);
    }

    bit = BitFor(completed_id);
    if (HasAnyToken(completed_id))
    {
        return Failure(STREAM_TOKEN_DUPLICATE);
    }

    ledger.snapshot.ready_mask |= bit;
    ledger.snapshot.admission_active = 0U;
    ledger.snapshot.admission_replacement = R2_BUFFER_POOL_INVALID_ID;
    ++ledger.snapshot.ready_publish_count;
    ledger.snapshot.last_status = STREAM_TOKEN_OK;
    return STREAM_TOKEN_OK;
}

StreamTokenStatus StreamTokenLedger_TakeReady(
    R2_BufferId id)
{
    uint32_t bit;
    StreamTokenStatus status = RequireSealed();

    if (status != STREAM_TOKEN_OK)
    {
        return status;
    }
    if (!ActiveId(id))
    {
        return Failure(STREAM_TOKEN_ID_OUT_OF_RANGE);
    }
    if (ledger.snapshot.ready_hold_active != 0U)
    {
        return Failure(STREAM_TOKEN_TRANSACTION_ACTIVE);
    }

    bit = BitFor(id);
    if ((ledger.snapshot.ready_mask & bit) == 0U)
    {
        return Failure(STREAM_TOKEN_MISSING);
    }
    if ((ledger.snapshot.free_mask & bit) != 0U)
    {
        return Failure(STREAM_TOKEN_DUPLICATE);
    }

    ledger.snapshot.ready_mask &= ~bit;
    ledger.snapshot.ready_hold_active = 1U;
    ledger.snapshot.ready_hold_buffer = id;
    ++ledger.snapshot.ready_take_count;
    ledger.snapshot.last_status = STREAM_TOKEN_OK;
    return STREAM_TOKEN_OK;
}

StreamTokenStatus StreamTokenLedger_CommitReadyClaim(
    R2_BufferId id)
{
    StreamTokenStatus status = RequireSealed();

    if (status != STREAM_TOKEN_OK)
    {
        return status;
    }
    if (!ActiveId(id))
    {
        return Failure(STREAM_TOKEN_ID_OUT_OF_RANGE);
    }
    if ((ledger.snapshot.ready_hold_active == 0U) ||
        (ledger.snapshot.ready_hold_buffer != id))
    {
        return Failure(STREAM_TOKEN_TRANSACTION_MISSING);
    }
    if (HasAnyToken(id))
    {
        return Failure(STREAM_TOKEN_DUPLICATE);
    }

    ledger.snapshot.ready_hold_active = 0U;
    ledger.snapshot.ready_hold_buffer = R2_BUFFER_POOL_INVALID_ID;
    ++ledger.snapshot.ready_claim_count;
    ledger.snapshot.last_status = STREAM_TOKEN_OK;
    return STREAM_TOKEN_OK;
}

StreamTokenStatus StreamTokenLedger_ValidateStable(
    const R2_BufferPoolSnapshot *pool)
{
    uint32_t id;
    uint32_t expected_free = 0U;
    uint32_t expected_ready = 0U;
    StreamTokenStatus status = RequireSealed();

    if (status != STREAM_TOKEN_OK)
    {
        return status;
    }
    if (pool == NULL)
    {
        return Failure(STREAM_TOKEN_INVALID_ARGUMENT);
    }
    if ((ledger.snapshot.admission_active != 0U) ||
        (ledger.snapshot.ready_hold_active != 0U))
    {
        return Failure(STREAM_TOKEN_TRANSACTION_ACTIVE);
    }
    if ((pool->activated != 1U) ||
        (pool->k != ledger.snapshot.k) ||
        (pool->active_count != ledger.snapshot.active_count) ||
        (pool->violation_count != 0U))
    {
        return Failure(STREAM_TOKEN_RECONCILE_ERROR);
    }

    for (id = 0U; id < R2_BUFFER_POOL_MAX_BUFFERS; ++id)
    {
        uint32_t bit = 1UL << id;

        if (id >= ledger.snapshot.active_count)
        {
            if ((pool->states[id] != R2_BUFFER_STATE_INACTIVE) ||
                (((ledger.snapshot.free_mask |
                   ledger.snapshot.ready_mask) & bit) != 0U))
            {
                return Failure(STREAM_TOKEN_RECONCILE_ERROR);
            }
            continue;
        }

        if (pool->states[id] == R2_BUFFER_STATE_FREE)
        {
            expected_free |= bit;
        }
        else if (pool->states[id] == R2_BUFFER_STATE_READY)
        {
            expected_ready |= bit;
        }
        else if ((pool->states[id] != R2_BUFFER_STATE_DMA_OWNED) &&
                 (pool->states[id] != R2_BUFFER_STATE_PROCESSING))
        {
            return Failure(STREAM_TOKEN_RECONCILE_ERROR);
        }
    }

    if ((ledger.snapshot.free_mask != expected_free) ||
        (ledger.snapshot.ready_mask != expected_ready) ||
        (pool->free_count != Popcount32(ledger.snapshot.free_mask)) ||
        (pool->ready_count != Popcount32(ledger.snapshot.ready_mask)))
    {
        return Failure(STREAM_TOKEN_RECONCILE_ERROR);
    }

    ledger.snapshot.last_status = STREAM_TOKEN_OK;
    return STREAM_TOKEN_OK;
}

StreamTokenStatus StreamTokenLedger_GetSnapshot(
    StreamTokenSnapshot *out)
{
    if (out == NULL)
    {
        return STREAM_TOKEN_INVALID_ARGUMENT;
    }

    *out = ledger.snapshot;
    return STREAM_TOKEN_OK;
}
