#include "r3_command_ledger.h"

#include <string.h>

static int BindingMatches(const R3CommandLedgerBinding *binding,
    const R3LifecycleStartRequest *request)
{
    return binding->boot_id == request->boot_id &&
        binding->request_id == request->request_id &&
        binding->generation == request->generation &&
        binding->configuration_id == request->configuration_id;
}

static int TicketMatches(const R3LifecycleStartTicket *a,
    const R3LifecycleStartTicket *b)
{
    return a->start_ticket == b->start_ticket &&
        a->stream_ticket.identity.boot_id == b->stream_ticket.identity.boot_id &&
        a->stream_ticket.identity.run_id == b->stream_ticket.identity.run_id &&
        a->stream_ticket.identity.generation == b->stream_ticket.identity.generation;
}

static int StopMatches(const R3CommandLedger *ledger,
    const R3LifecycleStopRequest *request)
{
    const StreamRunIdentity *identity = &request->stream_ticket.identity;
    return identity->boot_id == ledger->snapshot.binding.boot_id &&
        identity->run_id == ledger->snapshot.binding.request_id &&
        identity->generation == ledger->snapshot.binding.generation;
}

void R3CommandLedger_Initialize(R3CommandLedger *ledger, uint32_t boot_id)
{
    if (ledger == NULL) return;
    (void)memset(ledger, 0, sizeof(*ledger));
    ledger->boot_id = boot_id;
}

R3CommandLedgerStatus R3CommandLedger_BeginStart(
    R3CommandLedger *ledger, const R3LifecycleStartRequest *request,
    R3LifecycleStartTicket *out_replay_ticket)
{
    if ((ledger == NULL) || (request == NULL) || (out_replay_ticket == NULL) ||
        (request->request_id == 0U) || (request->configuration_id == 0U))
        return R3_COMMAND_LEDGER_INVALID_ARGUMENT;
    if (request->boot_id != ledger->boot_id) return R3_COMMAND_LEDGER_STALE_BOOT;
    if (ledger->snapshot.phase == R3_COMMAND_LEDGER_EMPTY)
    {
        (void)memset(&ledger->snapshot, 0, sizeof(ledger->snapshot));
        ledger->snapshot.phase = R3_COMMAND_LEDGER_PREPARING;
        ledger->snapshot.binding.boot_id = request->boot_id;
        ledger->snapshot.binding.request_id = request->request_id;
        ledger->snapshot.binding.generation = request->generation;
        ledger->snapshot.binding.configuration_id = request->configuration_id;
        return R3_COMMAND_LEDGER_OK_NEW;
    }
    if (ledger->snapshot.phase == R3_COMMAND_LEDGER_STOPPED)
    {
        if (ledger->snapshot.binding.request_id == request->request_id)
        {
            if (!BindingMatches(&ledger->snapshot.binding, request))
                return R3_COMMAND_LEDGER_REQUEST_CONFLICT;
            *out_replay_ticket = ledger->snapshot.start_ticket;
            return R3_COMMAND_LEDGER_OK_REPLAY;
        }
        (void)memset(&ledger->snapshot, 0, sizeof(ledger->snapshot));
        ledger->snapshot.phase = R3_COMMAND_LEDGER_PREPARING;
        ledger->snapshot.binding.boot_id = request->boot_id;
        ledger->snapshot.binding.request_id = request->request_id;
        ledger->snapshot.binding.generation = request->generation;
        ledger->snapshot.binding.configuration_id = request->configuration_id;
        return R3_COMMAND_LEDGER_OK_NEW;
    }
    if (ledger->snapshot.binding.request_id != request->request_id)
        return R3_COMMAND_LEDGER_INVALID_STATE;
    if (!BindingMatches(&ledger->snapshot.binding, request))
        return R3_COMMAND_LEDGER_REQUEST_CONFLICT;
    if (ledger->snapshot.start_ticket_valid == 0U)
        return R3_COMMAND_LEDGER_INVALID_STATE;
    *out_replay_ticket = ledger->snapshot.start_ticket;
    return R3_COMMAND_LEDGER_OK_REPLAY;
}

