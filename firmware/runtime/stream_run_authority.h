#ifndef STREAM_RUN_AUTHORITY_H
#define STREAM_RUN_AUTHORITY_H

#include <stdint.h>

#include "stream_queue_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * R2->R3 Runtime Foundation Consolidation, F0-3 Stage 2C.
 *
 * Run-scoped lease/provenance authority bridge.
 *
 * This layer does NOT own the R3 lifecycle state machine. Communication will
 * remain the sole START/STOP coordinator. Instead, the coordinator supplies
 * the current immutable boot/run/generation identity during offline run
 * preparation. Stage 2C then:
 *
 *   - mints every READY provenance internally;
 *   - assigns one nonzero lease_id to each published READY descriptor;
 *   - keeps a per-buffer live-lease registry across READY/HELD/PROCESSING;
 *   - requires callers to present the run ticket for every normal operation;
 *   - constructs QueueAdapter permits internally;
 *   - supplies the bounded QueueAdapter authorizer;
 *   - rejects stale/wrong-run tickets and unminted/forged READY provenance;
 *   - exposes no API that accepts caller-selected provenance or permits.
 *
 * The underlying StreamQueueAdapter remains queue transport/protected-commit
 * authority. AdcDbmDriver, DmaSlots, StreamOwnership, and StreamTokenLedger
 * retain their existing authorities.
 *
 * A stale/wrong-run operation is an infrastructure fault, not a benign miss.
 * Once faulted, the authority refuses normal service; controlled/full-reset
 * recovery belongs to R3.
 */

typedef struct
{
    uint32_t boot_id;
    uint32_t run_id;
    uint32_t generation;
} StreamRunIdentity;

typedef struct
{
    StreamRunIdentity identity;
} StreamRunTicket;

typedef enum
{
    STREAM_RUN_AUTHORITY_OK = 0,
    STREAM_RUN_AUTHORITY_EMPTY,
    STREAM_RUN_AUTHORITY_INVALID_ARGUMENT,
    STREAM_RUN_AUTHORITY_INVALID_STATE,
    STREAM_RUN_AUTHORITY_FAULTED,
    STREAM_RUN_AUTHORITY_STALE_RUN,
    STREAM_RUN_AUTHORITY_LEASE_EXHAUSTED,
    STREAM_RUN_AUTHORITY_ADAPTER_ERROR,
    STREAM_RUN_AUTHORITY_PROVENANCE_ERROR
} StreamRunAuthorityStatus;

typedef enum
{
    STREAM_RUN_LEASE_NONE = 0,
    STREAM_RUN_LEASE_READY,
    STREAM_RUN_LEASE_HELD,
    STREAM_RUN_LEASE_PROCESSING
} StreamRunLeaseState;

typedef struct
{
    uint32_t initialized;
    uint32_t faulted;
    uint32_t k;
    StreamRunIdentity identity;
    uint32_t next_lease_id;
    uint32_t live_lease_count;
    uint32_t held_valid;
    R2_BufferId held_buffer;
    uint32_t held_lease_id;
    uint32_t processing_valid;
    R2_BufferId processing_buffer;
    uint32_t processing_lease_id;
    uint32_t publish_count;
    uint32_t take_count;
    uint32_t claim_count;
    uint32_t cancel_count;
    uint32_t complete_count;
    uint32_t stale_run_count;
    uint32_t provenance_failure_count;
    uint32_t adapter_failure_count;
    uint32_t failure_count;
} StreamRunAuthoritySnapshot;

/*
 * Offline run preparation boundary. StreamOwnership must already be healthy
 * and initialized for K. On success QueueAdapter is initialized with the
 * Stage-2C bounded authorizer and out_ticket becomes the sole normal run ticket
 * for this authority instance.
 *
 * Stage 2C assigns no lifecycle meaning beyond immutable identity equality:
 * R3 owns when a new boot_id/run_id/generation becomes current.
 */
StreamRunAuthorityStatus StreamRunAuthority_Initialize(
    uint32_t k,
    const StreamRunIdentity *identity,
    StreamRunTicket *out_ticket);

/*
 * Normal offline reset boundary after DMA quiescence and worker resource
 * return. This delegates the actual queue/token reset to StreamQueueAdapter.
 * Active Stage-2C leases are forbidden. Faulted state deliberately cannot be
 * repaired here.
 */
StreamRunAuthorityStatus StreamRunAuthority_ResetOffline(
    const StreamRunTicket *ticket);

StreamRunAuthorityStatus StreamRunAuthority_BindProcessingTask(
    const StreamRunTicket *ticket,
    TaskHandle_t processing_task);

/* DMA/ISR path. The ticket must be the current run ticket. */
StreamRunAuthorityStatus StreamRunAuthority_TakeFreeFromISR(
    const StreamRunTicket *ticket,
    R2_BufferId *out_id,
    BaseType_t *higher_priority_task_woken);

/*
 * Publish a logically committed ownership descriptor as READY. The caller
 * supplies ownership only; boot/run/generation/lease provenance is minted here
 * and cannot be selected through the normal API.
 */
StreamRunAuthorityStatus StreamRunAuthority_PublishReadyFromISR(
    const StreamRunTicket *ticket,
    const StreamOwnershipDescriptor *ownership,
    BaseType_t *higher_priority_task_woken);

/*
 * Processing path. TakeReady returns only semantic ownership metadata. The
 * exact READY descriptor/provenance remains mirrored internally and is the
 * only descriptor eligible for Claim or CANCEL.
 */
StreamRunAuthorityStatus StreamRunAuthority_TakeReady(
    const StreamRunTicket *ticket,
    StreamOwnershipDescriptor *out_ownership);

StreamRunAuthorityStatus StreamRunAuthority_ClaimHeldReady(
    const StreamRunTicket *ticket);

StreamRunAuthorityStatus StreamRunAuthority_CancelHeldReady(
    const StreamRunTicket *ticket,
    StreamQueueAdapterReceipt *out_receipt);

StreamRunAuthorityStatus StreamRunAuthority_CompleteAndReleaseBlock(
    const StreamRunTicket *ticket,
    StreamQueueAdapterReceipt *out_receipt);

StreamRunAuthorityStatus StreamRunAuthority_GetSnapshot(
    StreamRunAuthoritySnapshot *out);

#ifdef __cplusplus
}
#endif

#endif
