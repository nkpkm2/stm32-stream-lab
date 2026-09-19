#include "stream_run_authority.h"

#include <stddef.h>
#include <string.h>

typedef struct
{
    uint32_t lease_id;
    StreamRunLeaseState state;
} StreamRunLeaseEntry;

typedef struct
{
    uint32_t initialized;
    uint32_t faulted;
    uint32_t k;
    StreamRunIdentity identity;
    uint32_t next_lease_id;

    StreamRunLeaseEntry leases[R2_BUFFER_POOL_MAX_BUFFERS];

    uint32_t held_valid;
    StreamQueueAdapterReadyDescriptor held_descriptor;

    uint32_t processing_valid;
    StreamQueueAdapterReadyDescriptor processing_descriptor;

    uint32_t pending_active;
    StreamQueueAdapterOperation pending_operation;

    uint32_t publish_count;
    uint32_t take_count;
    uint32_t claim_count;
    uint32_t cancel_count;
    uint32_t complete_count;
    uint32_t stale_run_count;
    uint32_t provenance_failure_count;
    uint32_t adapter_failure_count;
    uint32_t failure_count;
} StreamRunAuthorityStorage;

static StreamRunAuthorityStorage authority;

static int SupportedK(uint32_t k)
{
    return (k == 1U) || (k == 2U) || (k == 4U) || (k == 8U);
}

static int SameIdentity(
    const StreamRunIdentity *a,
    const StreamRunIdentity *b)
{
    return (a->boot_id == b->boot_id) &&
        (a->run_id == b->run_id) &&
        (a->generation == b->generation);
}

static int SameOwnership(
    const StreamOwnershipDescriptor *a,
    const StreamOwnershipDescriptor *b)
{
    return (a->sequence == b->sequence) &&
        (a->buffer_id == b->buffer_id) &&
        (a->completed_slot == b->completed_slot) &&
        (a->mapping_epoch == b->mapping_epoch);
}

static uint32_t ProvenanceLease(
    const StreamQueueAdapterProvenance *provenance)
{
    return provenance->words[3];
}

static int ProvenanceMatchesIdentity(
    const StreamQueueAdapterProvenance *provenance,
    const StreamRunIdentity *identity)
{
    return (provenance->words[0] == identity->boot_id) &&
        (provenance->words[1] == identity->run_id) &&
        (provenance->words[2] == identity->generation) &&
        (provenance->words[3] != 0U);
}

static void FillProvenance(
    StreamQueueAdapterProvenance *provenance,
    uint32_t lease_id)
{
    provenance->words[0] = authority.identity.boot_id;
    provenance->words[1] = authority.identity.run_id;
    provenance->words[2] = authority.identity.generation;
    provenance->words[3] = lease_id;
}

static void FillPermit(
    const StreamQueueAdapterReadyDescriptor *descriptor,
    StreamQueueAdapterPermit *permit)
{
    permit->words[0] = descriptor->provenance.words[0];
    permit->words[1] = descriptor->provenance.words[1];
    permit->words[2] = descriptor->provenance.words[2];
    permit->words[3] = descriptor->provenance.words[3];
}

static int SamePermitAndProvenance(
    const StreamQueueAdapterPermit *permit,
    const StreamQueueAdapterProvenance *provenance)
{
    return (permit->words[0] == provenance->words[0]) &&
        (permit->words[1] == provenance->words[1]) &&
        (permit->words[2] == provenance->words[2]) &&
        (permit->words[3] == provenance->words[3]);
}

static int SameReadyDescriptor(
    const StreamQueueAdapterReadyDescriptor *a,
    const StreamQueueAdapterReadyDescriptor *b)
{
    return SameOwnership(&a->ownership, &b->ownership) &&
        (a->provenance.words[0] == b->provenance.words[0]) &&
        (a->provenance.words[1] == b->provenance.words[1]) &&
        (a->provenance.words[2] == b->provenance.words[2]) &&
        (a->provenance.words[3] == b->provenance.words[3]);
}

static StreamRunAuthorityStatus LatchFault(
    StreamRunAuthorityStatus status)
{
    if (authority.initialized != 0U)
    {
        authority.faulted = 1U;
    }
    if (authority.failure_count != UINT32_MAX)
    {
        ++authority.failure_count;
    }
    return status;
}

