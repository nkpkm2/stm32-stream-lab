#include "stream_run_authority.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        (void)fprintf(stderr, "CHECK failed: %s (%s:%d)\n", \
            #condition, __FILE__, __LINE__); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

typedef struct
{
    uint32_t initialized;
    uint32_t faulted;
    StreamQueueAdapterAuthorizeFn authorizer;
    void *authorizer_context;
    TaskHandle_t processing_task;
    uint32_t ready_valid;
    StreamQueueAdapterReadyDescriptor ready_descriptor;
    uint32_t held_valid;
    StreamQueueAdapterReadyDescriptor held_descriptor;
    uint32_t processing_valid;
    StreamQueueAdapterReadyDescriptor processing_descriptor;
    uint32_t commit_serial;
    uint32_t force_wrong_authorizer_operation;
} FakeAdapter;

static FakeAdapter fake;

static StreamRunIdentity Identity(
    uint32_t boot_id,
    uint32_t run_id,
    uint32_t generation)
{
    StreamRunIdentity identity;
    identity.boot_id = boot_id;
    identity.run_id = run_id;
    identity.generation = generation;
    return identity;
}

static StreamOwnershipDescriptor Ownership(
    uint32_t sequence,
    R2_BufferId buffer_id,
    uint32_t completed_slot,
    uint32_t mapping_epoch)
{
    StreamOwnershipDescriptor ownership;
    ownership.sequence = sequence;
    ownership.buffer_id = buffer_id;
    ownership.completed_slot = completed_slot;
    ownership.mapping_epoch = mapping_epoch;
    return ownership;
}

static StreamRunAuthoritySnapshot Snap(void)
{
    StreamRunAuthoritySnapshot snapshot;
    CHECK(StreamRunAuthority_GetSnapshot(&snapshot) ==
        STREAM_RUN_AUTHORITY_OK);
    return snapshot;
}

static StreamRunTicket InitDefault(void)
{
    StreamRunTicket ticket;
    StreamRunIdentity identity = Identity(11U, 22U, 33U);

    (void)memset(&fake, 0, sizeof(fake));
    CHECK(StreamRunAuthority_Initialize(
        4U, &identity, &ticket) == STREAM_RUN_AUTHORITY_OK);
    return ticket;
}

static int InvokeAuthorizer(
    StreamQueueAdapterOperation operation,
    const StreamQueueAdapterReadyDescriptor *descriptor,
    const StreamQueueAdapterPermit *permit)
{
    StreamQueueAdapterOperation actual = operation;

    if (fake.force_wrong_authorizer_operation != 0U)
    {
        actual = (operation == STREAM_QUEUE_ADAPTER_OP_COMPLETE) ?
            STREAM_QUEUE_ADAPTER_OP_CANCEL :
            STREAM_QUEUE_ADAPTER_OP_COMPLETE;
    }

    return fake.authorizer(
        actual,
        descriptor->ownership.buffer_id,
        descriptor,
        permit,
        fake.authorizer_context);
}

StreamQueueAdapterStatus StreamQueueAdapter_Initialize(
    uint32_t k,
    StreamQueueAdapterAuthorizeFn authorizer,
    void *authorizer_context)
{
    (void)k;
    if ((fake.initialized != 0U) || (authorizer == NULL))
    {
        return STREAM_QUEUE_ADAPTER_INVALID_STATE;
    }
    fake.initialized = 1U;
    fake.authorizer = authorizer;
    fake.authorizer_context = authorizer_context;
    return STREAM_QUEUE_ADAPTER_OK;
}

StreamQueueAdapterStatus StreamQueueAdapter_ResetOffline(void)
{
    if (fake.faulted != 0U)
    {
        return STREAM_QUEUE_ADAPTER_FAULTED;
    }
    if ((fake.ready_valid != 0U) ||
        (fake.held_valid != 0U) ||
        (fake.processing_valid != 0U))
    {
        return STREAM_QUEUE_ADAPTER_INVALID_STATE;
    }
    (void)memset(&fake, 0, sizeof(fake));
    return STREAM_QUEUE_ADAPTER_OK;
}

StreamQueueAdapterStatus StreamQueueAdapter_BindProcessingTask(
    TaskHandle_t processing_task)
{
    if ((fake.faulted != 0U) ||
        (fake.initialized == 0U) ||
        (processing_task == NULL) ||
        (fake.processing_task != NULL))
    {
        return STREAM_QUEUE_ADAPTER_INVALID_STATE;
    }
    fake.processing_task = processing_task;
    return STREAM_QUEUE_ADAPTER_OK;
}

