#ifndef R3_W3_RUNTIME_H
#define R3_W3_RUNTIME_H

#include <stdint.h>

#include "adc_dbm_driver.h"
#include "r3_command_ledger.h"
#include "r3_lifecycle.h"
#include "r3_result_store.h"
#include "r3_worker_tasks.h"
#include "stream_ownership_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * W3's concrete Communication-task integration of the lifecycle transaction.
 * All public calls are task-context only and are rejected unless issued by the
 * Communication task which initialized this module.  This keeps workers and
 * DMA callbacks outside the START/rollback authority boundary.
 */

#define R3_W3_RUNTIME_BLOCK_SAMPLES 256U

typedef enum
{
    R3_W3_RUNTIME_OK = 0,
    R3_W3_RUNTIME_INVALID_ARGUMENT,
    R3_W3_RUNTIME_INVALID_STATE,
    R3_W3_RUNTIME_LIFECYCLE_ERROR,
    R3_W3_RUNTIME_WORKER_ERROR,
    R3_W3_RUNTIME_AUTHORITY_ERROR,
    R3_W3_RUNTIME_DRIVER_ERROR,
    R3_W3_RUNTIME_RESET_REQUIRED,
    R3_W3_RUNTIME_REQUEST_CONFLICT,
    R3_W3_RUNTIME_STALE_COMMAND,
    R3_W3_RUNTIME_RESULT_BUSY
} R3W3RuntimeStatus;

typedef struct
{
    uint32_t boot_id;
    uint32_t k;
    /* Target-harness-only fault selectors.  They are explicit and are never
     * enabled by the ordinary runtime configuration. */
    uint32_t inject_arm_failure;
    uint32_t inject_commit_failure;
    /* Target-harness-only STOP-edge controls.  Production startup leaves both
     * at zero: completions notify Processing immediately and callbacks do not
     * deliberately hold a lease. */
    uint32_t suppress_processing_notify;
    uint32_t processing_hold_ticks;
} R3W3RuntimeConfig;

typedef struct
{
    uint32_t initialized;
    uint32_t coordinator_valid;
    uint32_t current_run_valid;
    uint32_t current_stop_valid;
    uint32_t rollback_ack_mask;
    uint32_t runtime_fault;
    uint32_t processing_entered;
    uint32_t stop_begin_report_valid;
    uint32_t stop_report_valid;
    R3CommandLedgerSnapshot command_ledger;
    R3ResultStoreSnapshot result_store;
    AdcDbmDriverStopBeginReport stop_begin_report;
    AdcDbmDriverStopReport stop_report;
    R3LifecycleSnapshot lifecycle;
    AdcDbmDriverSnapshot driver;
    StreamRunAuthoritySnapshot authority;
    StreamOwnershipSnapshot ownership;
    R3WorkerTasksSnapshot workers;
} R3W3RuntimeSnapshot;

R3W3RuntimeStatus R3W3Runtime_Initialize(const R3W3RuntimeConfig *config);
R3W3RuntimeStatus R3W3Runtime_PrepareStart(
    const R3LifecycleStartRequest *request,
    R3LifecycleStartTicket *out_ticket);
R3W3RuntimeStatus R3W3Runtime_CommitStart(const R3LifecycleStartTicket *ticket);
R3W3RuntimeStatus R3W3Runtime_StopBeforeCommit(
    const R3LifecycleStartTicket *ticket);
R3W3RuntimeStatus R3W3Runtime_StopRunning(uint32_t stop_id);
/* Atomic Communication command APIs used by the W5 protocol layer. */
R3W3RuntimeStatus R3W3Runtime_Start(
    const R3LifecycleStartRequest *request,
    R3LifecycleStartTicket *out_ticket);
R3W3RuntimeStatus R3W3Runtime_Stop(
    const R3LifecycleStopRequest *request);
R3W3RuntimeStatus R3W3Runtime_AcquireResult(uint32_t result_id,
    const uint8_t **out_bytes, uint32_t *out_size);
R3W3RuntimeStatus R3W3Runtime_ReleaseResult(uint32_t result_id);
R3W3RuntimeStatus R3W3Runtime_GetSnapshot(R3W3RuntimeSnapshot *out);

/* R4-only outer-IRQ tail handoff.  The DMA callback accumulates a wake request
 * but never invokes portYIELD_FROM_ISR itself, so the real hardware IRQ has
 * exactly one traceISR_EXIT path at its outer handler tail. */
BaseType_t R3W3Runtime_TakeDmaYieldRequest(void);

#ifdef __cplusplus
}
#endif

#endif