static StreamRunAuthorityStatus RequireCurrent(
    const StreamRunTicket *ticket)
{
    if (authority.faulted != 0U)
    {
        return STREAM_RUN_AUTHORITY_FAULTED;
    }
    if (authority.initialized == 0U)
    {
        return STREAM_RUN_AUTHORITY_INVALID_STATE;
    }
    if ((ticket == NULL) ||
        !SameIdentity(&ticket->identity, &authority.identity))
    {
        if (authority.stale_run_count != UINT32_MAX)
        {
            ++authority.stale_run_count;
        }
        return LatchFault(STREAM_RUN_AUTHORITY_STALE_RUN);
    }
    return STREAM_RUN_AUTHORITY_OK;
}

static StreamRunAuthorityStatus AdapterResult(
    StreamQueueAdapterStatus status)
{
    if (status == STREAM_QUEUE_ADAPTER_OK)
    {
        return STREAM_RUN_AUTHORITY_OK;
    }
    if (status == STREAM_QUEUE_ADAPTER_EMPTY)
    {
        return STREAM_RUN_AUTHORITY_EMPTY;
    }
    if (authority.adapter_failure_count != UINT32_MAX)
    {
        ++authority.adapter_failure_count;
    }
    return LatchFault(STREAM_RUN_AUTHORITY_ADAPTER_ERROR);
}

static void ClearHeld(void)
{
    (void)memset(&authority.held_descriptor, 0,
        sizeof(authority.held_descriptor));
    authority.held_descriptor.ownership.buffer_id =
        R2_BUFFER_POOL_INVALID_ID;
    authority.held_valid = 0U;
}

static void ClearProcessing(void)
{
    (void)memset(&authority.processing_descriptor, 0,
        sizeof(authority.processing_descriptor));
    authority.processing_descriptor.ownership.buffer_id =
        R2_BUFFER_POOL_INVALID_ID;
    authority.processing_valid = 0U;
}

static uint32_t CountLiveLeases(void)
{
    uint32_t id;
    uint32_t count = 0U;

    for (id = 0U; id < R2_BUFFER_POOL_MAX_BUFFERS; ++id)
    {
        if (authority.leases[id].state != STREAM_RUN_LEASE_NONE)
        {
            ++count;
        }
    }
    return count;
}

static int LeaseEntryMatches(
    const StreamQueueAdapterReadyDescriptor *descriptor,
    StreamRunLeaseState expected_state)
{
    R2_BufferId id = descriptor->ownership.buffer_id;
    uint32_t lease_id = ProvenanceLease(&descriptor->provenance);

    if ((uint32_t)id >= R2_BUFFER_POOL_MAX_BUFFERS)
    {
        return 0;
    }
    return (authority.leases[id].state == expected_state) &&
        (authority.leases[id].lease_id == lease_id);
}

static int AuthorizeQueueOperation(
    StreamQueueAdapterOperation operation,
    R2_BufferId id,
    const StreamQueueAdapterReadyDescriptor *ready_descriptor,
    const StreamQueueAdapterPermit *permit,
    void *context)
{
    const StreamQueueAdapterReadyDescriptor *expected = NULL;
    StreamRunLeaseState expected_state = STREAM_RUN_LEASE_NONE;

    if ((context != &authority) ||
        (authority.initialized == 0U) ||
        (authority.faulted != 0U) ||
        (authority.pending_active == 0U) ||
        (operation != authority.pending_operation) ||
        (ready_descriptor == NULL) ||
        (permit == NULL) ||
        (id != ready_descriptor->ownership.buffer_id) ||
        !ProvenanceMatchesIdentity(
            &ready_descriptor->provenance, &authority.identity) ||
        !SamePermitAndProvenance(
            permit, &ready_descriptor->provenance))
    {
        return 0;
    }

    if ((operation == STREAM_QUEUE_ADAPTER_OP_READY_CLAIM) ||
        (operation == STREAM_QUEUE_ADAPTER_OP_CANCEL))
    {
        if (authority.held_valid == 0U)
        {
            return 0;
        }
        expected = &authority.held_descriptor;
        expected_state = STREAM_RUN_LEASE_HELD;
    }
    else if (operation == STREAM_QUEUE_ADAPTER_OP_COMPLETE)
    {
        if (authority.processing_valid == 0U)
        {
            return 0;
        }
        expected = &authority.processing_descriptor;
        expected_state = STREAM_RUN_LEASE_PROCESSING;
    }
    else
    {
        return 0;
    }

    return SameReadyDescriptor(ready_descriptor, expected) &&
        LeaseEntryMatches(ready_descriptor, expected_state);
}