StreamQueueAdapterStatus StreamQueueAdapter_TakeFreeFromISR(
    R2_BufferId *out_id,
    BaseType_t *higher_priority_task_woken)
{
    if (fake.faulted != 0U)
    {
        return STREAM_QUEUE_ADAPTER_FAULTED;
    }
    if ((out_id == NULL) || (higher_priority_task_woken == NULL))
    {
        return STREAM_QUEUE_ADAPTER_INVALID_ARGUMENT;
    }
    *out_id = 2U;
    return STREAM_QUEUE_ADAPTER_OK;
}

StreamQueueAdapterStatus StreamQueueAdapter_PublishReadyFromISR(
    const StreamQueueAdapterReadyDescriptor *descriptor,
    BaseType_t *higher_priority_task_woken)
{
    if (fake.faulted != 0U)
    {
        return STREAM_QUEUE_ADAPTER_FAULTED;
    }
    if ((descriptor == NULL) || (higher_priority_task_woken == NULL) ||
        (fake.ready_valid != 0U))
    {
        return STREAM_QUEUE_ADAPTER_QUEUE_ERROR;
    }
    fake.ready_descriptor = *descriptor;
    fake.ready_valid = 1U;
    return STREAM_QUEUE_ADAPTER_OK;
}

StreamQueueAdapterStatus StreamQueueAdapter_TakeReady(
    StreamQueueAdapterReadyDescriptor *out_descriptor)
{
    if (fake.faulted != 0U)
    {
        return STREAM_QUEUE_ADAPTER_FAULTED;
    }
    if (out_descriptor == NULL)
    {
        return STREAM_QUEUE_ADAPTER_INVALID_ARGUMENT;
    }
    if (fake.ready_valid == 0U)
    {
        return STREAM_QUEUE_ADAPTER_EMPTY;
    }
    if (fake.held_valid != 0U)
    {
        return STREAM_QUEUE_ADAPTER_INVALID_STATE;
    }

    *out_descriptor = fake.ready_descriptor;
    fake.ready_valid = 0U;
    fake.held_descriptor = fake.ready_descriptor;
    fake.held_valid = 1U;
    return STREAM_QUEUE_ADAPTER_OK;
}

StreamQueueAdapterStatus StreamQueueAdapter_ClaimHeldReady(
    const StreamQueueAdapterPermit *permit)
{
    if (fake.faulted != 0U)
    {
        return STREAM_QUEUE_ADAPTER_FAULTED;
    }
    if ((permit == NULL) || (fake.held_valid == 0U))
    {
        return STREAM_QUEUE_ADAPTER_INVALID_STATE;
    }
    if (!InvokeAuthorizer(
            STREAM_QUEUE_ADAPTER_OP_READY_CLAIM,
            &fake.held_descriptor,
            permit) ||
        !InvokeAuthorizer(
            STREAM_QUEUE_ADAPTER_OP_READY_CLAIM,
            &fake.held_descriptor,
            permit))
    {
        fake.faulted = 1U;
        return STREAM_QUEUE_ADAPTER_AUTH_REJECTED;
    }

    fake.processing_descriptor = fake.held_descriptor;
    fake.processing_valid = 1U;
    fake.held_valid = 0U;
    return STREAM_QUEUE_ADAPTER_OK;
}

StreamQueueAdapterStatus StreamQueueAdapter_CancelHeldReady(
    const StreamQueueAdapterPermit *permit,
    StreamQueueAdapterReceipt *out_receipt)
{
    if (fake.faulted != 0U)
    {
        return STREAM_QUEUE_ADAPTER_FAULTED;
    }
    if ((permit == NULL) || (out_receipt == NULL) ||
        (fake.held_valid == 0U))
    {
        return STREAM_QUEUE_ADAPTER_INVALID_STATE;
    }
    if (!InvokeAuthorizer(
            STREAM_QUEUE_ADAPTER_OP_CANCEL,
            &fake.held_descriptor,
            permit) ||
        !InvokeAuthorizer(
            STREAM_QUEUE_ADAPTER_OP_CANCEL,
            &fake.held_descriptor,
            permit))
    {
        fake.faulted = 1U;
        return STREAM_QUEUE_ADAPTER_AUTH_REJECTED;
    }

    ++fake.commit_serial;
    out_receipt->commit_serial = fake.commit_serial;
    out_receipt->operation = STREAM_QUEUE_ADAPTER_OP_CANCEL;
    out_receipt->buffer_id = fake.held_descriptor.ownership.buffer_id;
    fake.held_valid = 0U;
    return STREAM_QUEUE_ADAPTER_OK;
}

