#ifndef R4_RUNTIME_TARGET_H
#define R4_RUNTIME_TARGET_H

#include <stdint.h>

#include "r4_runtime_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cortex-M4/FreeRTOS binding for the generic R4 runtime ledger.  These are
 * functional trace endpoints, not a removable recorder.  They perform no
 * allocation, logging, FreeRTOS API call, or recursive queue operation. */
R4_RuntimeStatus R4_RuntimeTarget_Initialize(void);
R4_RuntimeStatus R4_RuntimeTarget_OpenWindow(uint32_t window);
R4_RuntimeStatus R4_RuntimeTarget_CloseWindow(uint32_t window);
R4_RuntimeStatus R4_RuntimeTarget_Checkpoint(void);
R4_RuntimeStatus R4_RuntimeTarget_ReadNow(uint64_t *out);

/* A CPU accounting window belongs to the acquisition stream, not to a test
 * task.  Arm it before the stream starts; the production DMA completion
 * callback then opens it at S0 and seals it at S1. */
typedef struct
{
    uint32_t configured;
    uint32_t opened;
    uint32_t closed;
    uint32_t window;
    uint32_t open_sequence;
    uint32_t close_sequence;
    uint32_t last_sequence;
    uint32_t first_error_sequence;
    R4_RuntimeStatus open_status;
    R4_RuntimeStatus close_status;
    R4_RuntimeStatus boundary_status;
} R4_DmaWindowSnapshot;

R4_RuntimeStatus R4_RuntimeTarget_ArmDmaWindow(uint32_t window,
    uint32_t open_sequence, uint32_t close_sequence);
R4_RuntimeStatus R4_RuntimeTarget_OnDmaInputBoundary(uint32_t sequence);
R4_RuntimeStatus R4_RuntimeTarget_GetDmaWindowSnapshot(
    R4_DmaWindowSnapshot *out);

typedef struct
{
    uint32_t active;
    uint32_t operation;
    uint32_t malformed_count;
    uint32_t discarded_count;
    uint32_t lock_count;
    uint32_t commit_count;
    uint32_t unlock_count;
    uint32_t completed_count;
    uint64_t t_lock;
    uint64_t t_commit;
    uint64_t t_unlock;
    uint64_t max_total_cycles;
    uint64_t max_prefix_cycles;
    uint64_t max_suffix_cycles;
} R4_CompletionTimingSnapshot;

typedef struct
{
    uint64_t dma_irq_count;
    uint64_t dma_yield_requested_count;
    uint64_t dma_no_yield_count;
    /* Real DMA trace-entry to post-RuntimeEvent IRQ-exit span.  It includes
     * R4 entry/exit accounting, HAL/callback work and the common yield-tail;
     * Cortex-M exception entry/return remain explicitly outside this scope. */
    uint64_t dma_service_count;
    uint64_t dma_max_service_cycles;
    /* Profile-neutral raw-DWT observer used only by the frozen R4
     * perturbation A/B harness.  Its envelope is DMA vector entry through
     * the common tail immediately before portYIELD_FROM_ISR. */
    uint64_t dma_probe_count;
    uint64_t dma_probe_max_cycles;
} R4_DmaTailSnapshot;

/* Read-only witness of the existing FreeRTOS V11.1.0 traceISR route,
 * filtered by IPSR=15.  It never creates a second SysTick trace path. */
typedef struct
{
    uint64_t enter_count;
    uint64_t exit_count;
} R4_SysTickTraceSnapshot;

/* Read-only witness for the real FreeRTOS task-selection trace route.  It is
 * intentionally a semantic recorder: the first non-Idle TASK_SWITCHED_IN
 * must not be preceded by a fabricated TASK_SWITCHED_OUT. */
typedef struct
{
    uint64_t first_task_in_count;
    uint64_t task_out_before_first_in_count;
    uint64_t task_switched_out_count;
    uint64_t task_switched_in_count;
    uint64_t idle_switched_out_count;
    uint64_t idle_switched_in_count;
    uint32_t first_task_identity;
} R4_TaskTraceSnapshot;

/* Hooks/IRQs may only latch a fail-closed request.  The Communication owner
 * consumes it in task context through the normal blocking safe-stop path. */
typedef enum
{
    R4_RUNTIME_INFRA_NONE = 0,
    R4_RUNTIME_INFRA_TICK_SERVICE_GAP,
    R4_RUNTIME_INFRA_CLOCK64_MONITOR_GAP,
    R4_RUNTIME_INFRA_RUNTIME_EVENT
} R4_RuntimeInfrastructureFault;

typedef struct
{
    R4_RuntimeInfrastructureFault first_fault;
    uint32_t fail_closed_requested;
    uint64_t monitor_service_count;
    uint64_t last_monitor_cycle;
    uint64_t max_monitor_interval_cycles;
    uint64_t monitor_interval_limit_cycles;
} R4_RuntimeHealthSnapshot;