static StreamRunAuthorityStatus BeginPending(
    StreamQueueAdapterOperation operation)
{
    if (authority.pending_active != 0U)
    {
        return LatchFault(STREAM_RUN_AUTHORITY_INVALID_STATE);
    }
    authority.pending_active = 1U;
    authority.pending_operation = operation;
    return STREAM_RUN_AUTHORITY_OK;
}

static void EndPending(void)
{
    authority.pending_active = 0U;
    authority.pending_operation = STREAM_QUEUE_ADAPTER_OP_INIT;
}

StreamRunAuthorityStatus StreamRunAuthority_Initialize(
    uint32_t k,
    const StreamRunIdentity *identity,
    StreamRunTicket *out_ticket)
{
    StreamQueueAdapterStatus adapter_status;

    if ((identity == NULL) || (out_ticket == NULL) || !SupportedK(k))
    {
        return STREAM_RUN_AUTHORITY_INVALID_ARGUMENT;
    }
    if ((authority.initialized != 0U) || (authority.faulted != 0U))
    {
        return STREAM_RUN_AUTHORITY_INVALID_STATE;
    }

    (void)memset(&authority, 0, sizeof(authority));
    authority.initialized = 1U;
    authority.k = k;
    authority.identity = *identity;
    authority.next_lease_id = 1U;
    ClearHeld();
    ClearProcessing();

    adapter_status = StreamQueueAdapter_Initialize(
        k, AuthorizeQueueOperation, &authority);
    if (adapter_status != STREAM_QUEUE_ADAPTER_OK)
    {
        if (authority.adapter_failure_count != UINT32_MAX)
        {
            ++authority.adapter_failure_count;
        }
        return LatchFault(STREAM_RUN_AUTHORITY_ADAPTER_ERROR);
    }

    out_ticket->identity = *identity;
    return STREAM_RUN_AUTHORITY_OK;
}

StreamRunAuthorityStatus StreamRunAuthority_ResetOffline(
    const StreamRunTicket *ticket)
{
    StreamRunAuthorityStatus status = RequireCurrent(ticket);
    StreamQueueAdapterStatus adapter_status;
    uint32_t id;

    if (status != STREAM_RUN_AUTHORITY_OK)
    {
        return status;
    }
    if ((authority.pending_active != 0U) ||
        (authority.held_valid != 0U) ||
        (authority.processing_valid != 0U) ||
        (CountLiveLeases() != 0U))
    {
        return LatchFault(STREAM_RUN_AUTHORITY_INVALID_STATE);
    }

    adapter_status = StreamQueueAdapter_ResetOffline();
    if (adapter_status != STREAM_QUEUE_ADAPTER_OK)
    {
        if (authority.adapter_failure_count != UINT32_MAX)
        {
            ++authority.adapter_failure_count;
        }
        return LatchFault(STREAM_RUN_AUTHORITY_ADAPTER_ERROR);
    }

    (void)memset(&authority, 0, sizeof(authority));
    for (id = 0U; id < R2_BUFFER_POOL_MAX_BUFFERS; ++id)
    {
        authority.leases[id].state = STREAM_RUN_LEASE_NONE;
    }
    ClearHeld();
    ClearProcessing();
    return STREAM_RUN_AUTHORITY_OK;
}

StreamRunAuthorityStatus StreamRunAuthority_BindProcessingTask(
    const StreamRunTicket *ticket,
    TaskHandle_t processing_task)
{
    StreamRunAuthorityStatus status = RequireCurrent(ticket);

    if (status != STREAM_RUN_AUTHORITY_OK)
    {
        return status;
    }
    if (processing_task == NULL)
    {
        return STREAM_RUN_AUTHORITY_INVALID_ARGUMENT;
    }

    return AdapterResult(
        StreamQueueAdapter_BindProcessingTask(processing_task));
}

