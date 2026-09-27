#ifndef R3_COMMAND_LEDGER_H
#define R3_COMMAND_LEDGER_H

#include <stdint.h>

#include "r3_lifecycle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Communication-owned, one-entry command ledger.  A request id is bound to
 * the complete START binding for the lifetime of its result.  It is not a
 * cache: it is the authority that makes retries replay one run rather than
 * manufacture another run. */
typedef enum
{
    R3_COMMAND_LEDGER_OK_NEW = 0,
    R3_COMMAND_LEDGER_OK_REPLAY,
    R3_COMMAND_LEDGER_INVALID_ARGUMENT,
    R3_COMMAND_LEDGER_STALE_BOOT,
    R3_COMMAND_LEDGER_REQUEST_CONFLICT,
    R3_COMMAND_LEDGER_INVALID_STATE,
    R3_COMMAND_LEDGER_STOP_CONFLICT,
    R3_COMMAND_LEDGER_STALE_TICKET
} R3CommandLedgerStatus;

typedef enum
{
    R3_COMMAND_LEDGER_EMPTY = 0,
    R3_COMMAND_LEDGER_PREPARING,
    R3_COMMAND_LEDGER_RUNNING,
    R3_COMMAND_LEDGER_STOPPED
} R3CommandLedgerPhase;

typedef struct
{
    uint32_t boot_id;
    uint32_t request_id;
    uint32_t generation;
    uint32_t configuration_id;
} R3CommandLedgerBinding;

typedef struct
{
    R3CommandLedgerPhase phase;
    R3CommandLedgerBinding binding;
    R3LifecycleStartTicket start_ticket;
    uint32_t start_ticket_valid;
    uint32_t stop_id;
    uint32_t stop_result_valid;
} R3CommandLedgerSnapshot;

typedef struct
{
    uint32_t boot_id;
    R3CommandLedgerSnapshot snapshot;
} R3CommandLedger;

void R3CommandLedger_Initialize(R3CommandLedger *ledger, uint32_t boot_id);
R3CommandLedgerStatus R3CommandLedger_BeginStart(
    R3CommandLedger *ledger, const R3LifecycleStartRequest *request,
    R3LifecycleStartTicket *out_replay_ticket);
R3CommandLedgerStatus R3CommandLedger_RecordPrepared(
    R3CommandLedger *ledger, const R3LifecycleStartTicket *ticket);
R3CommandLedgerStatus R3CommandLedger_RecordRunning(
    R3CommandLedger *ledger, const R3LifecycleStartTicket *ticket);
R3CommandLedgerStatus R3CommandLedger_AbortStart(
    R3CommandLedger *ledger, const R3LifecycleStartRequest *request);
R3CommandLedgerStatus R3CommandLedger_BeginStop(
    R3CommandLedger *ledger, const R3LifecycleStopRequest *request);
R3CommandLedgerStatus R3CommandLedger_RecordStopped(
    R3CommandLedger *ledger, const R3LifecycleStopRequest *request);
R3CommandLedgerStatus R3CommandLedger_GetSnapshot(
    const R3CommandLedger *ledger, R3CommandLedgerSnapshot *out);

#ifdef __cplusplus
}
#endif

#endif
