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
} R4_DmaTailSnapshot;

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
void R4_RuntimeTarget_TraceTaskSwitchedOut(void *task);
void R4_RuntimeTarget_TraceTaskSwitchedIn(void *task);
/* Called exactly once from the real DMA IRQ common tail, before the port's
 * yield macro chooses either the no-switch or scheduler-request exit. */
void R4_RuntimeTarget_TraceDmaTailYield(uint32_t higher_priority_task_woken);
R4_RuntimeStatus R4_RuntimeTarget_GetDmaTailSnapshot(R4_DmaTailSnapshot *out);

const R4_RuntimeLedger *R4_RuntimeTarget_GetLedger(void);

#if defined(STREAM_LAB_R4_HW)
/* Board-only T17 controls.  They are excluded from production profiles so a
 * diagnostic software-pended IRQ cannot become an experiment control path. */
R4_RuntimeStatus R4_RuntimeTarget_TestArmPendingIrq(void);
R4_RuntimeStatus R4_RuntimeTarget_TestInjectDuplicateExit(uint32_t irq_id);
#endif

#ifdef __cplusplus
}
#endif

#endif /* R4_RUNTIME_TARGET_H */
