#include "r4_runtime_event.h"

#include <stddef.h>

static R4_RuntimeStatus Latch(R4_RuntimeLedger *ledger, R4_RuntimeStatus status)
{
    if (ledger->faulted == 0U)
    {
        ledger->faulted = 1U;
        ledger->first_error = status;
    }
    return ledger->first_error;
}

static R4_RuntimeStatus AccountOwner(R4_RuntimeOwnerBucket *buckets,
    uint32_t capacity, uintptr_t identity, uint64_t elapsed)
{
    uint32_t index;
    R4_RuntimeOwnerBucket *free_bucket = NULL;

    if (identity == 0U)
    {
        return R4_RUNTIME_OWNER_CAPACITY_EXCEEDED;
    }
    for (index = 0U; index < capacity; ++index)
    {
        if (buckets[index].identity == identity)
        {
            buckets[index].cycles += elapsed;
            return R4_RUNTIME_OK;
        }
        if ((free_bucket == NULL) && (buckets[index].identity == 0U))
        {
            free_bucket = &buckets[index];
        }
    }
    if (free_bucket == NULL)
    {
        return R4_RUNTIME_OWNER_CAPACITY_EXCEEDED;
    }
    free_bucket->identity = identity;
    free_bucket->cycles = elapsed;
    return R4_RUNTIME_OK;
}

static R4_RuntimeStatus Settle(R4_RuntimeLedger *ledger, uint64_t now)
{
    uint64_t elapsed = now - ledger->last_time;
    uint32_t index;
    R4_RuntimeStatus status = R4_RUNTIME_OK;

    if (ledger->active.kind == R4_RUNTIME_CONTEXT_IRQ)
    {
        ledger->irq_cycles += elapsed;
        status = AccountOwner(ledger->irq_buckets, R4_RUNTIME_MAX_IRQ_BUCKETS,
            ledger->active.identity, elapsed);
    }
    else if (ledger->active.kind == R4_RUNTIME_CONTEXT_TASK)
    {
        ledger->task_cycles += elapsed;
        status = AccountOwner(ledger->task_buckets, R4_RUNTIME_MAX_TASK_BUCKETS,
            ledger->active.identity, elapsed);
    }
    else if (ledger->active.kind == R4_RUNTIME_CONTEXT_IDLE)
    {
        ledger->idle_cycles += elapsed;
    }
    else
    {
        ledger->unclassified_cycles += elapsed;
    }

    /* Do not let a later window update overwrite a live-owner capacity
     * failure.  The caller latches this status and invalidates the run. */
    if (status != R4_RUNTIME_OK)
    {
        ledger->last_time = now;
        return status;
    }

    for (index = 0U; index < R4_RUNTIME_WINDOW_COUNT; ++index)
    {
        if (ledger->window_open[index] != 0U)
        {
            ledger->window_cycles[index] += elapsed;
            if (ledger->active.kind == R4_RUNTIME_CONTEXT_IRQ)
            {
                ledger->window_irq_cycles[index] += elapsed;
                status = AccountOwner(ledger->window_irq_buckets[index],
                    R4_RUNTIME_MAX_IRQ_BUCKETS, ledger->active.identity, elapsed);
            }
            else if (ledger->active.kind == R4_RUNTIME_CONTEXT_TASK)
            {
                ledger->window_task_cycles[index] += elapsed;
                status = AccountOwner(ledger->window_task_buckets[index],
                    R4_RUNTIME_MAX_TASK_BUCKETS, ledger->active.identity, elapsed);
            }
            else if (ledger->active.kind == R4_RUNTIME_CONTEXT_IDLE)
            {
                ledger->window_idle_cycles[index] += elapsed;
            }
            else
            {
                ledger->window_unclassified_cycles[index] += elapsed;
            }
            if (status != R4_RUNTIME_OK)
            {
                break;
            }
        }
    }
    ledger->last_time = now;
    return status;
}

