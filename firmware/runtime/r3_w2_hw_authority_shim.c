#include "r3_w2_hw_authority_shim.h"

#include <stddef.h>
#include <string.h>

#include "r3_w2_hw_harness.h"

typedef struct
{
    uint32_t initialized;
    StreamRunTicket ticket;
    uint32_t ready_valid;
    uint32_t held_valid;
    uint32_t processing_valid;
    StreamOwnershipDescriptor ready;
    StreamOwnershipDescriptor held;
    StreamOwnershipDescriptor processing;
    uint32_t publish_count;
    uint32_t take_count;
    uint32_t claim_count;
    uint32_t cancel_count;
    uint32_t complete_count;
    uint32_t commit_serial;
} R3W2HwAuthorityShimStorage;

static R3W2HwAuthorityShimStorage storage;

/*
 * The frozen W2-HW profile deliberately does not link the R1 acquisition
 * translation unit.  Cube's common DMA2 ISR still has this call site,
 * though.  A DMA2 IRQ is outside this synthetic TIM6-driven harness, so
 * make it a visible terminal failure rather than silently importing R1 or
 * accepting the unexpected interrupt.
 */
void R1_Acquisition_DmaIrqEnter(uint32_t dma2_lisr)
{
    (void)dma2_lisr;
    g_r3_w2_hw_result.invariant_bits |= R3_W2_HW_INV_ISR_PATH;
    if (g_r3_w2_hw_result.first_fault == (uint32_t)R3_W2_HW_FAULT_NONE)
    {
        g_r3_w2_hw_result.first_fault = (uint32_t)R3_W2_HW_FAULT_ISR_NOTIFY;
    }
}

static int TicketMatches(const StreamRunTicket *ticket)
{
    return (ticket != NULL) &&
        (storage.initialized != 0U) &&
        (ticket->identity.boot_id == storage.ticket.identity.boot_id) &&
        (ticket->identity.run_id == storage.ticket.identity.run_id) &&
        (ticket->identity.generation == storage.ticket.identity.generation);
}

static void FillReceipt(
    StreamQueueAdapterReceipt *receipt,
    StreamQueueAdapterOperation operation,
    R2_BufferId buffer_id)
{
    ++storage.commit_serial;
    receipt->commit_serial = storage.commit_serial;
    receipt->operation = operation;
    receipt->buffer_id = buffer_id;
}

StreamRunAuthorityStatus R3_W2_HW_AuthorityShim_Reset(
    const StreamRunTicket *ticket)
{
    if (ticket == NULL)
    {
        return STREAM_RUN_AUTHORITY_INVALID_ARGUMENT;
    }

    (void)memset(&storage, 0, sizeof(storage));
    storage.initialized = 1U;
    storage.ticket = *ticket;
    return STREAM_RUN_AUTHORITY_OK;
}

StreamRunAuthorityStatus R3_W2_HW_AuthorityShim_PublishReady(
    const StreamRunTicket *ticket,
    const StreamOwnershipDescriptor *ownership)
{
    if ((ticket == NULL) || (ownership == NULL))
    {
        return STREAM_RUN_AUTHORITY_INVALID_ARGUMENT;
    }
    if (!TicketMatches(ticket))
    {
        return STREAM_RUN_AUTHORITY_STALE_RUN;
    }
    if ((storage.ready_valid != 0U) ||
        (storage.held_valid != 0U) ||
        (storage.processing_valid != 0U))
    {
        return STREAM_RUN_AUTHORITY_INVALID_STATE;
    }

    storage.ready = *ownership;
    storage.ready_valid = 1U;
    ++storage.publish_count;
    return STREAM_RUN_AUTHORITY_OK;
}

StreamRunAuthorityStatus R3_W2_HW_AuthorityShim_GetHarnessSnapshot(
    R3W2HwAuthorityShimSnapshot *out)
{
    if (out == NULL)
    {
        return STREAM_RUN_AUTHORITY_INVALID_ARGUMENT;
    }

    (void)memset(out, 0, sizeof(*out));
    out->initialized = storage.initialized;
    out->ready_valid = storage.ready_valid;
    out->held_valid = storage.held_valid;
    out->processing_valid = storage.processing_valid;
    out->publish_count = storage.publish_count;
    out->take_count = storage.take_count;
    out->claim_count = storage.claim_count;
    out->cancel_count = storage.cancel_count;
    out->complete_count = storage.complete_count;
    out->commit_serial = storage.commit_serial;
    out->ready = storage.ready;
    out->held = storage.held;
    out->processing = storage.processing;
    return STREAM_RUN_AUTHORITY_OK;
}

