#include "r4_runtime_target.h"
#include "r4_tick_service_target.h"

#include "FreeRTOS.h"
#include "stm32f4xx.h"

#include "r3_w3_runtime.h"

static R4_Clock64 target_clock;
static R4_RuntimeLedger target_ledger;
static uint32_t target_initialized;
static R4_RuntimeStatus target_boot_error = R4_RUNTIME_NOT_INITIALIZED;
static R4_CompletionTimingSnapshot completion_timing;
static R4_DmaTailSnapshot dma_tail;
static R4_SysTickTraceSnapshot systick_trace;
static R4_DmaWindowSnapshot dma_window;
static R4_RuntimeHealthSnapshot runtime_health;
static uintptr_t target_idle_task;
#if defined(STREAM_LAB_R4_HW)
static uint32_t target_test_pend_irq_after_mask;
static uint32_t target_test_pend_high_from_low;
#endif

static uint32_t TargetReadCycle(void *context)
{
    (void)context;
    return DWT->CYCCNT;
}

static uint32_t TargetSaveAndDisable(void *context)
{
    uint32_t primask;

    (void)context;
    primask = __get_PRIMASK();
    __disable_irq();
    __DMB();
#if defined(STREAM_LAB_R4_HW)
    if (target_test_pend_irq_after_mask != 0U)
    {
        /* PRIMASK is already set.  TIM6 therefore becomes pending in the
         * RuntimeEvent transaction and cannot observe a partial ledger. */
        target_test_pend_irq_after_mask = 0U;
        NVIC_SetPendingIRQ(TIM6_DAC_IRQn);
    }
#endif
    return primask;
}

static void TargetRestore(uint32_t saved_mask, void *context)
{
    (void)context;
    __DMB();
    __set_PRIMASK(saved_mask);
}

/* A timing boundary only snapshots the private DWT-derived clock.  It neither
 * publishes queue state nor touches a device register, so the DMB pair used
 * by the RuntimeEvent transaction is unnecessary here.  Keeping the barriers
 * on the commit path preserves the accounting transaction's ordering while
 * making t_lock/t_unlock measure the queue critical section rather than the
 * measurement scaffold. */
static uint32_t TargetSaveAndDisableBoundary(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    return primask;
}

static void TargetRestoreBoundary(uint32_t saved_mask)
{
    __set_PRIMASK(saved_mask);
}

static R4_RuntimeStatus Apply(R4_RuntimeEventKind kind, uintptr_t identity)
{
    R4_RuntimeEvent event;
    R4_RuntimeEventReceipt receipt;

    if (target_initialized == 0U)
    {
        return target_boot_error;
    }

    event.kind = kind;
    event.identity = identity;
    return R4_RuntimeEvent_Apply(&target_ledger, &event, &receipt);
}

R4_RuntimeStatus R4_RuntimeTarget_Initialize(void)
{
    R4_RuntimePlatform platform;
    R4_RuntimeContext initial;
    uint32_t saved_mask;
    R4_Clock64Status clock_status;

    if (target_initialized != 0U)
    {
        return R4_RuntimeLedger_GetStatus(&target_ledger);
    }

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DMB();

    saved_mask = TargetSaveAndDisable(NULL);
    clock_status = R4_Clock64_InitializeLocked(&target_clock, DWT->CYCCNT);
    TargetRestore(saved_mask, NULL);
    if (clock_status != R4_CLOCK64_OK)
    {
        target_boot_error = R4_RUNTIME_CLOCK_ERROR;
        return target_boot_error;
    }

    platform.read_cycle = TargetReadCycle;
    platform.save_and_disable = TargetSaveAndDisable;
    platform.restore = TargetRestore;
    platform.context = NULL;
    initial.kind = R4_RUNTIME_CONTEXT_IDLE;
    initial.identity = 0U;
    target_boot_error = R4_RuntimeLedger_Initialize(
        &target_ledger, &target_clock, &platform, initial);
    if (target_boot_error == R4_RUNTIME_OK)
    {
        dma_tail.dma_irq_count = 0U;
        dma_tail.dma_yield_requested_count = 0U;
        dma_tail.dma_no_yield_count = 0U;
        systick_trace.enter_count = 0U;
        systick_trace.exit_count = 0U;
        dma_window.configured = 0U;
        dma_window.open_status = R4_RUNTIME_NOT_INITIALIZED;
        dma_window.close_status = R4_RUNTIME_NOT_INITIALIZED;
        dma_window.boundary_status = R4_RUNTIME_NOT_INITIALIZED;
        runtime_health.first_fault = R4_RUNTIME_INFRA_NONE;
        runtime_health.fail_closed_requested = 0U;
        runtime_health.monitor_service_count = 0U;
        runtime_health.last_monitor_cycle = 0U;
        runtime_health.max_monitor_interval_cycles = 0U;
        runtime_health.monitor_interval_limit_cycles = 0U;
        target_initialized = 1U;
        if (R4_TickServiceTarget_Initialize() != R4_TICK_SERVICE_OK)
        {
            target_initialized = 0U;
            target_boot_error = R4_RUNTIME_PLATFORM_ERROR;
        }
    }
    return target_boot_error;
}

