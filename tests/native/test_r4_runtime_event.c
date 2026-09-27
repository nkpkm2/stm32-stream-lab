#include "r4_clock64.h"
#include "r4_runtime_event.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { \
    if (!(x)) { \
        (void)fprintf(stderr, "CHECK failed: %s (%s:%d)\n", \
            #x, __FILE__, __LINE__); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

typedef struct
{
    uint32_t raw;
    uint32_t mask;
    uint32_t save_count;
    uint32_t restore_count;
} FakePlatform;

static uint32_t ReadCycle(void *context)
{
    return ((FakePlatform *)context)->raw;
}

static uint32_t SaveAndDisable(void *context)
{
    FakePlatform *platform = (FakePlatform *)context;
    uint32_t previous = platform->mask;
    platform->mask = 1U;
    ++platform->save_count;
    return previous;
}

static void Restore(uint32_t saved_mask, void *context)
{
    FakePlatform *platform = (FakePlatform *)context;
    platform->mask = saved_mask;
    ++platform->restore_count;
}

static R4_RuntimeLedger NewLedger(FakePlatform *platform, R4_Clock64 *clock)
{
    R4_RuntimePlatform ops;
    R4_RuntimeLedger ledger;
    R4_RuntimeContext initial;

    (void)memset(&ops, 0, sizeof(ops));
    ops.read_cycle = ReadCycle;
    ops.save_and_disable = SaveAndDisable;
    ops.restore = Restore;
    ops.context = platform;
    CHECK(R4_Clock64_InitializeLocked(clock, platform->raw) == R4_CLOCK64_OK);
    initial.kind = R4_RUNTIME_CONTEXT_TASK;
    initial.identity = 11U;
    CHECK(R4_RuntimeLedger_Initialize(&ledger, clock, &ops, initial) ==
        R4_RUNTIME_OK);
    return ledger;
}

static R4_RuntimeStatus Apply(
    R4_RuntimeLedger *ledger,
    FakePlatform *platform,
    uint32_t raw,
    R4_RuntimeEventKind kind,
    uintptr_t identity)
{
    R4_RuntimeEvent event;
    R4_RuntimeEventReceipt receipt;
    platform->raw = raw;
    event.kind = kind;
    event.identity = identity;
    return R4_RuntimeEvent_Apply(ledger, &event, &receipt);
}

static uint64_t BucketCycles(const R4_RuntimeOwnerBucket *buckets,
    uint32_t capacity, uintptr_t identity)
{
    uint32_t index;

    for (index = 0U; index < capacity; ++index)
    {
        if (buckets[index].identity == identity)
        {
            return buckets[index].cycles;
        }
    }
    return 0U;
}

static void CaseClockWrap(void)
{
    R4_Clock64 clock;
    uint64_t value;
    CHECK(R4_Clock64_InitializeLocked(&clock, UINT32_MAX - 3U) == R4_CLOCK64_OK);
    CHECK(R4_Clock64_ReadLocked(&clock, 2U, &value) == R4_CLOCK64_OK);
    CHECK(value == UINT64_C(0x100000002));
    CHECK(clock.wrap_count == 1U);
}

static void CaseClockNoWrapAndSameCycle(void)
{
    R4_Clock64 clock;
    uint64_t value;

    CHECK(R4_Clock64_InitializeLocked(&clock, 100U) == R4_CLOCK64_OK);
    /* Equal DWT timestamps are legal; event sequencing, not a fabricated
     * clock increment, orders same-cycle RuntimeEvent calls. */
    CHECK(R4_Clock64_ReadLocked(&clock, 100U, &value) == R4_CLOCK64_OK);
    CHECK(value == 100U);
    CHECK(R4_Clock64_ReadLocked(&clock, 101U, &value) == R4_CLOCK64_OK);
    CHECK(value == 101U);
    CHECK(clock.high_word == 0U);
    CHECK(clock.wrap_count == 0U);
    CHECK(clock.read_count == 3U);
}

static void CaseClockMultipleSyntheticWraps(void)
{
    R4_Clock64 clock;
    uint64_t value;

    CHECK(R4_Clock64_InitializeLocked(&clock, UINT32_MAX - 1U) ==
        R4_CLOCK64_OK);
    CHECK(R4_Clock64_ReadLocked(&clock, 1U, &value) == R4_CLOCK64_OK);
    CHECK(value == UINT64_C(0x100000001));
    /* Advance to the high end of the first extended epoch, then cross a
     * second synthetic wrap.  Each raw decrease must advance exactly one
     * high word and preserve the 64-bit monotonic sequence. */
    CHECK(R4_Clock64_ReadLocked(&clock, UINT32_MAX, &value) == R4_CLOCK64_OK);
    CHECK(value == UINT64_C(0x1FFFFFFFF));
    CHECK(R4_Clock64_ReadLocked(&clock, 0U, &value) == R4_CLOCK64_OK);
    CHECK(value == UINT64_C(0x200000000));
    CHECK(clock.high_word == 2U);
    CHECK(clock.wrap_count == 2U);
}

static void CaseAtomicWindow(void)
{
    FakePlatform platform = { 100U, 0U, 0U, 0U };
    R4_Clock64 clock;
    R4_RuntimeLedger ledger = NewLedger(&platform, &clock);

    CHECK(Apply(&ledger, &platform, 110U, R4_RUNTIME_EVENT_WINDOW_OPEN, 0U) ==
        R4_RUNTIME_OK);
    CHECK(Apply(&ledger, &platform, 130U, R4_RUNTIME_EVENT_CHECKPOINT, 0U) ==
        R4_RUNTIME_OK);
    CHECK(Apply(&ledger, &platform, 150U, R4_RUNTIME_EVENT_WINDOW_CLOSE, 0U) ==
        R4_RUNTIME_OK);
    CHECK(ledger.window_cycles[0] == 40U);
    CHECK(ledger.task_cycles == 50U);
    CHECK(platform.save_count == 3U);
    CHECK(platform.restore_count == 3U);
    CHECK(platform.mask == 0U);
}

static void CaseMaskedEntryRestored(void)
{
    FakePlatform platform = { 100U, 1U, 0U, 0U };
    R4_Clock64 clock;
    R4_RuntimeLedger ledger = NewLedger(&platform, &clock);

    CHECK(Apply(&ledger, &platform, 110U, R4_RUNTIME_EVENT_CHECKPOINT, 0U) ==
        R4_RUNTIME_OK);
    CHECK(platform.save_count == 1U);
    CHECK(platform.restore_count == 1U);
    /* RuntimeEvent must restore the caller's masked state, never unmask it. */
    CHECK(platform.mask == 1U);
}

static void CaseWindowClipping(void)
{
    FakePlatform platform = { 0U, 0U, 0U, 0U };
    R4_Clock64 clock;
    R4_RuntimeLedger ledger = NewLedger(&platform, &clock);

    /* [0,10) is before, [10,30) is inside, [30,50) is after.  The single
     * continuous task interval deliberately crosses both window boundaries. */
    CHECK(Apply(&ledger, &platform, 10U, R4_RUNTIME_EVENT_WINDOW_OPEN, 0U) ==
        R4_RUNTIME_OK);
    CHECK(Apply(&ledger, &platform, 20U, R4_RUNTIME_EVENT_CHECKPOINT, 0U) ==
        R4_RUNTIME_OK);
    CHECK(Apply(&ledger, &platform, 30U, R4_RUNTIME_EVENT_WINDOW_CLOSE, 0U) ==
        R4_RUNTIME_OK);
    CHECK(ledger.window_cycles[0] == 20U);
    CHECK(ledger.window_task_cycles[0] == 20U);
    CHECK(ledger.window_irq_cycles[0] == 0U);
    CHECK(ledger.window_idle_cycles[0] == 0U);
    CHECK(ledger.window_unclassified_cycles[0] == 0U);
    CHECK(BucketCycles(ledger.window_task_buckets[0],
        R4_RUNTIME_MAX_TASK_BUCKETS, 11U) == 20U);
    CHECK(Apply(&ledger, &platform, 50U, R4_RUNTIME_EVENT_CHECKPOINT, 0U) ==
        R4_RUNTIME_OK);
    /* Formal closed-window data is immutable; live task accounting continues. */
    CHECK(ledger.window_cycles[0] == 20U);
    CHECK(ledger.window_task_cycles[0] == 20U);
    CHECK(BucketCycles(ledger.window_task_buckets[0],
        R4_RUNTIME_MAX_TASK_BUCKETS, 11U) == 20U);
    CHECK(ledger.task_cycles == 50U);
}

static void CaseWindowOwnerAttribution(void)
{
    FakePlatform platform = { 0U, 0U, 0U, 0U };
    R4_Clock64 clock;
    R4_RuntimeLedger ledger = NewLedger(&platform, &clock);

    /* [10,35) is formal: task 11 owns [10,15) and [25,35), while IRQ 41
     * owns [15,25).  An event after CLOSE proves that the formal per-owner
     * result freezes even though whole-run accounting continues. */
    CHECK(Apply(&ledger, &platform, 10U, R4_RUNTIME_EVENT_WINDOW_OPEN, 0U) ==
        R4_RUNTIME_OK);
    CHECK(Apply(&ledger, &platform, 15U, R4_RUNTIME_EVENT_IRQ_ENTER, 41U) ==
        R4_RUNTIME_OK);
    CHECK(Apply(&ledger, &platform, 25U, R4_RUNTIME_EVENT_IRQ_EXIT, 41U) ==
        R4_RUNTIME_OK);
    CHECK(Apply(&ledger, &platform, 35U, R4_RUNTIME_EVENT_WINDOW_CLOSE, 0U) ==
        R4_RUNTIME_OK);
    CHECK(ledger.window_cycles[0] == 25U);
    CHECK(ledger.window_task_cycles[0] == 15U);
    CHECK(ledger.window_irq_cycles[0] == 10U);
    CHECK(ledger.window_idle_cycles[0] == 0U);
    CHECK(ledger.window_unclassified_cycles[0] == 0U);
    CHECK(BucketCycles(ledger.window_task_buckets[0],
        R4_RUNTIME_MAX_TASK_BUCKETS, 11U) == 15U);
    CHECK(BucketCycles(ledger.window_irq_buckets[0],
        R4_RUNTIME_MAX_IRQ_BUCKETS, 41U) == 10U);
    CHECK(Apply(&ledger, &platform, 45U, R4_RUNTIME_EVENT_CHECKPOINT, 0U) ==
        R4_RUNTIME_OK);
    CHECK(ledger.window_task_cycles[0] == 15U);
    CHECK(ledger.window_irq_cycles[0] == 10U);
    CHECK(ledger.task_cycles == 35U);
    CHECK(ledger.irq_cycles == 10U);
}

static void CaseWindowEntirelyOutside(void)
{
    FakePlatform platform = { 0U, 0U, 0U, 0U };
    R4_Clock64 clock;
    R4_RuntimeLedger ledger = NewLedger(&platform, &clock);

    CHECK(Apply(&ledger, &platform, 10U, R4_RUNTIME_EVENT_CHECKPOINT, 0U) ==
        R4_RUNTIME_OK);
    CHECK(Apply(&ledger, &platform, 20U, R4_RUNTIME_EVENT_WINDOW_OPEN, 0U) ==
        R4_RUNTIME_OK);
    CHECK(Apply(&ledger, &platform, 20U, R4_RUNTIME_EVENT_WINDOW_CLOSE, 0U) ==
        R4_RUNTIME_OK);
    CHECK(Apply(&ledger, &platform, 40U, R4_RUNTIME_EVENT_CHECKPOINT, 0U) ==
        R4_RUNTIME_OK);
    CHECK(ledger.window_cycles[0] == 0U);
    CHECK(ledger.task_cycles == 40U);
}

static void CaseIdleInterruptedByIrq(void)
{
    FakePlatform platform = { 0U, 0U, 0U, 0U };
    R4_Clock64 clock;
    R4_RuntimeLedger ledger = NewLedger(&platform, &clock);

    /* [0,10) and [20,30) belong to idle; only [10,20) belongs to the IRQ.
     * This proves the two classifications are exclusive rather than merely
     * proving that the aggregate ledger happens to conserve elapsed time. */
    ledger.active.kind = R4_RUNTIME_CONTEXT_IDLE;
    ledger.active.identity = 0U;
    CHECK(Apply(&ledger, &platform, 10U, R4_RUNTIME_EVENT_IRQ_ENTER, 31U) ==
        R4_RUNTIME_OK);
    CHECK(Apply(&ledger, &platform, 20U, R4_RUNTIME_EVENT_IRQ_EXIT, 31U) ==
        R4_RUNTIME_OK);
    CHECK(Apply(&ledger, &platform, 30U, R4_RUNTIME_EVENT_CHECKPOINT, 0U) ==
        R4_RUNTIME_OK);
    CHECK(ledger.idle_cycles == 20U);
    CHECK(ledger.irq_cycles == 10U);
    CHECK(ledger.task_cycles == 0U);
    CHECK(BucketCycles(ledger.irq_buckets, R4_RUNTIME_MAX_IRQ_BUCKETS,
        31U) == 10U);
}

static void CaseTimeRegressionFaults(void)
{
    FakePlatform platform = { 0U, 0U, 0U, 0U };
    R4_Clock64 clock;
    R4_RuntimeLedger ledger = NewLedger(&platform, &clock);

    /* Model a corrupted/future ledger boundary.  Underflow is forbidden: the
     * formal measurement latches an error and all later events are rejected. */
    ledger.last_time = 100U;
    CHECK(Apply(&ledger, &platform, 50U, R4_RUNTIME_EVENT_CHECKPOINT, 0U) ==
        R4_RUNTIME_TIME_REGRESSION);
    CHECK(R4_RuntimeLedger_GetStatus(&ledger) == R4_RUNTIME_TIME_REGRESSION);
    CHECK(Apply(&ledger, &platform, 101U, R4_RUNTIME_EVENT_CHECKPOINT, 0U) ==
        R4_RUNTIME_TIME_REGRESSION);
}

static void CaseNestedIrq(void)
{
    FakePlatform platform = { 0U, 0U, 0U, 0U };
    R4_Clock64 clock;
    R4_RuntimeLedger ledger = NewLedger(&platform, &clock);

    CHECK(Apply(&ledger, &platform, 5U, R4_RUNTIME_EVENT_IRQ_ENTER, 17U) ==
        R4_RUNTIME_OK);
    CHECK(Apply(&ledger, &platform, 8U, R4_RUNTIME_EVENT_IRQ_ENTER, 23U) ==
        R4_RUNTIME_OK);
    CHECK(Apply(&ledger, &platform, 18U, R4_RUNTIME_EVENT_IRQ_EXIT, 23U) ==
        R4_RUNTIME_OK);
    CHECK(Apply(&ledger, &platform, 30U, R4_RUNTIME_EVENT_IRQ_EXIT, 17U) ==
        R4_RUNTIME_OK);
    CHECK(ledger.irq_cycles == 25U);
    CHECK(ledger.task_cycles == 5U);
    CHECK(BucketCycles(ledger.task_buckets, R4_RUNTIME_MAX_TASK_BUCKETS,
        11U) == 5U);
    /* Low IRQ owns 5--8 and 18--30 only.  The 8--18 high IRQ interval is
     * exclusive to IRQ 23 and cannot also be charged to the interrupted low
     * IRQ or task. */
    CHECK(BucketCycles(ledger.irq_buckets, R4_RUNTIME_MAX_IRQ_BUCKETS,
        17U) == 15U);
    CHECK(BucketCycles(ledger.irq_buckets, R4_RUNTIME_MAX_IRQ_BUCKETS,
        23U) == 10U);
    CHECK(ledger.irq_depth == 0U);
    CHECK(ledger.active.kind == R4_RUNTIME_CONTEXT_TASK);
    CHECK(ledger.active.identity == 11U);
}

static void CaseDuplicateExitFaults(void)
{
    FakePlatform platform = { 0U, 0U, 0U, 0U };
    R4_Clock64 clock;
    R4_RuntimeLedger ledger = NewLedger(&platform, &clock);

    CHECK(Apply(&ledger, &platform, 1U, R4_RUNTIME_EVENT_IRQ_ENTER, 7U) ==
        R4_RUNTIME_OK);
    CHECK(Apply(&ledger, &platform, 2U, R4_RUNTIME_EVENT_IRQ_EXIT, 7U) ==
        R4_RUNTIME_OK);
    CHECK(Apply(&ledger, &platform, 3U, R4_RUNTIME_EVENT_IRQ_EXIT, 7U) ==
        R4_RUNTIME_IRQ_EXIT_MISMATCH);
    CHECK(R4_RuntimeLedger_GetStatus(&ledger) == R4_RUNTIME_IRQ_EXIT_MISMATCH);
    CHECK(Apply(&ledger, &platform, 4U, R4_RUNTIME_EVENT_CHECKPOINT, 0U) ==
        R4_RUNTIME_IRQ_EXIT_MISMATCH);
}

static void CaseTaskSwitch(void)
{
    FakePlatform platform = { 0U, 0U, 0U, 0U };
    R4_Clock64 clock;
    R4_RuntimeLedger ledger = NewLedger(&platform, &clock);

    CHECK(Apply(&ledger, &platform, 4U, R4_RUNTIME_EVENT_TASK_SWITCHED_OUT, 11U) ==
        R4_RUNTIME_OK);
    CHECK(Apply(&ledger, &platform, 5U, R4_RUNTIME_EVENT_TASK_SWITCHED_IN, 19U) ==
        R4_RUNTIME_OK);
    CHECK(Apply(&ledger, &platform, 11U, R4_RUNTIME_EVENT_CHECKPOINT, 0U) ==
        R4_RUNTIME_OK);
    CHECK(ledger.task_cycles == 10U);
    CHECK(ledger.unclassified_cycles == 1U);
    CHECK(ledger.active.identity == 19U);
    CHECK(BucketCycles(ledger.task_buckets, R4_RUNTIME_MAX_TASK_BUCKETS,
        11U) == 4U);
    CHECK(BucketCycles(ledger.task_buckets, R4_RUNTIME_MAX_TASK_BUCKETS,
        19U) == 6U);
}

static void CaseLockedCheckpoint(void)
{
    FakePlatform platform = { 20U, 0U, 0U, 0U };
    R4_Clock64 clock;
    R4_RuntimeLedger ledger = NewLedger(&platform, &clock);
    R4_RuntimeEventReceipt receipt;

    CHECK(R4_RuntimeLedger_CheckpointLocked(&ledger, 33U, &receipt) ==
        R4_RUNTIME_OK);
    CHECK(receipt.time == 33U);
    CHECK(receipt.serial == 1U);
    CHECK(ledger.task_cycles == 13U);
    CHECK(platform.save_count == 0U);
    CHECK(platform.restore_count == 0U);
}

static void CaseLockedCheckpointTime(void)
{
    FakePlatform platform = { 20U, 0U, 0U, 0U };
    R4_Clock64 clock;
    R4_RuntimeLedger ledger = NewLedger(&platform, &clock);
    uint64_t committed_time = 0U;

    CHECK(R4_RuntimeLedger_CheckpointLockedTime(&ledger, 33U,
        &committed_time) == R4_RUNTIME_OK);
    CHECK(committed_time == 33U);
    CHECK(ledger.event_serial == 1U);
    CHECK(ledger.task_cycles == 13U);
    CHECK(platform.save_count == 0U);
    CHECK(platform.restore_count == 0U);
}

static void RunCase(const char *name)
{
    if (strcmp(name, "clock_wrap") == 0)
    {
        CaseClockWrap();
    }
    else if (strcmp(name, "clock_no_wrap_same_cycle") == 0)
    {
        CaseClockNoWrapAndSameCycle();
    }
    else if (strcmp(name, "clock_multiple_wraps") == 0)
    {
        CaseClockMultipleSyntheticWraps();
    }
    else if (strcmp(name, "atomic_window") == 0)
    {
        CaseAtomicWindow();
    }
    else if (strcmp(name, "masked_entry") == 0)
    {
        CaseMaskedEntryRestored();
    }
    else if (strcmp(name, "time_regression") == 0)
    {
        CaseTimeRegressionFaults();
    }
    else if (strcmp(name, "window_clipping") == 0)
    {
        CaseWindowClipping();
    }
    else if (strcmp(name, "window_outside") == 0)
    {
        CaseWindowEntirelyOutside();
    }
    else if (strcmp(name, "window_owner") == 0)
    {
        CaseWindowOwnerAttribution();
    }
    else if (strcmp(name, "idle_irq") == 0)
    {
        CaseIdleInterruptedByIrq();
    }
    else if (strcmp(name, "nested_irq") == 0)
    {
        CaseNestedIrq();
    }
    else if (strcmp(name, "duplicate_exit") == 0)
    {
        CaseDuplicateExitFaults();
    }
    else if (strcmp(name, "task_switch") == 0)
    {
        CaseTaskSwitch();
    }
    else if (strcmp(name, "locked_checkpoint") == 0)
    {
        CaseLockedCheckpoint();
    }
    else if (strcmp(name, "locked_checkpoint_time") == 0)
    {
        CaseLockedCheckpointTime();
    }
    else
    {
        (void)fprintf(stderr, "unknown case: %s\n", name);
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        (void)fprintf(stderr, "usage: %s CASE\n", argv[0]);
        return EXIT_FAILURE;
    }
    RunCase(argv[1]);
    return EXIT_SUCCESS;
}