StreamQueueAdapterStatus StreamQueueAdapter_CompleteAndReleaseBlock(
    const StreamQueueAdapterPermit *permit,
    StreamQueueAdapterReceipt *out_receipt)
{
    if (fake.faulted != 0U)
    {
        return STREAM_QUEUE_ADAPTER_FAULTED;
    }
    if ((permit == NULL) || (out_receipt == NULL) ||
        (fake.processing_valid == 0U))
    {
        return STREAM_QUEUE_ADAPTER_INVALID_STATE;
    }
    if (!InvokeAuthorizer(
            STREAM_QUEUE_ADAPTER_OP_COMPLETE,
            &fake.processing_descriptor,
            permit) ||
        !InvokeAuthorizer(
            STREAM_QUEUE_ADAPTER_OP_COMPLETE,
            &fake.processing_descriptor,
            permit))
    {
        fake.faulted = 1U;
        return STREAM_QUEUE_ADAPTER_AUTH_REJECTED;
    }

    ++fake.commit_serial;
    out_receipt->commit_serial = fake.commit_serial;
    out_receipt->operation = STREAM_QUEUE_ADAPTER_OP_COMPLETE;
    out_receipt->buffer_id =
        fake.processing_descriptor.ownership.buffer_id;
    fake.processing_valid = 0U;
    return STREAM_QUEUE_ADAPTER_OK;
}

static void Publish(
    const StreamRunTicket *ticket,
    StreamOwnershipDescriptor ownership)
{
    BaseType_t hpw = pdFALSE;
    CHECK(StreamRunAuthority_PublishReadyFromISR(
        ticket, &ownership, &hpw) == STREAM_RUN_AUTHORITY_OK);
}

static StreamOwnershipDescriptor Take(const StreamRunTicket *ticket)
{
    StreamOwnershipDescriptor ownership;
    CHECK(StreamRunAuthority_TakeReady(
        ticket, &ownership) == STREAM_RUN_AUTHORITY_OK);
    return ownership;
}

static void CaseInitialize(void)
{
    StreamRunTicket ticket = InitDefault();
    StreamRunAuthoritySnapshot s = Snap();

    CHECK(fake.authorizer != NULL);
    CHECK(fake.authorizer_context != NULL);
    CHECK(s.initialized == 1U);
    CHECK(s.faulted == 0U);
    CHECK(s.k == 4U);
    CHECK(s.identity.boot_id == ticket.identity.boot_id);
    CHECK(s.identity.run_id == ticket.identity.run_id);
    CHECK(s.identity.generation == ticket.identity.generation);
    CHECK(s.next_lease_id == 1U);
    CHECK(s.live_lease_count == 0U);
}

static void CasePublishProvenance(void)
{
    StreamRunTicket ticket = InitDefault();
    StreamOwnershipDescriptor ownership = Ownership(1U, 0U, 0U, 2U);
    StreamRunAuthoritySnapshot s;

    Publish(&ticket, ownership);

    CHECK(fake.ready_valid == 1U);
    CHECK(fake.ready_descriptor.ownership.sequence == 1U);
    CHECK(fake.ready_descriptor.ownership.buffer_id == 0U);
    CHECK(fake.ready_descriptor.provenance.words[0] == 11U);
    CHECK(fake.ready_descriptor.provenance.words[1] == 22U);
    CHECK(fake.ready_descriptor.provenance.words[2] == 33U);
    CHECK(fake.ready_descriptor.provenance.words[3] == 1U);

    s = Snap();
    CHECK(s.next_lease_id == 2U);
    CHECK(s.live_lease_count == 1U);
    CHECK(s.publish_count == 1U);
}

static void CaseLeaseMonotonic(void)
{
    StreamRunTicket ticket = InitDefault();
    StreamOwnershipDescriptor a = Ownership(1U, 0U, 0U, 2U);
    StreamOwnershipDescriptor b = Ownership(2U, 1U, 1U, 3U);
    StreamOwnershipDescriptor out;
    StreamQueueAdapterReceipt receipt;

    Publish(&ticket, a);
    out = Take(&ticket);
    CHECK(out.buffer_id == 0U);
    CHECK(StreamRunAuthority_CancelHeldReady(
        &ticket, &receipt) == STREAM_RUN_AUTHORITY_OK);

    Publish(&ticket, b);
    CHECK(fake.ready_descriptor.provenance.words[3] == 2U);
    CHECK(Snap().next_lease_id == 3U);
}