void R4_RuntimeTarget_LatchInfrastructureFault(R4_RuntimeInfrastructureFault fault)
{
    uint32_t saved_mask;

    if ((target_initialized == 0U) || (fault == R4_RUNTIME_INFRA_NONE)) return;
    saved_mask = TargetSaveAndDisable(NULL);
    if (runtime_health.first_fault == R4_RUNTIME_INFRA_NONE)
    {
        runtime_health.first_fault = fault;
    }
    runtime_health.fail_closed_requested = 1U;
    /* This is the only R4-to-lifecycle ISR action.  It is bounded and closes
     * admission gates immediately; task-context code performs the actual
     * driver/worker stop after observing the sticky request. */
    (void)R3W3Runtime_RequestInfrastructureStopFromIsr();
    TargetRestore(saved_mask, NULL);
}

R4_RuntimeStatus R4_RuntimeTarget_MonitorService(uint64_t interval_limit_cycles)
{
    uint32_t saved_mask;
    uint64_t now;
    uint64_t interval = 0U;
    uint32_t limit_exceeded = 0U;

    if ((target_initialized == 0U) || (interval_limit_cycles == 0U))
    {
        return R4_RUNTIME_INVALID_ARGUMENT;
    }
    if (R4_RuntimeTarget_ReadNow(&now) != R4_RUNTIME_OK)
    {
        R4_RuntimeTarget_LatchInfrastructureFault(R4_RUNTIME_INFRA_RUNTIME_EVENT);
        return R4_RUNTIME_CLOCK_ERROR;
    }
    saved_mask = TargetSaveAndDisable(NULL);
    if (runtime_health.monitor_service_count != 0U)
    {
        interval = now - runtime_health.last_monitor_cycle;
        if (interval > runtime_health.max_monitor_interval_cycles)
        {
            runtime_health.max_monitor_interval_cycles = interval;
        }
        if (interval > interval_limit_cycles)
        {
            limit_exceeded = 1U;
        }
    }
    runtime_health.last_monitor_cycle = now;
    runtime_health.monitor_interval_limit_cycles = interval_limit_cycles;
    ++runtime_health.monitor_service_count;
    TargetRestore(saved_mask, NULL);
    if (limit_exceeded != 0U)
    {
        R4_RuntimeTarget_LatchInfrastructureFault(
            R4_RUNTIME_INFRA_CLOCK64_MONITOR_GAP);
    }
    return R4_RuntimeLedger_GetStatus(&target_ledger);
}

R4_RuntimeStatus R4_RuntimeTarget_GetHealthSnapshot(R4_RuntimeHealthSnapshot *out)
{
    uint32_t saved_mask;

    if (out == NULL) return R4_RUNTIME_INVALID_ARGUMENT;
    if (target_initialized == 0U) return target_boot_error;
    saved_mask = TargetSaveAndDisable(NULL);
    *out = runtime_health;
    TargetRestore(saved_mask, NULL);
    return R4_RuntimeLedger_GetStatus(&target_ledger);
}