StreamRunAuthorityStatus StreamRunAuthority_TakeReady(
    const StreamRunTicket *ticket,
    StreamOwnershipDescriptor *out_ownership)
{
    if ((ticket == NULL) || (out_ownership == NULL))
    {
        return STREAM_RUN_AUTHORITY_INVALID_ARGUMENT;
    }
    if (!TicketMatches(ticket))
    {
        return STREAM_RUN_AUTHORITY_STALE_RUN;
    }
    if ((storage.held_valid != 0U) || (storage.processing_valid != 0U))
    {
        return STREAM_RUN_AUTHORITY_INVALID_STATE;
    }
    if (storage.ready_valid == 0U)
    {
        return STREAM_RUN_AUTHORITY_EMPTY;
    }

    storage.held = storage.ready;
    storage.held_valid = 1U;
    storage.ready_valid = 0U;
    (void)memset(&storage.ready, 0, sizeof(storage.ready));
    *out_ownership = storage.held;
    ++storage.take_count;
    return STREAM_RUN_AUTHORITY_OK;
}

StreamRunAuthorityStatus StreamRunAuthority_ClaimHeldReady(
    const StreamRunTicket *ticket)
{
    if (ticket == NULL)
    {
        return STREAM_RUN_AUTHORITY_INVALID_ARGUMENT;
    }
    if (!TicketMatches(ticket))
    {
        return STREAM_RUN_AUTHORITY_STALE_RUN;
    }
    if ((storage.held_valid == 0U) || (storage.processing_valid != 0U))
    {
        return STREAM_RUN_AUTHORITY_INVALID_STATE;
    }

    storage.processing = storage.held;
    storage.processing_valid = 1U;
    storage.held_valid = 0U;
    (void)memset(&storage.held, 0, sizeof(storage.held));
    ++storage.claim_count;
    return STREAM_RUN_AUTHORITY_OK;
}

StreamRunAuthorityStatus StreamRunAuthority_CancelHeldReady(
    const StreamRunTicket *ticket,
    StreamQueueAdapterReceipt *out_receipt)
{
    R2_BufferId id;

    if ((ticket == NULL) || (out_receipt == NULL))
    {
        return STREAM_RUN_AUTHORITY_INVALID_ARGUMENT;
    }
    if (!TicketMatches(ticket))
    {
        return STREAM_RUN_AUTHORITY_STALE_RUN;
    }
    if (storage.held_valid == 0U)
    {
        return STREAM_RUN_AUTHORITY_INVALID_STATE;
    }

    id = storage.held.buffer_id;
    storage.held_valid = 0U;
    (void)memset(&storage.held, 0, sizeof(storage.held));
    ++storage.cancel_count;
    FillReceipt(out_receipt, STREAM_QUEUE_ADAPTER_OP_CANCEL, id);
    return STREAM_RUN_AUTHORITY_OK;
}

StreamRunAuthorityStatus StreamRunAuthority_CompleteAndReleaseBlock(
    const StreamRunTicket *ticket,
    StreamQueueAdapterReceipt *out_receipt)
{
    R2_BufferId id;

    if ((ticket == NULL) || (out_receipt == NULL))
    {
        return STREAM_RUN_AUTHORITY_INVALID_ARGUMENT;
    }
    if (!TicketMatches(ticket))
    {
        return STREAM_RUN_AUTHORITY_STALE_RUN;
    }
    if (storage.processing_valid == 0U)
    {
        return STREAM_RUN_AUTHORITY_INVALID_STATE;
    }

    id = storage.processing.buffer_id;
    storage.processing_valid = 0U;
    (void)memset(&storage.processing, 0, sizeof(storage.processing));
    ++storage.complete_count;
    FillReceipt(out_receipt, STREAM_QUEUE_ADAPTER_OP_COMPLETE, id);
    return STREAM_RUN_AUTHORITY_OK;
}

StreamRunAuthorityStatus StreamRunAuthority_GetSnapshot(
    StreamRunAuthoritySnapshot *out)
{
    if (out == NULL)
    {
        return STREAM_RUN_AUTHORITY_INVALID_ARGUMENT;
    }
    if (storage.initialized == 0U)
    {
        return STREAM_RUN_AUTHORITY_INVALID_STATE;
    }

    (void)memset(out, 0, sizeof(*out));
    out->initialized = 1U;
    out->k = 1U;
    out->identity = storage.ticket.identity;
    out->next_lease_id = 1U;
    out->live_lease_count =
        storage.ready_valid + storage.held_valid + storage.processing_valid;
    out->held_valid = storage.held_valid;
    out->held_buffer = storage.held_valid != 0U ?
        storage.held.buffer_id : R2_BUFFER_POOL_INVALID_ID;
    out->held_lease_id = storage.held_valid != 0U ? 1U : 0U;
    out->processing_valid = storage.processing_valid;
    out->processing_buffer = storage.processing_valid != 0U ?
        storage.processing.buffer_id : R2_BUFFER_POOL_INVALID_ID;
    out->processing_lease_id = storage.processing_valid != 0U ? 1U : 0U;
    out->publish_count = storage.publish_count;
    out->take_count = storage.take_count;
    out->claim_count = storage.claim_count;
    out->cancel_count = storage.cancel_count;
    out->complete_count = storage.complete_count;
    return STREAM_RUN_AUTHORITY_OK;
}