StreamRunAuthorityStatus StreamRunAuthority_TakeFreeFromISR(
    const StreamRunTicket *ticket,
    R2_BufferId *out_id,
    BaseType_t *higher_priority_task_woken)
{
    StreamRunAuthorityStatus status = RequireCurrent(ticket);

    if (status != STREAM_RUN_AUTHORITY_OK)
    {
        return status;
    }
    if ((out_id == NULL) || (higher_priority_task_woken == NULL))
    {
        return STREAM_RUN_AUTHORITY_INVALID_ARGUMENT;
    }

    return AdapterResult(
        StreamQueueAdapter_TakeFreeFromISR(
            out_id, higher_priority_task_woken));
}

StreamRunAuthorityStatus StreamRunAuthority_PublishReadyFromISR(
    const StreamRunTicket *ticket,
    const StreamOwnershipDescriptor *ownership,
    BaseType_t *higher_priority_task_woken)
{
    StreamRunAuthorityStatus status = RequireCurrent(ticket);
    StreamQueueAdapterReadyDescriptor descriptor;
    StreamQueueAdapterStatus adapter_status;
    R2_BufferId id;
    uint32_t lease_id;

    if (status != STREAM_RUN_AUTHORITY_OK)
    {
        return status;
    }
    if ((ownership == NULL) || (higher_priority_task_woken == NULL))
    {
        return STREAM_RUN_AUTHORITY_INVALID_ARGUMENT;
    }

    id = ownership->buffer_id;
    if (((uint32_t)id >= R2_BUFFER_POOL_MAX_BUFFERS) ||
        (authority.leases[id].state != STREAM_RUN_LEASE_NONE))
    {
        if (authority.provenance_failure_count != UINT32_MAX)
        {
            ++authority.provenance_failure_count;
        }
        return LatchFault(STREAM_RUN_AUTHORITY_PROVENANCE_ERROR);
    }

    lease_id = authority.next_lease_id;
    if (lease_id == 0U)
    {
        return LatchFault(STREAM_RUN_AUTHORITY_LEASE_EXHAUSTED);
    }

    descriptor.ownership = *ownership;
    FillProvenance(&descriptor.provenance, lease_id);

    adapter_status = StreamQueueAdapter_PublishReadyFromISR(
        &descriptor, higher_priority_task_woken);
    if (adapter_status != STREAM_QUEUE_ADAPTER_OK)
    {
        if (authority.adapter_failure_count != UINT32_MAX)
        {
            ++authority.adapter_failure_count;
        }
        return LatchFault(STREAM_RUN_AUTHORITY_ADAPTER_ERROR);
    }

    authority.leases[id].lease_id = lease_id;
    authority.leases[id].state = STREAM_RUN_LEASE_READY;
    ++authority.next_lease_id;
    if (authority.publish_count != UINT32_MAX)
    {
        ++authority.publish_count;
    }
    return STREAM_RUN_AUTHORITY_OK;
}

StreamRunAuthorityStatus StreamRunAuthority_TakeReady(
    const StreamRunTicket *ticket,
    StreamOwnershipDescriptor *out_ownership)
{
    StreamRunAuthorityStatus status = RequireCurrent(ticket);
    StreamQueueAdapterReadyDescriptor descriptor;
    StreamQueueAdapterStatus adapter_status;

    if (status != STREAM_RUN_AUTHORITY_OK)
    {
        return status;
    }
    if (out_ownership == NULL)
    {
        return STREAM_RUN_AUTHORITY_INVALID_ARGUMENT;
    }
    if ((authority.held_valid != 0U) ||
        (authority.processing_valid != 0U))
    {
        return LatchFault(STREAM_RUN_AUTHORITY_INVALID_STATE);
    }

    adapter_status = StreamQueueAdapter_TakeReady(&descriptor);
    if (adapter_status == STREAM_QUEUE_ADAPTER_EMPTY)
    {
        return STREAM_RUN_AUTHORITY_EMPTY;
    }
    if (adapter_status != STREAM_QUEUE_ADAPTER_OK)
    {
        if (authority.adapter_failure_count != UINT32_MAX)
        {
            ++authority.adapter_failure_count;
        }
        return LatchFault(STREAM_RUN_AUTHORITY_ADAPTER_ERROR);
    }

    if (!ProvenanceMatchesIdentity(
            &descriptor.provenance, &authority.identity) ||
        !LeaseEntryMatches(&descriptor, STREAM_RUN_LEASE_READY))
    {
        if (authority.provenance_failure_count != UINT32_MAX)
        {
            ++authority.provenance_failure_count;
        }
        return LatchFault(STREAM_RUN_AUTHORITY_PROVENANCE_ERROR);
    }

    authority.leases[descriptor.ownership.buffer_id].state =
        STREAM_RUN_LEASE_HELD;
    authority.held_descriptor = descriptor;
    authority.held_valid = 1U;
    if (authority.take_count != UINT32_MAX)
    {
        ++authority.take_count;
    }

    *out_ownership = descriptor.ownership;
    return STREAM_RUN_AUTHORITY_OK;
}