R4_RuntimeStatus R4_RuntimeTarget_OpenWindow(uint32_t window)
{
    return Apply(R4_RUNTIME_EVENT_WINDOW_OPEN, (uintptr_t)window);
}

R4_RuntimeStatus R4_RuntimeTarget_CloseWindow(uint32_t window)
{
    return Apply(R4_RUNTIME_EVENT_WINDOW_CLOSE, (uintptr_t)window);
}

R4_RuntimeStatus R4_RuntimeTarget_ArmDmaWindow(uint32_t window,
    uint32_t open_sequence, uint32_t close_sequence)
{
    uint32_t saved_mask;

    if ((target_initialized == 0U) || (window >= R4_RUNTIME_WINDOW_COUNT) ||
        (open_sequence == 0U) || (close_sequence <= open_sequence))
    {
        return target_initialized != 0U ? R4_RUNTIME_INVALID_ARGUMENT :
            target_boot_error;
    }
    saved_mask = TargetSaveAndDisable(NULL);
    if (dma_window.configured != 0U)
    {
        TargetRestore(saved_mask, NULL);
        return R4_RUNTIME_WINDOW_ERROR;
    }
    dma_window.configured = 1U;
    dma_window.opened = 0U;
    dma_window.closed = 0U;
    dma_window.window = window;
    dma_window.open_sequence = open_sequence;
    dma_window.close_sequence = close_sequence;
    dma_window.last_sequence = 0U;
    dma_window.first_error_sequence = 0U;
    dma_window.open_status = R4_RUNTIME_NOT_INITIALIZED;
    dma_window.close_status = R4_RUNTIME_NOT_INITIALIZED;
    dma_window.boundary_status = R4_RUNTIME_OK;
    TargetRestore(saved_mask, NULL);
    return R4_RUNTIME_OK;
}

R4_RuntimeStatus R4_RuntimeTarget_OnDmaInputBoundary(uint32_t sequence)
{
    R4_RuntimeEventKind kind = R4_RUNTIME_EVENT_CHECKPOINT;
    uint32_t window = 0U;
    uint32_t action = 0U;
    uint32_t saved_mask;
    R4_RuntimeStatus status;

    if ((target_initialized == 0U) || (sequence == 0U))
    {
        return target_initialized != 0U ? R4_RUNTIME_INVALID_ARGUMENT :
            target_boot_error;
    }
    saved_mask = TargetSaveAndDisable(NULL);
    if (dma_window.configured == 0U)
    {
        TargetRestore(saved_mask, NULL);
        return R4_RUNTIME_OK;
    }
    dma_window.last_sequence = sequence;
    window = dma_window.window;
    if ((dma_window.opened == 0U) && (sequence == dma_window.open_sequence))
    {
        action = 1U;
        kind = R4_RUNTIME_EVENT_WINDOW_OPEN;
    }
    else if ((dma_window.opened != 0U) && (dma_window.closed == 0U) &&
             (sequence == dma_window.close_sequence))
    {
        action = 2U;
        kind = R4_RUNTIME_EVENT_WINDOW_CLOSE;
    }
    else if (((dma_window.opened == 0U) && (sequence > dma_window.open_sequence)) ||
             ((dma_window.closed == 0U) && (sequence > dma_window.close_sequence)))
    {
        dma_window.boundary_status = R4_RUNTIME_WINDOW_ERROR;
        if (dma_window.first_error_sequence == 0U)
        {
            dma_window.first_error_sequence = sequence;
        }
        TargetRestore(saved_mask, NULL);
        return R4_RUNTIME_WINDOW_ERROR;
    }
    TargetRestore(saved_mask, NULL);

    if (action == 0U)
    {
        return R4_RUNTIME_OK;
    }
    status = Apply(kind, (uintptr_t)window);
    saved_mask = TargetSaveAndDisable(NULL);
    if (action == 1U)
    {
        dma_window.open_status = status;
        if (status == R4_RUNTIME_OK) dma_window.opened = 1U;
    }
    else
    {
        dma_window.close_status = status;
        if (status == R4_RUNTIME_OK) dma_window.closed = 1U;
    }
    if (status != R4_RUNTIME_OK)
    {
        dma_window.boundary_status = status;
        if (dma_window.first_error_sequence == 0U)
        {
            dma_window.first_error_sequence = sequence;
        }
    }
    TargetRestore(saved_mask, NULL);
    return status;
}