R4_RuntimeStatus R4_RuntimeLedger_Initialize(
    R4_RuntimeLedger *ledger,
    R4_Clock64 *clock,
    const R4_RuntimePlatform *platform,
    R4_RuntimeContext initial_context)
{
    uint32_t index;
    uint32_t owner_index;

    if ((ledger == NULL) || (clock == NULL) || (platform == NULL) ||
        (platform->read_cycle == NULL) || (platform->save_and_disable == NULL) ||
        (platform->restore == NULL))
    {
        return R4_RUNTIME_INVALID_ARGUMENT;
    }
    if (R4_Clock64_GetStatus(clock) != R4_CLOCK64_OK)
    {
        return R4_RUNTIME_CLOCK_ERROR;
    }

    ledger->initialized = 1U;
    ledger->faulted = 0U;
    ledger->first_error = R4_RUNTIME_OK;
    ledger->clock = clock;
    ledger->platform = *platform;
    ledger->last_time = clock->last_time;
    ledger->event_serial = 0U;
    ledger->active = initial_context;
    ledger->irq_depth = 0U;
    ledger->task_cycles = 0U;
    ledger->irq_cycles = 0U;
    ledger->idle_cycles = 0U;
    ledger->unclassified_cycles = 0U;
    for (index = 0U; index < R4_RUNTIME_WINDOW_COUNT; ++index)
    {
        ledger->window_open[index] = 0U;
        ledger->window_cycles[index] = 0U;
        ledger->window_task_cycles[index] = 0U;
        ledger->window_irq_cycles[index] = 0U;
        ledger->window_idle_cycles[index] = 0U;
        ledger->window_unclassified_cycles[index] = 0U;
        for (owner_index = 0U; owner_index < R4_RUNTIME_MAX_TASK_BUCKETS;
            ++owner_index)
        {
            ledger->window_task_buckets[index][owner_index].identity = 0U;
            ledger->window_task_buckets[index][owner_index].cycles = 0U;
        }
        for (owner_index = 0U; owner_index < R4_RUNTIME_MAX_IRQ_BUCKETS;
            ++owner_index)
        {
            ledger->window_irq_buckets[index][owner_index].identity = 0U;
            ledger->window_irq_buckets[index][owner_index].cycles = 0U;
        }
    }
    for (index = 0U; index < R4_RUNTIME_MAX_TASK_BUCKETS; ++index)
    {
        ledger->task_buckets[index].identity = 0U;
        ledger->task_buckets[index].cycles = 0U;
    }
    for (index = 0U; index < R4_RUNTIME_MAX_IRQ_BUCKETS; ++index)
    {
        ledger->irq_buckets[index].identity = 0U;
        ledger->irq_buckets[index].cycles = 0U;
    }
    return R4_RUNTIME_OK;
}

R4_RuntimeStatus R4_RuntimeEvent_Apply(
    R4_RuntimeLedger *ledger,
    const R4_RuntimeEvent *event,
    R4_RuntimeEventReceipt *out_receipt)
{
    uint32_t saved_mask;
    uint64_t now;
    R4_Clock64Status clock_status;
    R4_RuntimeStatus status = R4_RUNTIME_OK;

    if ((ledger == NULL) || (event == NULL) || (out_receipt == NULL))
    {
        return R4_RUNTIME_INVALID_ARGUMENT;
    }
    if (ledger->initialized == 0U)
    {
        return R4_RUNTIME_NOT_INITIALIZED;
    }
    if (ledger->faulted != 0U)
    {
        return ledger->first_error;
    }

    saved_mask = ledger->platform.save_and_disable(ledger->platform.context);
    clock_status = R4_Clock64_ReadLocked(
        ledger->clock, ledger->platform.read_cycle(ledger->platform.context), &now);
    if (clock_status != R4_CLOCK64_OK)
    {
        status = Latch(ledger, R4_RUNTIME_CLOCK_ERROR);
    }
    else if (now < ledger->last_time)
    {
        status = Latch(ledger, R4_RUNTIME_TIME_REGRESSION);
    }
    else
    {
        status = Settle(ledger, now);
        if (status != R4_RUNTIME_OK)
        {
            status = Latch(ledger, status);
        }
        else switch (event->kind)
        {
            case R4_RUNTIME_EVENT_IRQ_ENTER:
                if (ledger->irq_depth >= R4_RUNTIME_MAX_IRQ_NESTING)
                {
                    status = Latch(ledger, R4_RUNTIME_IRQ_NESTING_OVERFLOW);
                }
                else
                {
                    ledger->irq_stack[ledger->irq_depth].irq_id = (uint32_t)event->identity;
                    ledger->irq_stack[ledger->irq_depth].interrupted = ledger->active;
                    ++ledger->irq_depth;
                    ledger->active.kind = R4_RUNTIME_CONTEXT_IRQ;
                    ledger->active.identity = event->identity;
                }
                break;

            case R4_RUNTIME_EVENT_IRQ_EXIT:
                if ((ledger->irq_depth == 0U) ||
                    (ledger->irq_stack[ledger->irq_depth - 1U].irq_id !=
                     (uint32_t)event->identity))
                {
                    status = Latch(ledger, R4_RUNTIME_IRQ_EXIT_MISMATCH);
                }
                else
                {
                    --ledger->irq_depth;
                    ledger->active = ledger->irq_stack[ledger->irq_depth].interrupted;
                }
                break;

            case R4_RUNTIME_EVENT_TASK_SWITCHED_OUT:
                if ((ledger->irq_depth != 0U) ||
                    (ledger->active.kind != R4_RUNTIME_CONTEXT_TASK) ||
                    (ledger->active.identity != event->identity))
                {
                    status = Latch(ledger, R4_RUNTIME_TASK_SWITCH_MISMATCH);
                }
                else
                {
                    ledger->active.kind = R4_RUNTIME_CONTEXT_NONE;
                    ledger->active.identity = 0U;
                }
                break;

            case R4_RUNTIME_EVENT_TASK_SWITCHED_IN:
                if (ledger->irq_depth != 0U)
                {
                    status = Latch(ledger, R4_RUNTIME_TASK_SWITCH_MISMATCH);
                }
                else
                {
                    ledger->active.kind = R4_RUNTIME_CONTEXT_TASK;
                    ledger->active.identity = event->identity;
                }
                break;

            case R4_RUNTIME_EVENT_WINDOW_OPEN:
            case R4_RUNTIME_EVENT_WINDOW_CLOSE:
                if (event->identity >= R4_RUNTIME_WINDOW_COUNT)
                {
                    status = Latch(ledger, R4_RUNTIME_WINDOW_ERROR);
                }
                else if (event->kind == R4_RUNTIME_EVENT_WINDOW_OPEN)
                {
                    if (ledger->window_open[event->identity] != 0U)
                    {
                        status = Latch(ledger, R4_RUNTIME_WINDOW_ERROR);
                    }
                    else
                    {
                        ledger->window_open[event->identity] = 1U;
                    }
                }
                else if (ledger->window_open[event->identity] == 0U)
                {
                    status = Latch(ledger, R4_RUNTIME_WINDOW_ERROR);
                }
                else
                {
                    ledger->window_open[event->identity] = 0U;
                }
                break;

            case R4_RUNTIME_EVENT_CHECKPOINT:
                break;

            default:
                status = Latch(ledger, R4_RUNTIME_INVALID_ARGUMENT);
                break;
        }
    }

    if (status == R4_RUNTIME_OK)
    {
        ++ledger->event_serial;
    }
    out_receipt->time = ledger->last_time;
    out_receipt->serial = ledger->event_serial;
    out_receipt->status = status;
    ledger->platform.restore(saved_mask, ledger->platform.context);
    return status;
}

