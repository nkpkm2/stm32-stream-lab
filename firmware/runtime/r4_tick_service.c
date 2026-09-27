#include "r4_tick_service.h"

#include <stddef.h>

static R4_TickServiceStatus Latch(R4_TickService *service,
    R4_TickServiceStatus status)
{
    if (service->first_error == R4_TICK_SERVICE_OK)
    {
        service->first_error = status;
    }
    return service->first_error;
}

R4_TickServiceStatus R4_TickService_Initialize(
    R4_TickService *service,
    const R4_TickServiceCallbacks *callbacks)
{
    if ((service == NULL) || (callbacks == NULL) || (callbacks->now == NULL) ||
        (callbacks->commit_start == NULL) || (callbacks->release_job == NULL))
    {
        return R4_TICK_SERVICE_INVALID_ARGUMENT;
    }
    service->initialized = 1U;
    service->start_pending = 0U;
    service->start_committed = 0U;
    service->releases_enabled = 0U;
    service->job_occupied = 0U;
    service->first_error = R4_TICK_SERVICE_OK;
    service->service_seq = 0U;
    service->q0 = 0U;
    service->period_ticks = 0U;
    service->next_release_seq = 0U;
    service->start_count = 0U;
    service->release_count = 0U;
    service->skipped_count = 0U;
    service->last_release_cycle = 0U;
    service->callbacks = *callbacks;
    return R4_TICK_SERVICE_OK;
}

R4_TickServiceStatus R4_TickService_ArmStart(
    R4_TickService *service,
    uint64_t q0,
    uint64_t period_ticks)
{
    if ((service == NULL) || (service->initialized == 0U) ||
        (period_ticks == 0U))
    {
        return R4_TICK_SERVICE_INVALID_ARGUMENT;
    }
    if ((service->first_error != R4_TICK_SERVICE_OK) ||
        (service->start_pending != 0U) || (service->start_committed != 0U) ||
        (q0 < (service->service_seq + UINT64_C(2))))
    {
        return R4_TICK_SERVICE_INVALID_STATE;
    }
    service->q0 = q0;
    service->period_ticks = period_ticks;
    service->next_release_seq = q0;
    service->start_pending = 1U;
    return R4_TICK_SERVICE_OK;
}

R4_TickServiceStatus R4_TickService_CancelStart(R4_TickService *service)
{
    if ((service == NULL) || (service->initialized == 0U))
    {
        return R4_TICK_SERVICE_INVALID_ARGUMENT;
    }
    if (service->start_pending == 0U)
    {
        return R4_TICK_SERVICE_INVALID_STATE;
    }
    service->start_pending = 0U;
    return R4_TICK_SERVICE_OK;
}

R4_TickServiceStatus R4_TickService_CompleteJob(R4_TickService *service)
{
    if ((service == NULL) || (service->initialized == 0U))
    {
        return R4_TICK_SERVICE_INVALID_ARGUMENT;
    }
    if (service->job_occupied == 0U)
    {
        return R4_TICK_SERVICE_INVALID_STATE;
    }
    service->job_occupied = 0U;
    return R4_TICK_SERVICE_OK;
}

R4_TickServiceStatus R4_TickService_OnService(R4_TickService *service)
{
    uint64_t now;

    if ((service == NULL) || (service->initialized == 0U))
    {
        return R4_TICK_SERVICE_INVALID_ARGUMENT;
    }
    if (service->first_error != R4_TICK_SERVICE_OK)
    {
        return service->first_error;
    }
    ++service->service_seq;

    if ((service->start_pending != 0U) && (service->service_seq == service->q0))
    {
        if (service->callbacks.commit_start(service->service_seq,
                service->callbacks.context) == 0)
        {
            return Latch(service, R4_TICK_SERVICE_START_FAILED);
        }
        service->start_pending = 0U;
        service->start_committed = 1U;
        service->releases_enabled = 1U;
        ++service->start_count;
    }
    else if ((service->start_pending != 0U) && (service->service_seq > service->q0))
    {
        return Latch(service, R4_TICK_SERVICE_START_FAILED);
    }

    if ((service->releases_enabled != 0U) &&
        (service->service_seq == service->next_release_seq))
    {
        service->next_release_seq += service->period_ticks;
        if (service->job_occupied != 0U)
        {
            ++service->skipped_count;
        }
        else
        {
            now = service->callbacks.now(service->callbacks.context);
            if (service->callbacks.release_job(service->service_seq, now,
                    service->callbacks.context) == 0)
            {
                return Latch(service, R4_TICK_SERVICE_CALLBACK_FAILED);
            }
            service->job_occupied = 1U;
            service->last_release_cycle = now;
            ++service->release_count;
        }
    }
    return R4_TICK_SERVICE_OK;
}