R4_RuntimeStatus R4_RuntimeTarget_GetDmaWindowSnapshot(
    R4_DmaWindowSnapshot *out)
{
    uint32_t saved_mask;

    if (out == NULL) return R4_RUNTIME_INVALID_ARGUMENT;
    if (target_initialized == 0U) return target_boot_error;
    saved_mask = TargetSaveAndDisable(NULL);
    *out = dma_window;
    TargetRestore(saved_mask, NULL);
    return R4_RuntimeLedger_GetStatus(&target_ledger);
}

R4_RuntimeStatus R4_RuntimeTarget_Checkpoint(void)
{
    return Apply(R4_RUNTIME_EVENT_CHECKPOINT, 0U);
}

R4_RuntimeStatus R4_RuntimeTarget_ReadNow(uint64_t *out)
{
    uint32_t saved_mask;
    R4_Clock64Status status;

    if (out == NULL)
    {
        return R4_RUNTIME_INVALID_ARGUMENT;
    }
    if (target_initialized == 0U)
    {
        return target_boot_error;
    }
    saved_mask = TargetSaveAndDisable(NULL);
    status = R4_Clock64_ReadLocked(&target_clock, DWT->CYCCNT, out);
    TargetRestore(saved_mask, NULL);
    return status == R4_CLOCK64_OK ? R4_RUNTIME_OK : R4_RUNTIME_CLOCK_ERROR;
}

static uint64_t CompletionCommitNow(void)
{
    uint32_t saved_mask;
    uint64_t now;

    saved_mask = TargetSaveAndDisable(NULL);
    if (R4_RuntimeLedger_CheckpointLockedTime(&target_ledger, DWT->CYCCNT,
            &now) != R4_RUNTIME_OK)
    {
        TargetRestore(saved_mask, NULL);
        ++completion_timing.malformed_count;
        return 0U;
    }
    TargetRestore(saved_mask, NULL);
    return now;
}

/* t_lock/t_unlock are diagnostic boundaries, not ledger mutations.  They use
 * the same Clock64 extension but do not add two redundant RuntimeEvent
 * settlements to the bounded queue critical section.  t_commit remains the
 * one atomic accounting transaction mandated by the architecture. */
static uint64_t CompletionBoundaryNow(void)
{
    uint32_t saved_mask;
    uint64_t now;

    saved_mask = TargetSaveAndDisableBoundary();
    if (R4_Clock64_ReadLocked(&target_clock, DWT->CYCCNT, &now) != R4_CLOCK64_OK)
    {
        TargetRestoreBoundary(saved_mask);
        ++completion_timing.malformed_count;
        return 0U;
    }
    TargetRestoreBoundary(saved_mask);
    return now;
}

void R4_RuntimeTarget_CompletionLock(uint32_t operation)
{
    if (target_initialized == 0U)
    {
        return;
    }
    if (completion_timing.active != 0U)
    {
        ++completion_timing.malformed_count;
        return;
    }
    completion_timing.active = 1U;
    completion_timing.operation = operation;
    completion_timing.t_commit = 0U;
    completion_timing.t_unlock = 0U;
    completion_timing.t_lock = CompletionBoundaryNow();
    ++completion_timing.lock_count;
}

void R4_RuntimeTarget_CompletionCommit(uint32_t operation)
{
    if ((target_initialized == 0U) || (completion_timing.active == 0U) ||
        (completion_timing.operation != operation) ||
        (completion_timing.t_commit != 0U))
    {
        ++completion_timing.malformed_count;
        return;
    }
    completion_timing.t_commit = CompletionCommitNow();
    ++completion_timing.commit_count;
}

