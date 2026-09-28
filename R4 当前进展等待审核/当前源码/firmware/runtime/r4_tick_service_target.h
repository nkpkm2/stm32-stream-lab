#ifndef R4_TICK_SERVICE_TARGET_H
#define R4_TICK_SERVICE_TARGET_H

#include "r4_tick_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/* FreeRTOS binding of the R4 service-sequence core.  The real tick hook is
 * its only writer.  Task-side ticket publication and snapshots take a short
 * scheduler critical section; no 64-bit volatile read is treated as atomic. */
typedef struct
{
    R4_TickServiceStartFn commit_start;
    R4_TickServiceReleaseFn release_job;
    void *context;
} R4_TickServiceTargetCallbacks;

/* DWT evidence for the one real vApplicationTickHook service domain.  A
 * profile sets the permitted gap threshold; callers do not infer it from the
 * raw FreeRTOS tick count. */
typedef struct
{
    uint64_t service_count;
    uint64_t first_service_cycle;
    uint64_t last_service_cycle;
    uint64_t max_interval_cycles;
    uint64_t max_phase_error_cycles;
    uint64_t over_limit_interval_count;
    uint64_t expected_tick_cycles;
    uint64_t interval_limit_cycles;
} R4_TickServiceTiming;

R4_TickServiceStatus R4_TickServiceTarget_Initialize(void);
R4_TickServiceStatus R4_TickServiceTarget_Register(
    const R4_TickServiceTargetCallbacks *callbacks);
R4_TickServiceStatus R4_TickServiceTarget_ArmStart(
    uint64_t q0,
    uint64_t period_ticks);
R4_TickServiceStatus R4_TickServiceTarget_CancelStart(void);
R4_TickServiceStatus R4_TickServiceTarget_CompleteJob(void);

/* Sole production hook entrance.  It is called once per actual
 * vApplicationTickHook invocation, never by a catch-up or test helper. */
R4_TickServiceStatus R4_TickServiceTarget_OnTickHook(void);
R4_TickServiceStatus R4_TickServiceTarget_GetSnapshot(R4_TickService *out);
R4_TickServiceStatus R4_TickServiceTarget_ConfigureTiming(
    uint64_t expected_tick_cycles, uint64_t interval_limit_cycles);
R4_TickServiceStatus R4_TickServiceTarget_GetTiming(R4_TickServiceTiming *out);

#ifdef __cplusplus
}
#endif

#endif /* R4_TICK_SERVICE_TARGET_H */
