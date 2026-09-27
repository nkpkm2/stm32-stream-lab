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

#ifdef __cplusplus
}
#endif

#endif /* R4_TICK_SERVICE_TARGET_H */