R3CommandLedgerStatus R3CommandLedger_RecordPrepared(
    R3CommandLedger *ledger, const R3LifecycleStartTicket *ticket)
{
    if ((ledger == NULL) || (ticket == NULL) ||
        (ledger->snapshot.phase != R3_COMMAND_LEDGER_PREPARING))
        return R3_COMMAND_LEDGER_INVALID_STATE;
    if (ticket->stream_ticket.identity.boot_id != ledger->snapshot.binding.boot_id ||
        ticket->stream_ticket.identity.run_id != ledger->snapshot.binding.request_id ||
        ticket->stream_ticket.identity.generation != ledger->snapshot.binding.generation)
        return R3_COMMAND_LEDGER_STALE_TICKET;
    ledger->snapshot.start_ticket = *ticket;
    ledger->snapshot.start_ticket_valid = 1U;
    return R3_COMMAND_LEDGER_OK_NEW;
}

R3CommandLedgerStatus R3CommandLedger_RecordRunning(
    R3CommandLedger *ledger, const R3LifecycleStartTicket *ticket)
{
    if ((ledger == NULL) || (ticket == NULL) ||
        (ledger->snapshot.phase != R3_COMMAND_LEDGER_PREPARING) ||
        (ledger->snapshot.start_ticket_valid == 0U))
        return R3_COMMAND_LEDGER_INVALID_STATE;
    if (!TicketMatches(&ledger->snapshot.start_ticket, ticket))
        return R3_COMMAND_LEDGER_STALE_TICKET;
    ledger->snapshot.phase = R3_COMMAND_LEDGER_RUNNING;
    return R3_COMMAND_LEDGER_OK_NEW;
}

R3CommandLedgerStatus R3CommandLedger_AbortStart(
    R3CommandLedger *ledger, const R3LifecycleStartRequest *request)
{
    if ((ledger == NULL) || (request == NULL) ||
        (ledger->snapshot.phase != R3_COMMAND_LEDGER_PREPARING) ||
        !BindingMatches(&ledger->snapshot.binding, request))
        return R3_COMMAND_LEDGER_INVALID_STATE;
    (void)memset(&ledger->snapshot, 0, sizeof(ledger->snapshot));
    return R3_COMMAND_LEDGER_OK_NEW;
}

R3CommandLedgerStatus R3CommandLedger_BeginStop(
    R3CommandLedger *ledger, const R3LifecycleStopRequest *request)
{
    if ((ledger == NULL) || (request == NULL) || (request->stop_id == 0U))
        return R3_COMMAND_LEDGER_INVALID_ARGUMENT;
    if (!StopMatches(ledger, request)) return R3_COMMAND_LEDGER_STALE_TICKET;
    if (ledger->snapshot.phase == R3_COMMAND_LEDGER_STOPPED)
        return (ledger->snapshot.stop_result_valid != 0U &&
                ledger->snapshot.stop_id == request->stop_id) ?
            R3_COMMAND_LEDGER_OK_REPLAY : R3_COMMAND_LEDGER_STOP_CONFLICT;
    if (ledger->snapshot.phase != R3_COMMAND_LEDGER_RUNNING)
        return R3_COMMAND_LEDGER_INVALID_STATE;
    return R3_COMMAND_LEDGER_OK_NEW;
}

R3CommandLedgerStatus R3CommandLedger_RecordStopped(
    R3CommandLedger *ledger, const R3LifecycleStopRequest *request)
{
    R3CommandLedgerStatus status = R3CommandLedger_BeginStop(ledger, request);
    if (status != R3_COMMAND_LEDGER_OK_NEW) return status;
    ledger->snapshot.phase = R3_COMMAND_LEDGER_STOPPED;
    ledger->snapshot.stop_id = request->stop_id;
    ledger->snapshot.stop_result_valid = 1U;
    return R3_COMMAND_LEDGER_OK_NEW;
}

R3CommandLedgerStatus R3CommandLedger_GetSnapshot(
    const R3CommandLedger *ledger, R3CommandLedgerSnapshot *out)
{
    if ((ledger == NULL) || (out == NULL) || (ledger->boot_id == 0U))
        return R3_COMMAND_LEDGER_INVALID_ARGUMENT;
    *out = ledger->snapshot;
    return R3_COMMAND_LEDGER_OK_NEW;
}
