#ifndef R3_LIFECYCLE_H
#define R3_LIFECYCLE_H

#include <stdint.h>

#include "stream_run_authority.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    R3_LIFECYCLE_IDLE = 0,
    R3_LIFECYCLE_PREPARING,
    R3_LIFECYCLE_RUNNING,
    R3_LIFECYCLE_QUIESCING,
    R3_LIFECYCLE_RESET_REQUIRED
} R3LifecycleState;

typedef enum
{
    R3_LIFECYCLE_OK = 0,
    R3_LIFECYCLE_INVALID_ARGUMENT,
    R3_LIFECYCLE_INVALID_STATE,
    R3_LIFECYCLE_INVALID_CONFIG,
    R3_LIFECYCLE_STALE_TICKET,
    R3_LIFECYCLE_STOPPED,
    R3_LIFECYCLE_PREPARE_FAILED,
    R3_LIFECYCLE_ARM_FAILED,
    R3_LIFECYCLE_COMMIT_FAILED,
    R3_LIFECYCLE_ROLLBACK_FAILED,
    R3_LIFECYCLE_STATUS_RESET_REQUIRED
} R3LifecycleStatus;

typedef struct
{
    uint32_t boot_id;
    uint32_t request_id;
    uint32_t generation;
    uint32_t configuration_id;
} R3LifecycleStartRequest;

typedef struct
{
    StreamRunTicket stream_ticket;
    uint32_t start_ticket;
} R3LifecycleStartTicket;

typedef struct
{
    StreamRunTicket stream_ticket;
    uint32_t stop_id;
} R3LifecycleStopRequest;

/* All callbacks run in Communication task context and may block only where
 * their individual driver contract permits.  rollback must leave TIM2 stopped
 * and ADC/DMA no longer accessing sample memory before returning success. */
typedef struct
{
    void *context;
    R3LifecycleStatus (*validate)(void *context, const R3LifecycleStartRequest *request);
    R3LifecycleStatus (*prepare)(void *context, const R3LifecycleStartRequest *request,
                                 const StreamRunTicket *ticket);
    R3LifecycleStatus (*arm)(void *context, const R3LifecycleStartRequest *request,
                             const StreamRunTicket *ticket);
    R3LifecycleStatus (*commit_timer)(void *context, const StreamRunTicket *ticket);
    R3LifecycleStatus (*rollback)(void *context, const StreamRunTicket *ticket);
} R3LifecycleHooks;

typedef struct
{
    R3LifecycleState state;
    uint32_t boot_id;
    uint32_t run_id;
    uint32_t generation;
    uint32_t configuration_id;
    uint32_t start_ticket;
    uint32_t start_ticket_valid;
    uint32_t stop_id;
    uint32_t acquisition_publish_allowed;
    uint32_t processing_claim_allowed;
    uint32_t interference_release_allowed;
    uint32_t prepare_count;
    uint32_t commit_count;
    uint32_t rollback_count;
    uint32_t failure_count;
} R3LifecycleSnapshot;

void R3Lifecycle_Init(uint32_t boot_id, const R3LifecycleHooks *hooks);
R3LifecycleStatus R3Lifecycle_PrepareStart(const R3LifecycleStartRequest *request,
                                            R3LifecycleStartTicket *out_ticket);
R3LifecycleStatus R3Lifecycle_RequestStopBeforeCommit(const R3LifecycleStartTicket *ticket);
R3LifecycleStatus R3Lifecycle_CommitStart(const R3LifecycleStartTicket *ticket);
R3LifecycleStatus R3Lifecycle_RequestStop(const R3LifecycleStopRequest *request);
R3LifecycleStatus R3Lifecycle_GetSnapshot(R3LifecycleSnapshot *out);

#ifdef __cplusplus
}
#endif

#endif