static void CaseRoundTripComplete(void)
{
    StreamRunTicket ticket = InitDefault();
    StreamOwnershipDescriptor ownership = Ownership(1U, 0U, 0U, 2U);
    StreamOwnershipDescriptor out;
    StreamQueueAdapterReceipt receipt;
    StreamRunAuthoritySnapshot s;

    Publish(&ticket, ownership);
    out = Take(&ticket);
    CHECK(out.sequence == ownership.sequence);
    CHECK(out.buffer_id == ownership.buffer_id);

    CHECK(StreamRunAuthority_ClaimHeldReady(&ticket) ==
        STREAM_RUN_AUTHORITY_OK);
    s = Snap();
    CHECK(s.held_valid == 0U);
    CHECK(s.processing_valid == 1U);
    CHECK(s.processing_buffer == 0U);
    CHECK(s.processing_lease_id == 1U);

    CHECK(StreamRunAuthority_CompleteAndReleaseBlock(
        &ticket, &receipt) == STREAM_RUN_AUTHORITY_OK);
    CHECK(receipt.operation == STREAM_QUEUE_ADAPTER_OP_COMPLETE);
    CHECK(receipt.buffer_id == 0U);

    s = Snap();
    CHECK(s.processing_valid == 0U);
    CHECK(s.live_lease_count == 0U);
    CHECK(s.publish_count == 1U);
    CHECK(s.take_count == 1U);
    CHECK(s.claim_count == 1U);
    CHECK(s.complete_count == 1U);
}

static void CaseCancel(void)
{
    StreamRunTicket ticket = InitDefault();
    StreamOwnershipDescriptor ownership = Ownership(1U, 0U, 0U, 2U);
    StreamQueueAdapterReceipt receipt;
    StreamRunAuthoritySnapshot s;

    Publish(&ticket, ownership);
    (void)Take(&ticket);
    CHECK(StreamRunAuthority_CancelHeldReady(
        &ticket, &receipt) == STREAM_RUN_AUTHORITY_OK);
    CHECK(receipt.operation == STREAM_QUEUE_ADAPTER_OP_CANCEL);
    CHECK(receipt.buffer_id == 0U);

    s = Snap();
    CHECK(s.held_valid == 0U);
    CHECK(s.processing_valid == 0U);
    CHECK(s.live_lease_count == 0U);
    CHECK(s.cancel_count == 1U);
}

static void CaseStaleTicketFaults(void)
{
    StreamRunTicket ticket = InitDefault();
    StreamRunTicket stale = ticket;
    StreamRunAuthoritySnapshot s;
    R2_BufferId id = R2_BUFFER_POOL_INVALID_ID;
    BaseType_t hpw = pdFALSE;

    stale.identity.generation += 1U;
    CHECK(StreamRunAuthority_TakeFreeFromISR(
        &stale, &id, &hpw) == STREAM_RUN_AUTHORITY_STALE_RUN);

    s = Snap();
    CHECK(s.faulted == 1U);
    CHECK(s.stale_run_count == 1U);
    CHECK(s.failure_count == 1U);
    CHECK(StreamRunAuthority_TakeFreeFromISR(
        &ticket, &id, &hpw) == STREAM_RUN_AUTHORITY_FAULTED);
}

static void CaseStaleReadyProvenanceFaults(void)
{
    StreamRunTicket ticket = InitDefault();
    StreamOwnershipDescriptor ownership = Ownership(1U, 0U, 0U, 2U);
    StreamOwnershipDescriptor out;
    StreamRunAuthoritySnapshot s;

    Publish(&ticket, ownership);
    fake.ready_descriptor.provenance.words[2] += 1U;

    CHECK(StreamRunAuthority_TakeReady(
        &ticket, &out) == STREAM_RUN_AUTHORITY_PROVENANCE_ERROR);
    s = Snap();
    CHECK(s.faulted == 1U);
    CHECK(s.provenance_failure_count == 1U);
}

static void CaseUnmintedReadyFaults(void)
{
    StreamRunTicket ticket = InitDefault();
    StreamOwnershipDescriptor out;
    StreamRunAuthoritySnapshot s;

    fake.ready_valid = 1U;
    fake.ready_descriptor.ownership = Ownership(9U, 0U, 0U, 7U);
    fake.ready_descriptor.provenance.words[0] = 11U;
    fake.ready_descriptor.provenance.words[1] = 22U;
    fake.ready_descriptor.provenance.words[2] = 33U;
    fake.ready_descriptor.provenance.words[3] = 55U;

    CHECK(StreamRunAuthority_TakeReady(
        &ticket, &out) == STREAM_RUN_AUTHORITY_PROVENANCE_ERROR);
    s = Snap();
    CHECK(s.faulted == 1U);
    CHECK(s.live_lease_count == 0U);
}