/* These three calls are bound around the exact V11.1.0 xQueueGenericSend
 * critical section.  They are intentionally separate from QueueAdapter's
 * semantic validation: t_commit is emitted only after its legal operation has
 * updated the ownership/token ledger. */
void R4_RuntimeTarget_CompletionLock(uint32_t operation);
void R4_RuntimeTarget_CompletionCommit(uint32_t operation);
void R4_RuntimeTarget_CompletionUnlock(uint32_t operation);
R4_RuntimeStatus R4_RuntimeTarget_GetCompletionTiming(
    R4_CompletionTimingSnapshot *out);

void R4_RuntimeTarget_TraceIsrEnter(void);
void R4_RuntimeTarget_TraceIsrExit(void);
R4_RuntimeStatus R4_RuntimeTarget_GetSysTickTraceSnapshot(
    R4_SysTickTraceSnapshot *out);
/* Called by the static-idle-memory application hook before the scheduler
 * starts.  It avoids a FreeRTOS API call from a trace macro while allowing
 * Idle residency to remain distinct from ordinary task residency. */
void R4_RuntimeTarget_BindIdleTask(void *task);
void R4_RuntimeTarget_TraceTaskSwitchedOut(void *task);
void R4_RuntimeTarget_TraceTaskSwitchedIn(void *task);
R4_RuntimeStatus R4_RuntimeTarget_GetTaskTraceSnapshot(
    R4_TaskTraceSnapshot *out);
/* Called exactly once from the real DMA IRQ common tail, before the port's
 * yield macro chooses either the no-switch or scheduler-request exit. */
void R4_RuntimeTarget_TraceDmaTailYield(uint32_t higher_priority_task_woken);
void R4_RuntimeTarget_ObserveDmaServiceEnter(void);
void R4_RuntimeTarget_ObserveDmaServiceExit(void);
R4_RuntimeStatus R4_RuntimeTarget_GetDmaTailSnapshot(R4_DmaTailSnapshot *out);
R4_RuntimeStatus R4_RuntimeTarget_MonitorService(uint64_t interval_limit_cycles);
void R4_RuntimeTarget_LatchInfrastructureFault(R4_RuntimeInfrastructureFault fault);
R4_RuntimeStatus R4_RuntimeTarget_GetHealthSnapshot(R4_RuntimeHealthSnapshot *out);

const R4_RuntimeLedger *R4_RuntimeTarget_GetLedger(void);

#if defined(STREAM_LAB_R4_HW)
/* Board-only T17 controls.  They are excluded from production profiles so a
 * diagnostic software-pended IRQ cannot become an experiment control path. */
R4_RuntimeStatus R4_RuntimeTarget_TestArmPendingIrq(void);
R4_RuntimeStatus R4_RuntimeTarget_TestArmNestedIrq(void);
void R4_RuntimeTarget_TestPendHighFromLowIrq(void);
R4_RuntimeStatus R4_RuntimeTarget_TestInjectDuplicateExit(uint32_t irq_id);
/* Fault-injection only: corrupt the next ledger settlement boundary so a
 * normal target Apply must latch TIME_REGRESSION and reject later events. */
R4_RuntimeStatus R4_RuntimeTarget_TestInjectTimeRegression(void);
R4_RuntimeStatus R4_RuntimeTarget_TestArmPendingCompletionIrq(void);
void R4_RuntimeTarget_TestObserveCompletionPendingIrq(void);
R4_RuntimeStatus R4_RuntimeTarget_TestGetCompletionPendingIrqSnapshot(
    volatile uint32_t *arm_count, volatile uint32_t *irq_count,
    volatile uint32_t *active_at_irq);
/* Diagnostic-only direct entrance used by the R4 microbenchmark.  It calls
 * the same RuntimeEvent Apply transaction as production trace endpoints; it
 * is never present in a production build. */
R4_RuntimeStatus R4_RuntimeTarget_TestApplyEvent(R4_RuntimeEventKind kind,
    uintptr_t identity);
/* Reset only the diagnostic ledger between benchmark samples.  Clock64 stays
 * live, so this never creates another clock authority. */
R4_RuntimeStatus R4_RuntimeTarget_TestResetLedger(void);
/* Board-only observer of the actual interval after RuntimeEvent raises
 * PRIMASK and before it restores the caller's saved value. */
R4_RuntimeStatus R4_RuntimeTarget_TestResetMaskTiming(void);
R4_RuntimeStatus R4_RuntimeTarget_TestGetMaskTiming(
    volatile uint32_t *sample_count, volatile uint64_t *max_cycles);
#endif

#ifdef __cplusplus
}
#endif

#endif /* R4_RUNTIME_TARGET_H */
