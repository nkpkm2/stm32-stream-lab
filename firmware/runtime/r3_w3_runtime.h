#ifndef R3_W3_RUNTIME_H
#define R3_W3_RUNTIME_H

#include <stdint.h>

#include "adc_dbm_driver.h"
#include "r3_lifecycle.h"
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
    R3_W3_RUNTIME_RESET_REQUIRED
} R3W3RuntimeStatus;

typedef struct
{
    uint32_t boot_id;
    uint32_t k;
    /* Target-harness-only fault selectors.  They are explicit and are never
     * enabled by the ordinary runtime configuration. */
    uint32_t inject_arm_failure;
    uint32_t inject_commit_failure;
} R3W3RuntimeConfig;

typedef struct
{
    uint32_t initialized;
    uint32_t coordinator_valid;
    uint32_t current_run_valid;
    uint32_t current_stop_valid;
    uint32_t rollback_ack_mask;
    uint32_t runtime_fault;
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
R3W3RuntimeStatus R3W3Runtime_GetSnapshot(R3W3RuntimeSnapshot *out);

#ifdef __cplusplus
}
#endif

#endif