void R4_RuntimeTarget_CompletionUnlock(uint32_t operation)
{
    uint64_t now;

    if ((target_initialized == 0U) || (completion_timing.active == 0U) ||
        (completion_timing.operation != operation) ||
        (completion_timing.t_lock == 0U))
    {
        ++completion_timing.malformed_count;
        completion_timing.active = 0U;
        return;
    }
    if (completion_timing.t_commit == 0U)
    {
        /* An aborted attempt never reached semantic completion, so it is not
         * a timing sample.  Keep it visible without forging a paired event. */
        ++completion_timing.discarded_count;
        completion_timing.active = 0U;
        return;
    }
    now = CompletionBoundaryNow();
    ++completion_timing.unlock_count;
    if (now == 0U)
    {
        completion_timing.active = 0U;
        return;
    }
    completion_timing.t_unlock = now;
    if ((completion_timing.t_unlock < completion_timing.t_commit) ||
        (completion_timing.t_commit < completion_timing.t_lock))
    {
        ++completion_timing.malformed_count;
    }
    else
    {
        uint64_t prefix = completion_timing.t_commit - completion_timing.t_lock;
        uint64_t suffix = completion_timing.t_unlock - completion_timing.t_commit;
        uint64_t total = completion_timing.t_unlock - completion_timing.t_lock;
        if (prefix > completion_timing.max_prefix_cycles) completion_timing.max_prefix_cycles = prefix;
        if (suffix > completion_timing.max_suffix_cycles) completion_timing.max_suffix_cycles = suffix;
        if (total > completion_timing.max_total_cycles) completion_timing.max_total_cycles = total;
        ++completion_timing.completed_count;
    }
    completion_timing.active = 0U;
}

R4_RuntimeStatus R4_RuntimeTarget_GetCompletionTiming(
    R4_CompletionTimingSnapshot *out)
{
    if (out == NULL)
    {
        return R4_RUNTIME_INVALID_ARGUMENT;
    }
    *out = completion_timing;
    return target_initialized != 0U ? R4_RuntimeLedger_GetStatus(&target_ledger) :
        target_boot_error;
}

void R4_RuntimeTarget_TraceIsrEnter(void)
{
    const uint32_t irq_id = __get_IPSR();

    if (irq_id == 15U)
    {
        ++systick_trace.enter_count;
    }
    (void)Apply(R4_RUNTIME_EVENT_IRQ_ENTER, (uintptr_t)irq_id);
}

void R4_RuntimeTarget_TraceIsrExit(void)
{
    const uint32_t irq_id = __get_IPSR();

    if (irq_id == 15U)
    {
        ++systick_trace.exit_count;
    }
    (void)Apply(R4_RUNTIME_EVENT_IRQ_EXIT, (uintptr_t)irq_id);
}

R4_RuntimeStatus R4_RuntimeTarget_GetSysTickTraceSnapshot(
    R4_SysTickTraceSnapshot *out)
{
    uint32_t saved_mask;

    if (out == NULL)
    {
        return R4_RUNTIME_INVALID_ARGUMENT;
    }
    saved_mask = TargetSaveAndDisableBoundary();
    *out = systick_trace;
    TargetRestoreBoundary(saved_mask);
    return target_initialized != 0U ? R4_RuntimeLedger_GetStatus(&target_ledger) :
        target_boot_error;
}

void R4_RuntimeTarget_BindIdleTask(void *task)
{
    target_idle_task = (uintptr_t)task;
}

void R4_RuntimeTarget_TraceTaskSwitchedOut(void *task)
{
    if ((target_idle_task != 0U) && ((uintptr_t)task == target_idle_task))
    {
        (void)Apply(R4_RUNTIME_EVENT_IDLE_SWITCHED_OUT, 0U);
    }
    else
    {
        (void)Apply(R4_RUNTIME_EVENT_TASK_SWITCHED_OUT, (uintptr_t)task);
    }
}

void R4_RuntimeTarget_TraceTaskSwitchedIn(void *task)
{
    if ((target_idle_task != 0U) && ((uintptr_t)task == target_idle_task))
    {
        (void)Apply(R4_RUNTIME_EVENT_IDLE_SWITCHED_IN, 0U);
    }
    else
    {
        (void)Apply(R4_RUNTIME_EVENT_TASK_SWITCHED_IN, (uintptr_t)task);
    }
}