StreamRunAuthorityStatus StreamRunAuthority_ClaimHeldReady(
    const StreamRunTicket *ticket)
{
    StreamRunAuthorityStatus status = RequireCurrent(ticket);
    StreamQueueAdapterPermit permit;
    StreamQueueAdapterStatus adapter_status;
    R2_BufferId id;

    if (status != STREAM_RUN_AUTHORITY_OK)
    {
        return status;
    }
    if ((authority.held_valid == 0U) ||
        (authority.processing_valid != 0U) ||
        !LeaseEntryMatches(
            &authority.held_descriptor, STREAM_RUN_LEASE_HELD))
    {
        return LatchFault(STREAM_RUN_AUTHORITY_INVALID_STATE);
    }

    FillPermit(&authority.held_descriptor, &permit);
    status = BeginPending(STREAM_QUEUE_ADAPTER_OP_READY_CLAIM);
    if (status != STREAM_RUN_AUTHORITY_OK)
    {
        return status;
    }

    adapter_status = StreamQueueAdapter_ClaimHeldReady(&permit);
    EndPending();

    if (adapter_status != STREAM_QUEUE_ADAPTER_OK)
    {
        if (authority.adapter_failure_count != UINT32_MAX)
        {
            ++authority.adapter_failure_count;
        }
        return LatchFault(STREAM_RUN_AUTHORITY_ADAPTER_ERROR);
    }

    id = authority.held_descriptor.ownership.buffer_id;
    authority.leases[id].state = STREAM_RUN_LEASE_PROCESSING;
    authority.processing_descriptor = authority.held_descriptor;
    authority.processing_valid = 1U;
    ClearHeld();
    if (authority.claim_count != UINT32_MAX)
    {
        ++authority.claim_count;
    }
    return STREAM_RUN_AUTHORITY_OK;
}

StreamRunAuthorityStatus StreamRunAuthority_CancelHeldReady(
    const StreamRunTicket *ticket,
    StreamQueueAdapterReceipt *out_receipt)
{
    StreamRunAuthorityStatus status = RequireCurrent(ticket);
    StreamQueueAdapterPermit permit;
    StreamQueueAdapterStatus adapter_status;
    R2_BufferId id;

    if (status != STREAM_RUN_AUTHORITY_OK)
    {
        return status;
    }
    if (out_receipt == NULL)
    {
        return STREAM_RUN_AUTHORITY_INVALID_ARGUMENT;
    }
    if ((authority.held_valid == 0U) ||
        !LeaseEntryMatches(
            &authority.held_descriptor, STREAM_RUN_LEASE_HELD))
    {
        return LatchFault(STREAM_RUN_AUTHORITY_INVALID_STATE);
    }

    id = authority.held_descriptor.ownership.buffer_id;
    FillPermit(&authority.held_descriptor, &permit);
    status = BeginPending(STREAM_QUEUE_ADAPTER_OP_CANCEL);
    if (status != STREAM_RUN_AUTHORITY_OK)
    {
        return status;
    }

    adapter_status = StreamQueueAdapter_CancelHeldReady(
        &permit, out_receipt);
    EndPending();

    if (adapter_status != STREAM_QUEUE_ADAPTER_OK)
    {
        if (authority.adapter_failure_count != UINT32_MAX)
        {
            ++authority.adapter_failure_count;
        }
        return LatchFault(STREAM_RUN_AUTHORITY_ADAPTER_ERROR);
    }

    authority.leases[id].state = STREAM_RUN_LEASE_NONE;
    authority.leases[id].lease_id = 0U;
    ClearHeld();
    if (authority.cancel_count != UINT32_MAX)
    {
        ++authority.cancel_count;
    }
    return STREAM_RUN_AUTHORITY_OK;
}

