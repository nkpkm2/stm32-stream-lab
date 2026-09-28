#ifndef R4_TICK_SERVICE_H
#define R4_TICK_SERVICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* R4's service sequence is deliberately independent of the FreeRTOS tick.
 * Only the real tick hook advances it; test injection uses a separate
 * instance and never invokes the production hook. */
typedef enum
{
    R4_TICK_SERVICE_OK = 0,
    R4_TICK_SERVICE_INVALID_ARGUMENT,
    R4_TICK_SERVICE_INVALID_STATE,
    R4_TICK_SERVICE_START_FAILED,
    R4_TICK_SERVICE_CALLBACK_FAILED
} R4_TickServiceStatus;

typedef int (*R4_TickServiceStartFn)(uint64_t service_seq, void *context);
typedef int (*R4_TickServiceReleaseFn)(
    uint64_t planned_service_seq,
    uint64_t actual_cycle,
    void *context);
typedef uint64_t (*R4_TickServiceNowFn)(void *context);

typedef struct
{
    R4_TickServiceNowFn now;
    R4_TickServiceStartFn commit_start;
    R4_TickServiceReleaseFn release_job;
    void *context;
} R4_TickServiceCallbacks;

typedef struct
{
    uint32_t initialized;
    uint32_t start_pending;
    uint32_t start_committed;
    uint32_t releases_enabled;
    uint32_t job_occupied;
    R4_TickServiceStatus first_error;
    uint64_t service_seq;
    uint64_t q0;
    uint64_t period_ticks;
    uint64_t next_release_seq;
    uint64_t start_count;
    uint64_t release_count;
    uint64_t skipped_count;
    uint64_t last_release_cycle;
    R4_TickServiceCallbacks callbacks;
} R4_TickService;

R4_TickServiceStatus R4_TickService_Initialize(
    R4_TickService *service,
    const R4_TickServiceCallbacks *callbacks);

/* Publishes an uncommitted start ticket. q0 must be at least two *future*
 * service events so all expensive preparation is complete beforehand. */
R4_TickServiceStatus R4_TickService_ArmStart(
    R4_TickService *service,
    uint64_t q0,
    uint64_t period_ticks);
R4_TickServiceStatus R4_TickService_CancelStart(R4_TickService *service);
R4_TickServiceStatus R4_TickService_CompleteJob(R4_TickService *service);

/* Called once by each real vApplicationTickHook invocation. */
R4_TickServiceStatus R4_TickService_OnService(R4_TickService *service);

#ifdef __cplusplus
}
#endif

#endif /* R4_TICK_SERVICE_H */
