#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "r3_command_ledger.h"
#include "r3_result_store.h"

#define CHECK(x) do { if (!(x)) { (void)fprintf(stderr, "CHECK %s:%d\n", __FILE__, __LINE__); exit(EXIT_FAILURE); } } while (0)

static R3LifecycleStartRequest Start(uint32_t request_id, uint32_t generation,
    uint32_t configuration_id)
{
    R3LifecycleStartRequest request = { 7U, request_id, generation, configuration_id };
    return request;
}

static R3LifecycleStartTicket Ticket(uint32_t request_id, uint32_t generation)
{
    R3LifecycleStartTicket ticket;
    (void)memset(&ticket, 0, sizeof(ticket));
    ticket.stream_ticket.identity.boot_id = 7U;
    ticket.stream_ticket.identity.run_id = request_id;
    ticket.stream_ticket.identity.generation = generation;
    ticket.start_ticket = request_id + generation;
    return ticket;
}

static R3LifecycleStopRequest Stop(R3LifecycleStartTicket ticket, uint32_t stop_id)
{
    R3LifecycleStopRequest stop;
    stop.stream_ticket = ticket.stream_ticket;
    stop.stop_id = stop_id;
    return stop;
}

static void BeginAndRun(R3CommandLedger *ledger, R3LifecycleStartRequest request,
    R3LifecycleStartTicket ticket)
{
    R3LifecycleStartTicket replay;
    CHECK(R3CommandLedger_BeginStart(ledger, &request, &replay) == R3_COMMAND_LEDGER_OK_NEW);
    CHECK(R3CommandLedger_RecordPrepared(ledger, &ticket) == R3_COMMAND_LEDGER_OK_NEW);
    CHECK(R3CommandLedger_RecordRunning(ledger, &ticket) == R3_COMMAND_LEDGER_OK_NEW);
}

static void CaseDuplicateStart(void)
{
    R3CommandLedger ledger;
    R3LifecycleStartRequest request = Start(11U, 3U, 1U);
    R3LifecycleStartTicket ticket = Ticket(11U, 3U);
    R3LifecycleStartTicket replay;
    R3CommandLedger_Initialize(&ledger, 7U);
    BeginAndRun(&ledger, request, ticket);
    CHECK(R3CommandLedger_BeginStart(&ledger, &request, &replay) == R3_COMMAND_LEDGER_OK_REPLAY);
    CHECK(replay.start_ticket == ticket.start_ticket);
}

static void CaseRequestConflict(void)
{
    R3CommandLedger ledger;
    R3LifecycleStartRequest request = Start(11U, 3U, 1U);
    R3LifecycleStartRequest conflict = Start(11U, 3U, 2U);
    R3LifecycleStartTicket ticket = Ticket(11U, 3U);
    R3LifecycleStartTicket replay;
    R3CommandLedger_Initialize(&ledger, 7U);
    BeginAndRun(&ledger, request, ticket);
    CHECK(R3CommandLedger_BeginStart(&ledger, &conflict, &replay) == R3_COMMAND_LEDGER_REQUEST_CONFLICT);
}

static void CaseStopAndOldBoot(void)
{
    R3CommandLedger ledger;
    R3LifecycleStartRequest request = Start(11U, 3U, 1U);
    R3LifecycleStartTicket ticket = Ticket(11U, 3U);
    R3LifecycleStopRequest stop = Stop(ticket, 21U);
    R3LifecycleStartTicket replay;
    R3CommandLedger_Initialize(&ledger, 7U);
    BeginAndRun(&ledger, request, ticket);
    CHECK(R3CommandLedger_BeginStop(&ledger, &stop) == R3_COMMAND_LEDGER_OK_NEW);
    CHECK(R3CommandLedger_RecordStopped(&ledger, &stop) == R3_COMMAND_LEDGER_OK_NEW);
    CHECK(R3CommandLedger_BeginStop(&ledger, &stop) == R3_COMMAND_LEDGER_OK_REPLAY);
    request.boot_id = 6U;
    CHECK(R3CommandLedger_BeginStart(&ledger, &request, &replay) == R3_COMMAND_LEDGER_STALE_BOOT);
}

static void CaseResultBusyAndRelease(void)
{
    R3ResultStore store;
    StreamRunIdentity identity = { 7U, 11U, 3U };
    const uint8_t *bytes;
    uint32_t size;
    uint8_t copy[R3_RESULT_STORE_BYTES];
    R3ResultStore_Initialize(&store);
    CHECK(R3ResultStore_Seal(&store, &identity, 21U) == R3_RESULT_STORE_OK);
    CHECK(R3ResultStore_Acquire(&store, 21U, &bytes, &size) == R3_RESULT_STORE_OK);
    CHECK(size == R3_RESULT_STORE_BYTES);
    (void)memcpy(copy, bytes, sizeof(copy));
    CHECK(R3ResultStore_CanBeginRun(&store) == R3_RESULT_STORE_BUSY);
    CHECK(R3ResultStore_GetSnapshot(&store, &(R3ResultStoreSnapshot){0}) == R3_RESULT_STORE_OK);
    CHECK(memcmp(copy, bytes, sizeof(copy)) == 0);
    CHECK(R3ResultStore_Release(&store, 21U) == R3_RESULT_STORE_OK);
    CHECK(R3ResultStore_CanBeginRun(&store) == R3_RESULT_STORE_OK);
}

int main(int argc, char **argv)
{
    if (argc != 2) return EXIT_FAILURE;
    if (strcmp(argv[1], "duplicate_start") == 0) CaseDuplicateStart();
    else if (strcmp(argv[1], "request_conflict") == 0) CaseRequestConflict();
    else if (strcmp(argv[1], "stop_old_boot") == 0) CaseStopAndOldBoot();
    else if (strcmp(argv[1], "result_busy_release") == 0) CaseResultBusyAndRelease();
    else return EXIT_FAILURE;
    (void)puts("R3-W5 ledger/result native test: PASS");
    return EXIT_SUCCESS;
}