StreamRunAuthorityStatus StreamRunAuthority_CompleteAndReleaseBlock(
    const StreamRunTicket *ticket,
    StreamQueueAdapterReceipt *out_receipt)
{
    StreamRunAuthorityStatus status = RequireCurrent(ticket);
    StreamQueueAdapterPermit permit;
    StreamQueueAdapterStatus adapter_status;
    R2_BufferId id;

    if (status != STREAM_RUN_AUTHORITY_OK)
    {
        return status;
    }
    if (out_receipt == NULL)
    {
        return STREAM_RUN_AUTHORITY_INVALID_ARGUMENT;
    }
    if ((authority.processing_valid == 0U) ||
        !LeaseEntryMatches(
            &authority.processing_descriptor,
            STREAM_RUN_LEASE_PROCESSING))
    {
        return LatchFault(STREAM_RUN_AUTHORITY_INVALID_STATE);
    }

    id = authority.processing_descriptor.ownership.buffer_id;
    FillPermit(&authority.processing_descriptor, &permit);
    status = BeginPending(STREAM_QUEUE_ADAPTER_OP_COMPLETE);
    if (status != STREAM_RUN_AUTHORITY_OK)
    {
        return status;
    }

    adapter_status = StreamQueueAdapter_CompleteAndReleaseBlock(
        &permit, out_receipt);
    EndPending();

    if (adapter_status != STREAM_QUEUE_ADAPTER_OK)
    {
        if (authority.adapter_failure_count != UINT32_MAX)
        {
            ++authority.adapter_failure_count;
        }
        return LatchFault(STREAM_RUN_AUTHORITY_ADAPTER_ERROR);
    }

    authority.leases[id].state = STREAM_RUN_LEASE_NONE;
    authority.leases[id].lease_id = 0U;
    ClearProcessing();
    if (authority.complete_count != UINT32_MAX)
    {
        ++authority.complete_count;
    }
    return STREAM_RUN_AUTHORITY_OK;
}

StreamRunAuthorityStatus StreamRunAuthority_GetSnapshot(
    StreamRunAuthoritySnapshot *out)
{
    if (out == NULL)
    {
        return STREAM_RUN_AUTHORITY_INVALID_ARGUMENT;
    }

    (void)memset(out, 0, sizeof(*out));
    out->initialized = authority.initialized;
    out->faulted = authority.faulted;
    out->k = authority.k;
    out->identity = authority.identity;
    out->next_lease_id = authority.next_lease_id;
    out->live_lease_count = CountLiveLeases();
    out->held_valid = authority.held_valid;
    out->held_buffer = authority.held_valid != 0U ?
        authority.held_descriptor.ownership.buffer_id :
        R2_BUFFER_POOL_INVALID_ID;
    out->held_lease_id = authority.held_valid != 0U ?
        ProvenanceLease(&authority.held_descriptor.provenance) : 0U;
    out->processing_valid = authority.processing_valid;
    out->processing_buffer = authority.processing_valid != 0U ?
        authority.processing_descriptor.ownership.buffer_id :
        R2_BUFFER_POOL_INVALID_ID;
    out->processing_lease_id = authority.processing_valid != 0U ?
        ProvenanceLease(&authority.processing_descriptor.provenance) : 0U;
    out->publish_count = authority.publish_count;
    out->take_count = authority.take_count;
    out->claim_count = authority.claim_count;
    out->cancel_count = authority.cancel_count;
    out->complete_count = authority.complete_count;
    out->stale_run_count = authority.stale_run_count;
    out->provenance_failure_count = authority.provenance_failure_count;
    out->adapter_failure_count = authority.adapter_failure_count;
    out->failure_count = authority.failure_count;
    return STREAM_RUN_AUTHORITY_OK;
}