static void CaseWrongOperationRejected(void)
{
    StreamRunTicket ticket = InitDefault();
    StreamOwnershipDescriptor ownership = Ownership(1U, 0U, 0U, 2U);
    StreamRunAuthoritySnapshot s;

    Publish(&ticket, ownership);
    (void)Take(&ticket);
    fake.force_wrong_authorizer_operation = 1U;

    CHECK(StreamRunAuthority_ClaimHeldReady(&ticket) ==
        STREAM_RUN_AUTHORITY_ADAPTER_ERROR);
    s = Snap();
    CHECK(s.faulted == 1U);
    CHECK(s.adapter_failure_count == 1U);
}

static void CaseDirectAdapterClaimRejected(void)
{
    StreamRunTicket ticket = InitDefault();
    StreamOwnershipDescriptor ownership = Ownership(1U, 0U, 0U, 2U);
    StreamQueueAdapterPermit forged;
    StreamRunAuthoritySnapshot s;

    Publish(&ticket, ownership);
    (void)Take(&ticket);

    forged.words[0] = fake.held_descriptor.provenance.words[0];
    forged.words[1] = fake.held_descriptor.provenance.words[1];
    forged.words[2] = fake.held_descriptor.provenance.words[2];
    forged.words[3] = fake.held_descriptor.provenance.words[3];

    CHECK(StreamQueueAdapter_ClaimHeldReady(&forged) ==
        STREAM_QUEUE_ADAPTER_AUTH_REJECTED);

    CHECK(StreamRunAuthority_ClaimHeldReady(&ticket) ==
        STREAM_RUN_AUTHORITY_ADAPTER_ERROR);
    s = Snap();
    CHECK(s.faulted == 1U);
}

static void CaseResetAfterComplete(void)
{
    StreamRunTicket ticket = InitDefault();
    StreamOwnershipDescriptor ownership = Ownership(1U, 0U, 0U, 2U);
    StreamQueueAdapterReceipt receipt;
    StreamRunAuthoritySnapshot s;

    Publish(&ticket, ownership);
    (void)Take(&ticket);
    CHECK(StreamRunAuthority_ClaimHeldReady(&ticket) ==
        STREAM_RUN_AUTHORITY_OK);
    CHECK(StreamRunAuthority_CompleteAndReleaseBlock(
        &ticket, &receipt) == STREAM_RUN_AUTHORITY_OK);

    CHECK(StreamRunAuthority_ResetOffline(&ticket) ==
        STREAM_RUN_AUTHORITY_OK);
    s = Snap();
    CHECK(s.initialized == 0U);
    CHECK(s.faulted == 0U);
    CHECK(s.live_lease_count == 0U);
}

static void CaseResetWithLiveLeaseFaults(void)
{
    StreamRunTicket ticket = InitDefault();
    StreamOwnershipDescriptor ownership = Ownership(1U, 0U, 0U, 2U);
    StreamRunAuthoritySnapshot s;

    Publish(&ticket, ownership);
    CHECK(StreamRunAuthority_ResetOffline(&ticket) ==
        STREAM_RUN_AUTHORITY_INVALID_STATE);
    s = Snap();
    CHECK(s.faulted == 1U);
    CHECK(s.live_lease_count == 1U);
}

typedef struct
{
    const char *name;
    void (*run)(void);
} TestCase;

static const TestCase cases[] =
{
    {"initialize", CaseInitialize},
    {"publish_provenance", CasePublishProvenance},
    {"lease_monotonic", CaseLeaseMonotonic},
    {"roundtrip_complete", CaseRoundTripComplete},
    {"cancel", CaseCancel},
    {"stale_ticket_faults", CaseStaleTicketFaults},
    {"stale_ready_provenance_faults", CaseStaleReadyProvenanceFaults},
    {"unminted_ready_faults", CaseUnmintedReadyFaults},
    {"wrong_operation_rejected", CaseWrongOperationRejected},
    {"direct_adapter_claim_rejected", CaseDirectAdapterClaimRejected},
    {"reset_after_complete", CaseResetAfterComplete},
    {"reset_with_live_lease_faults", CaseResetWithLiveLeaseFaults}
};

int main(int argc, char **argv)
{
    size_t i;

    if (argc != 2)
    {
        (void)fprintf(stderr, "Usage: %s <case>\n", argv[0]);
        return EXIT_FAILURE;
    }

    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        if (strcmp(argv[1], cases[i].name) == 0)
        {
            cases[i].run();
            (void)puts("F0-3 StreamRunAuthority native test: PASS");
            return EXIT_SUCCESS;
        }
    }

    (void)fprintf(stderr, "Unknown case: %s\n", argv[1]);
    return EXIT_FAILURE;
}