void R4_RuntimeTarget_TraceDmaTailYield(uint32_t higher_priority_task_woken)
{
    if (target_initialized == 0U) return;
    ++dma_tail.dma_irq_count;
    if (higher_priority_task_woken != 0U)
    {
        ++dma_tail.dma_yield_requested_count;
    }
    else
    {
        ++dma_tail.dma_no_yield_count;
    }
}

R4_RuntimeStatus R4_RuntimeTarget_GetDmaTailSnapshot(R4_DmaTailSnapshot *out)
{
    uint32_t saved_mask;
    if (out == NULL) return R4_RUNTIME_INVALID_ARGUMENT;
    if (target_initialized == 0U) return target_boot_error;
    saved_mask = TargetSaveAndDisable(NULL);
    *out = dma_tail;
    TargetRestore(saved_mask, NULL);
    return R4_RuntimeLedger_GetStatus(&target_ledger);
}

const R4_RuntimeLedger *R4_RuntimeTarget_GetLedger(void)
{
    return target_initialized != 0U ? &target_ledger : NULL;
}

#if defined(STREAM_LAB_R4_HW)
R4_RuntimeStatus R4_RuntimeTarget_TestArmPendingIrq(void)
{
    if (target_initialized == 0U)
    {
        return target_boot_error;
    }
    /* Priority 5 is above SysTick (15), but the injected edge is held by the
     * RuntimeEvent's PRIMASK transaction, which is precisely T17's case. */
    NVIC_ClearPendingIRQ(TIM6_DAC_IRQn);
    NVIC_SetPriority(TIM6_DAC_IRQn, 5U);
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
    target_test_pend_irq_after_mask = 1U;
    return R4_RUNTIME_OK;
}

R4_RuntimeStatus R4_RuntimeTarget_TestArmNestedIrq(void)
{
    if (target_initialized == 0U)
    {
        return target_boot_error;
    }
    NVIC_ClearPendingIRQ(TIM7_IRQn);
    NVIC_ClearPendingIRQ(TIM6_DAC_IRQn);
    NVIC_SetPriority(TIM7_IRQn, 10U);
    NVIC_SetPriority(TIM6_DAC_IRQn, 5U);
    NVIC_EnableIRQ(TIM7_IRQn);
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
    target_test_pend_high_from_low = 1U;
    NVIC_SetPendingIRQ(TIM7_IRQn);
    return R4_RUNTIME_OK;
}

void R4_RuntimeTarget_TestPendHighFromLowIrq(void)
{
    if (target_test_pend_high_from_low != 0U)
    {
        target_test_pend_high_from_low = 0U;
        NVIC_SetPendingIRQ(TIM6_DAC_IRQn);
    }
}

R4_RuntimeStatus R4_RuntimeTarget_TestInjectDuplicateExit(uint32_t irq_id)
{
    /* This intentionally illegal second exit follows the real TIM6 exit.
     * RuntimeEvent must latch it; it is never enabled in a normal image. */
    return Apply(R4_RUNTIME_EVENT_IRQ_EXIT, (uintptr_t)irq_id);
}

R4_RuntimeStatus R4_RuntimeTarget_TestInjectTimeRegression(void)
{
    uint32_t saved_mask;

    if (target_initialized == 0U)
    {
        return target_boot_error;
    }
    /* This is deliberately a ledger-corruption test actuator, excluded from
     * every production profile.  It creates a future boundary without
     * touching the sole Clock64 authority or DWT hardware. */
    saved_mask = TargetSaveAndDisable(NULL);
    target_ledger.last_time = target_clock.last_time + UINT64_C(0x100000);
    TargetRestore(saved_mask, NULL);
    return Apply(R4_RUNTIME_EVENT_CHECKPOINT, 0U);
}

R4_RuntimeStatus R4_RuntimeTarget_TestApplyEvent(R4_RuntimeEventKind kind,
    uintptr_t identity)
{
    return Apply(kind, identity);
}
#endif
