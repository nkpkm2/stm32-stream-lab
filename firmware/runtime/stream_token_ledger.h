#ifndef STREAM_TOKEN_LEDGER_H
#define STREAM_TOKEN_LEDGER_H

#include <stdint.h>

#include "r2_buffer_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * R2->R3 Runtime Foundation Consolidation, F0-3 Stage 2A.
 *
 * Pure token-authority mirror.  There is no FreeRTOS dependency here.
 *
 * Queue transport and semantic ownership deliberately remain separate:
 *   - FreeQueue / ReadyQueue will be the transport authority in Stage 2B.
 *   - R2_BufferPool remains the semantic ownership model.
 *   - this ledger is the validation mirror that makes duplicate/missing/
 *     wrong-source token operations fail closed.
 *
 * The ledger models the two transaction-isolation windows that are not stable
 * reconciliation points:
 *
 *   admission_active:
 *     a FREE token has been removed for a replacement buffer, but READY
 *     publication for the completed buffer has not committed yet.
 *
 *   ready_hold_active:
 *     a READY descriptor/token has been removed by the Processing path, but
 *     the READY owner has not yet been resolved to PROCESSING (claim) or FREE
 *     (CANCEL).
 *
 * These two windows may coexist: a DMA completion can preempt a Processing
 * path that already holds a dequeued READY descriptor.  All calls are
 * externally serialized; this module provides no locks or atomics.
 *
 * ValidateStable() is legal only when neither transition window is active.
 */

typedef enum
{
    STREAM_TOKEN_OK = 0,
    STREAM_TOKEN_INVALID_ARGUMENT,
    STREAM_TOKEN_INVALID_STATE,
    STREAM_TOKEN_INVALID_K,
    STREAM_TOKEN_ID_OUT_OF_RANGE,
    STREAM_TOKEN_DUPLICATE,
    STREAM_TOKEN_MISSING,
    STREAM_TOKEN_WRONG_SOURCE,
    STREAM_TOKEN_TRANSACTION_ACTIVE,
    STREAM_TOKEN_TRANSACTION_MISSING,
    STREAM_TOKEN_RECONCILE_ERROR
} StreamTokenStatus;

typedef enum
{
    STREAM_TOKEN_FREE_INIT = 0,
    STREAM_TOKEN_FREE_CANCEL,
    STREAM_TOKEN_FREE_COMPLETE
} StreamTokenFreeSource;

typedef struct
{
    uint32_t initialized;
    uint32_t sealed;
    uint32_t faulted;
    uint32_t k;
    uint32_t active_count;
    uint32_t active_mask;
    uint32_t free_mask;
    uint32_t ready_mask;

    uint32_t admission_active;
    R2_BufferId admission_replacement;

    uint32_t ready_hold_active;
    R2_BufferId ready_hold_buffer;

    uint32_t init_send_count;
    uint32_t free_take_count;
    uint32_t ready_publish_count;
    uint32_t ready_take_count;
    uint32_t ready_claim_count;
    uint32_t cancel_send_count;
    uint32_t complete_send_count;
    uint32_t failure_count;
    StreamTokenStatus last_status;
} StreamTokenSnapshot;

void StreamTokenLedger_ResetOffline(void);

StreamTokenStatus StreamTokenLedger_Initialize(uint32_t k);

/*
 * Ledger-side commit for a FreeQueue send.
 *
 * This function is intentionally NOT an ownership/caller authorization oracle.
 * Stage 2B's queue adapter must first authenticate the explicit operation
 * context (caller, lease, generation) and perform/verify the matching semantic
 * owner transition inside the protected queue-send transaction:
 *
 *   INIT:     pool already represents the rebuilt FREE set.
 *   CANCEL:   held READY owner has committed READY -> FREE.
 *   COMPLETE: current PROCESSING lease has committed PROCESSING -> FREE.
 *
 * Only after those semantic preconditions hold may the adapter call this
 * function to commit the token mirror.  This layer then enforces source-class
 * transaction context and token uniqueness.  If either semantic or ledger
 * commit fails after queue-send serialization has begun, Stage 2B must latch
 * the infrastructure fault and prevent the resulting token from being consumed
 * by normal DMA admission.
 *
 * INIT is the only legal free-token source before SealInitialization().
 */
StreamTokenStatus StreamTokenLedger_RecordFreeSend(
    StreamTokenFreeSource source,
    R2_BufferId id);

/* Requires exactly B2..B(K+1) to have one FREE token each. */
StreamTokenStatus StreamTokenLedger_SealInitialization(void);

/*
 * Mirrors a successful FreeQueue receive in the DMA completion path.
 * The token is removed immediately and one admission transaction opens.
 */
StreamTokenStatus StreamTokenLedger_TakeFreeForAdmission(
    R2_BufferId replacement_id);

/*
 * Mirrors successful ReadyQueue publication after the physical and logical
 * REBIND commits have both succeeded.  This closes admission_active.
 */
StreamTokenStatus StreamTokenLedger_PublishReady(
    R2_BufferId completed_id);

/*
 * Mirrors a successful ReadyQueue receive.  The READY token disappears from
 * the queue/ledger, but the semantic owner is still READY until Claim or
 * CANCEL resolves the hold.
 */
StreamTokenStatus StreamTokenLedger_TakeReady(
    R2_BufferId id);

/* Close a held READY descriptor after BufferPool READY->PROCESSING succeeds. */
StreamTokenStatus StreamTokenLedger_CommitReadyClaim(
    R2_BufferId id);

/*
 * Stable-point reconciliation.  FREE and READY bits must exactly match the
 * BufferPool owner states.  DMA_OWNED and PROCESSING carry no queue token.
 */
StreamTokenStatus StreamTokenLedger_ValidateStable(
    const R2_BufferPoolSnapshot *pool);

StreamTokenStatus StreamTokenLedger_GetSnapshot(
    StreamTokenSnapshot *out);

#ifdef __cplusplus
}
#endif

#endif