R4_RuntimeStatus R4_RuntimeLedger_CheckpointLocked(
    R4_RuntimeLedger *ledger,
    uint32_t raw_cycle,
    R4_RuntimeEventReceipt *out_receipt)
{
    uint64_t now;
    R4_Clock64Status clock_status;

    if ((ledger == NULL) || (out_receipt == NULL))
    {
        return R4_RUNTIME_INVALID_ARGUMENT;
    }
    if (ledger->initialized == 0U)
    {
        return R4_RUNTIME_NOT_INITIALIZED;
    }
    if (ledger->faulted != 0U)
    {
        return ledger->first_error;
    }
    clock_status = R4_Clock64_ReadLocked(ledger->clock, raw_cycle, &now);
    if (clock_status != R4_CLOCK64_OK)
    {
        out_receipt->status = Latch(ledger, R4_RUNTIME_CLOCK_ERROR);
    }
    else if (now < ledger->last_time)
    {
        out_receipt->status = Latch(ledger, R4_RUNTIME_TIME_REGRESSION);
    }
    else
    {
        out_receipt->status = Settle(ledger, now);
        if (out_receipt->status != R4_RUNTIME_OK)
        {
            out_receipt->status = Latch(ledger, out_receipt->status);
        }
        else
        {
            ++ledger->event_serial;
        }
    }
    out_receipt->time = ledger->last_time;
    out_receipt->serial = ledger->event_serial;
    return out_receipt->status;
}

R4_RuntimeStatus R4_RuntimeLedger_CheckpointLockedTime(
    R4_RuntimeLedger *ledger,
    uint32_t raw_cycle,
    uint64_t *out_time)
{
    uint64_t now;
    R4_Clock64Status clock_status;

    if ((ledger == NULL) || (out_time == NULL))
    {
        return R4_RUNTIME_INVALID_ARGUMENT;
    }
    if (ledger->initialized == 0U)
    {
        return R4_RUNTIME_NOT_INITIALIZED;
    }
    if (ledger->faulted != 0U)
    {
        return ledger->first_error;
    }
    clock_status = R4_Clock64_ReadLocked(ledger->clock, raw_cycle, &now);
    if (clock_status != R4_CLOCK64_OK)
    {
        return Latch(ledger, R4_RUNTIME_CLOCK_ERROR);
    }
    if (now < ledger->last_time)
    {
        return Latch(ledger, R4_RUNTIME_TIME_REGRESSION);
    }
    if (Settle(ledger, now) != R4_RUNTIME_OK)
    {
        return Latch(ledger, R4_RUNTIME_OWNER_CAPACITY_EXCEEDED);
    }
    ++ledger->event_serial;
    *out_time = now;
    return R4_RUNTIME_OK;
}

R4_RuntimeStatus R4_RuntimeLedger_GetStatus(const R4_RuntimeLedger *ledger)
{
    if (ledger == NULL)
    {
        return R4_RUNTIME_INVALID_ARGUMENT;
    }
    if (ledger->initialized == 0U)
    {
        return R4_RUNTIME_NOT_INITIALIZED;
    }
    return ledger->faulted != 0U ? ledger->first_error : R4_RUNTIME_OK;
}
