#ifndef R4_RUNTIME_EVENT_H
#define R4_RUNTIME_EVENT_H

#include <stdint.h>

#include "r4_clock64.h"

#ifdef __cplusplus
extern "C" {
#endif

#define R4_RUNTIME_MAX_IRQ_NESTING 8U
#define R4_RUNTIME_WINDOW_COUNT 3U
#define R4_RUNTIME_MAX_TASK_BUCKETS 8U
#define R4_RUNTIME_MAX_IRQ_BUCKETS 16U

typedef uint32_t (*R4_RuntimeReadCycleFn)(void *context);
typedef uint32_t (*R4_RuntimeSaveDisableFn)(void *context);
typedef void (*R4_RuntimeRestoreFn)(uint32_t saved_mask, void *context);

typedef struct
{
    R4_RuntimeReadCycleFn read_cycle;
    R4_RuntimeSaveDisableFn save_and_disable;
    R4_RuntimeRestoreFn restore;
    void *context;
} R4_RuntimePlatform;

typedef enum
{
    R4_RUNTIME_EVENT_IRQ_ENTER = 0,
    R4_RUNTIME_EVENT_IRQ_EXIT,
    R4_RUNTIME_EVENT_TASK_SWITCHED_OUT,
    R4_RUNTIME_EVENT_TASK_SWITCHED_IN,
    R4_RUNTIME_EVENT_IDLE_SWITCHED_OUT,
    R4_RUNTIME_EVENT_IDLE_SWITCHED_IN,
    R4_RUNTIME_EVENT_WINDOW_OPEN,
    R4_RUNTIME_EVENT_WINDOW_CLOSE,
    R4_RUNTIME_EVENT_CHECKPOINT
} R4_RuntimeEventKind;

typedef enum
{
    R4_RUNTIME_OK = 0,
    R4_RUNTIME_INVALID_ARGUMENT,
    R4_RUNTIME_NOT_INITIALIZED,
    R4_RUNTIME_PLATFORM_ERROR,
    R4_RUNTIME_CLOCK_ERROR,
    R4_RUNTIME_TIME_REGRESSION,
    R4_RUNTIME_IRQ_NESTING_OVERFLOW,
    R4_RUNTIME_IRQ_EXIT_MISMATCH,
    R4_RUNTIME_TASK_SWITCH_MISMATCH,
    R4_RUNTIME_WINDOW_ERROR,
    R4_RUNTIME_OWNER_CAPACITY_EXCEEDED,
    R4_RUNTIME_FAULTED
} R4_RuntimeStatus;

typedef enum
{
    R4_RUNTIME_CONTEXT_NONE = 0,
    R4_RUNTIME_CONTEXT_IDLE,
    R4_RUNTIME_CONTEXT_TASK,
    R4_RUNTIME_CONTEXT_IRQ
} R4_RuntimeContextKind;

typedef struct
{
    R4_RuntimeContextKind kind;
    uintptr_t identity;
} R4_RuntimeContext;

typedef struct
{
    uint32_t irq_id;
    R4_RuntimeContext interrupted;
} R4_RuntimeIrqFrame;

/* Fixed, allocation-free owner accounting.  A full table is a measurement
 * fault: silently merging a new task or IRQ into another owner is forbidden.
 * Identity zero is reserved as an unused entry. */
typedef struct
{
    uintptr_t identity;
    uint64_t cycles;
} R4_RuntimeOwnerBucket;

typedef struct
{
    uint32_t initialized;
    uint32_t faulted;
    R4_RuntimeStatus first_error;
    R4_Clock64 *clock;
    R4_RuntimePlatform platform;

    uint64_t last_time;
    uint64_t event_serial;
    R4_RuntimeContext active;
    R4_RuntimeIrqFrame irq_stack[R4_RUNTIME_MAX_IRQ_NESTING];
    uint32_t irq_depth;

    uint32_t window_open[R4_RUNTIME_WINDOW_COUNT];
    /* Closed-window data is the formal result.  It is deliberately distinct
     * from the whole-run counters below: activity after CLOSE remains useful
     * live diagnostics but must never mutate a sealed CPU-window result. */
    uint64_t window_cycles[R4_RUNTIME_WINDOW_COUNT];
    uint64_t window_task_cycles[R4_RUNTIME_WINDOW_COUNT];
    uint64_t window_irq_cycles[R4_RUNTIME_WINDOW_COUNT];
    uint64_t window_idle_cycles[R4_RUNTIME_WINDOW_COUNT];
    uint64_t window_unclassified_cycles[R4_RUNTIME_WINDOW_COUNT];
    R4_RuntimeOwnerBucket
        window_task_buckets[R4_RUNTIME_WINDOW_COUNT][R4_RUNTIME_MAX_TASK_BUCKETS];
    R4_RuntimeOwnerBucket
        window_irq_buckets[R4_RUNTIME_WINDOW_COUNT][R4_RUNTIME_MAX_IRQ_BUCKETS];

    /* Live, whole-runtime diagnostics.  These intentionally continue after
     * every formal window is closed and must not be combined with a formal
     * window denominator when reporting utilization. */
    uint64_t task_cycles;
    uint64_t irq_cycles;
    uint64_t idle_cycles;
    uint64_t unclassified_cycles;
    R4_RuntimeOwnerBucket task_buckets[R4_RUNTIME_MAX_TASK_BUCKETS];
    R4_RuntimeOwnerBucket irq_buckets[R4_RUNTIME_MAX_IRQ_BUCKETS];
} R4_RuntimeLedger;

typedef struct
{
    R4_RuntimeEventKind kind;
    uintptr_t identity;
} R4_RuntimeEvent;

typedef struct
{
    uint64_t time;
    uint64_t serial;
    R4_RuntimeStatus status;
} R4_RuntimeEventReceipt;

R4_RuntimeStatus R4_RuntimeLedger_Initialize(
    R4_RuntimeLedger *ledger,
    R4_Clock64 *clock,
    const R4_RuntimePlatform *platform,
    R4_RuntimeContext initial_context);

/* The only state-mutating runtime accounting entrance.  It obtains time,
 * settles [last_time, now), checks nesting/identity, and changes the window
 * or active context under one saved-PRIMASK transaction. */
R4_RuntimeStatus R4_RuntimeEvent_Apply(
    R4_RuntimeLedger *ledger,
    const R4_RuntimeEvent *event,
    R4_RuntimeEventReceipt *out_receipt);

/* Fast path for an already-held R4 PRIMASK transaction.  It is deliberately
 * restricted to a checkpoint: callers cannot use it to bypass IRQ/task/window
 * identity transitions.  CompletionAdapter uses this at t_commit so logical
 * timestamping remains in the same protected section without paying the
 * generic callback/dispatch overhead a second time. */
R4_RuntimeStatus R4_RuntimeLedger_CheckpointLocked(
    R4_RuntimeLedger *ledger,
    uint32_t raw_cycle,
    R4_RuntimeEventReceipt *out_receipt);

/* CompletionAdapter needs only the committed timestamp.  This equivalent
 * checkpoint avoids materialising a diagnostic receipt in the measured queue
 * prefix; it retains exactly the same Clock64, settle and fault semantics. */
R4_RuntimeStatus R4_RuntimeLedger_CheckpointLockedTime(
    R4_RuntimeLedger *ledger,
    uint32_t raw_cycle,
    uint64_t *out_time);

R4_RuntimeStatus R4_RuntimeLedger_GetStatus(const R4_RuntimeLedger *ledger);

#ifdef __cplusplus
}
#endif

#endif /* R4_RUNTIME_EVENT_H */
