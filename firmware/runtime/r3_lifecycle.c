#include "r3_lifecycle.h"

#include <stddef.h>
#include <string.h>

typedef struct
{
    R3LifecycleHooks hooks;
    R3LifecycleSnapshot snapshot;
    StreamRunTicket stream_ticket;
    uint32_t initialized;
} R3LifecycleStorage;

static R3LifecycleStorage lifecycle;

static void CloseGates(void)
{
    lifecycle.snapshot.acquisition_publish_allowed = 0U;
    lifecycle.snapshot.processing_claim_allowed = 0U;
    lifecycle.snapshot.interference_release_allowed = 0U;
}

static int TicketMatches(const R3LifecycleStartTicket *ticket)
{
    return (ticket != NULL) && (lifecycle.snapshot.start_ticket_valid != 0U) &&
        (ticket->start_ticket == lifecycle.snapshot.start_ticket) &&
        (ticket->stream_ticket.identity.boot_id == lifecycle.stream_ticket.identity.boot_id) &&
        (ticket->stream_ticket.identity.run_id == lifecycle.stream_ticket.identity.run_id) &&
        (ticket->stream_ticket.identity.generation == lifecycle.stream_ticket.identity.generation);
}

static R3LifecycleStatus RollbackToIdle(void)
{
    R3LifecycleStatus status;
    CloseGates();
    lifecycle.snapshot.start_ticket_valid = 0U;
    ++lifecycle.snapshot.rollback_count;
    status = lifecycle.hooks.rollback(lifecycle.hooks.context, &lifecycle.stream_ticket);
    if (status != R3_LIFECYCLE_OK)
    {
        lifecycle.snapshot.state = R3_LIFECYCLE_RESET_REQUIRED;
        ++lifecycle.snapshot.failure_count;
    return R3_LIFECYCLE_ROLLBACK_FAILED;
    }
    lifecycle.snapshot.state = R3_LIFECYCLE_IDLE;
    return R3_LIFECYCLE_OK;
}

void R3Lifecycle_Init(uint32_t boot_id, const R3LifecycleHooks *hooks)
{
    (void)memset(&lifecycle, 0, sizeof(lifecycle));
    if ((hooks == NULL) || (hooks->validate == NULL) || (hooks->prepare == NULL) ||
        (hooks->arm == NULL) || (hooks->commit_timer == NULL) || (hooks->rollback == NULL))
    {
        return;
    }
    lifecycle.hooks = *hooks;
    lifecycle.snapshot.boot_id = boot_id;
    lifecycle.snapshot.state = R3_LIFECYCLE_IDLE;
    lifecycle.initialized = 1U;
}

R3LifecycleStatus R3Lifecycle_PrepareStart(const R3LifecycleStartRequest *request,
                                            R3LifecycleStartTicket *out_ticket)
{
    R3LifecycleStatus status;
    if ((lifecycle.initialized == 0U) || (request == NULL) || (out_ticket == NULL))
    {
        return R3_LIFECYCLE_INVALID_ARGUMENT;
    }
    if ((lifecycle.snapshot.state != R3_LIFECYCLE_IDLE) ||
        (request->boot_id != lifecycle.snapshot.boot_id) || (request->request_id == 0U) ||
        (request->configuration_id == 0U))
    {
        return R3_LIFECYCLE_INVALID_STATE;
    }
    status = lifecycle.hooks.validate(lifecycle.hooks.context, request);
    if (status != R3_LIFECYCLE_OK) return R3_LIFECYCLE_INVALID_CONFIG;
    lifecycle.snapshot.state = R3_LIFECYCLE_PREPARING;
    CloseGates();
    lifecycle.snapshot.run_id = request->request_id;
    lifecycle.snapshot.generation = request->generation;
    lifecycle.snapshot.configuration_id = request->configuration_id;
    lifecycle.stream_ticket.identity.boot_id = request->boot_id;
    lifecycle.stream_ticket.identity.run_id = request->request_id;
    lifecycle.stream_ticket.identity.generation = request->generation;
    ++lifecycle.snapshot.start_ticket;
    if (lifecycle.snapshot.start_ticket == 0U) ++lifecycle.snapshot.start_ticket;
    lifecycle.snapshot.start_ticket_valid = 1U;
    status = lifecycle.hooks.prepare(lifecycle.hooks.context, request, &lifecycle.stream_ticket);
    if (status != R3_LIFECYCLE_OK)
    {
        (void)RollbackToIdle();
        return R3_LIFECYCLE_PREPARE_FAILED;
    }
    status = lifecycle.hooks.arm(lifecycle.hooks.context, request, &lifecycle.stream_ticket);
    if (status != R3_LIFECYCLE_OK)
    {
        (void)RollbackToIdle();
        return R3_LIFECYCLE_ARM_FAILED;
    }
    ++lifecycle.snapshot.prepare_count;
    out_ticket->stream_ticket = lifecycle.stream_ticket;
    out_ticket->start_ticket = lifecycle.snapshot.start_ticket;
    return R3_LIFECYCLE_OK;
}

R3LifecycleStatus R3Lifecycle_RequestStopBeforeCommit(const R3LifecycleStartTicket *ticket)
{
    if ((lifecycle.initialized == 0U) || !TicketMatches(ticket)) return R3_LIFECYCLE_STALE_TICKET;
    if (lifecycle.snapshot.state != R3_LIFECYCLE_PREPARING) return R3_LIFECYCLE_INVALID_STATE;
    return RollbackToIdle() == R3_LIFECYCLE_OK ? R3_LIFECYCLE_STOPPED : R3_LIFECYCLE_STATUS_RESET_REQUIRED;
}

R3LifecycleStatus R3Lifecycle_CommitStart(const R3LifecycleStartTicket *ticket)
{
    R3LifecycleStatus status;
    if ((lifecycle.initialized == 0U) || !TicketMatches(ticket)) return R3_LIFECYCLE_STALE_TICKET;
    if (lifecycle.snapshot.state != R3_LIFECYCLE_PREPARING) return R3_LIFECYCLE_INVALID_STATE;
    /* Context and all gates are visible before the timer can generate ADC triggers. */
    lifecycle.snapshot.acquisition_publish_allowed = 1U;
    lifecycle.snapshot.processing_claim_allowed = 1U;
    lifecycle.snapshot.interference_release_allowed = 1U;
    status = lifecycle.hooks.commit_timer(lifecycle.hooks.context, &lifecycle.stream_ticket);
    if (status != R3_LIFECYCLE_OK)
    {
        (void)RollbackToIdle();
        return R3_LIFECYCLE_COMMIT_FAILED;
    }
    lifecycle.snapshot.state = R3_LIFECYCLE_RUNNING;
    lifecycle.snapshot.start_ticket_valid = 0U;
    ++lifecycle.snapshot.commit_count;
    return R3_LIFECYCLE_OK;
}

R3LifecycleStatus R3Lifecycle_GetSnapshot(R3LifecycleSnapshot *out)
{
    if ((lifecycle.initialized == 0U) || (out == NULL)) return R3_LIFECYCLE_INVALID_ARGUMENT;
    *out = lifecycle.snapshot;
    return R3_LIFECYCLE_OK;
}
