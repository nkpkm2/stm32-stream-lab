#include "r4_tick_service_target.h"

#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "r4_runtime_target.h"

static R4_TickService target_service;
static R4_TickServiceTargetCallbacks target_callbacks;
static uint32_t target_initialized;

static uint64_t TargetNow(void *context)
{
    uint64_t now = 0U;

    (void)context;
    (void)R4_RuntimeTarget_ReadNow(&now);
    return now;
}

static int TargetCommitStart(uint64_t service_seq, void *context)
{
    (void)context;
    return (target_callbacks.commit_start != NULL) ?
        target_callbacks.commit_start(service_seq, target_callbacks.context) : 0;
}

static int TargetReleaseJob(uint64_t planned_service_seq,
    uint64_t actual_cycle, void *context)
{
    (void)context;
    return (target_callbacks.release_job != NULL) ?
        target_callbacks.release_job(planned_service_seq, actual_cycle,
            target_callbacks.context) : 0;
}

R4_TickServiceStatus R4_TickServiceTarget_Initialize(void)
{
    R4_TickServiceCallbacks callbacks;

    if (target_initialized != 0U)
    {
        return target_service.first_error;
    }
    callbacks.now = TargetNow;
    callbacks.commit_start = TargetCommitStart;
    callbacks.release_job = TargetReleaseJob;
    callbacks.context = NULL;
    (void)memset(&target_callbacks, 0, sizeof(target_callbacks));
    if (R4_TickService_Initialize(&target_service, &callbacks) !=
        R4_TICK_SERVICE_OK)
    {
        return R4_TICK_SERVICE_INVALID_STATE;
    }
    target_initialized = 1U;
    return R4_TICK_SERVICE_OK;
}

R4_TickServiceStatus R4_TickServiceTarget_Register(
    const R4_TickServiceTargetCallbacks *callbacks)
{
    R4_TickServiceStatus status;

    if ((target_initialized == 0U) || (callbacks == NULL) ||
        (callbacks->commit_start == NULL) || (callbacks->release_job == NULL))
    {
        return R4_TICK_SERVICE_INVALID_ARGUMENT;
    }
    taskENTER_CRITICAL();
    if ((target_service.start_pending != 0U) ||
        (target_service.start_committed != 0U))
    {
        status = R4_TICK_SERVICE_INVALID_STATE;
    }
    else
    {
        target_callbacks = *callbacks;
        status = R4_TICK_SERVICE_OK;
    }
    taskEXIT_CRITICAL();
    return status;
}

R4_TickServiceStatus R4_TickServiceTarget_ArmStart(uint64_t q0,
    uint64_t period_ticks)
{
    R4_TickServiceStatus status;

    if (target_initialized == 0U)
    {
        return R4_TICK_SERVICE_INVALID_STATE;
    }
    taskENTER_CRITICAL();
    status = ((target_callbacks.commit_start == NULL) ||
              (target_callbacks.release_job == NULL)) ?
        R4_TICK_SERVICE_INVALID_STATE :
        R4_TickService_ArmStart(&target_service, q0, period_ticks);
    taskEXIT_CRITICAL();
    return status;
}

R4_TickServiceStatus R4_TickServiceTarget_CancelStart(void)
{
    R4_TickServiceStatus status;

    if (target_initialized == 0U) return R4_TICK_SERVICE_INVALID_STATE;
    taskENTER_CRITICAL();
    status = R4_TickService_CancelStart(&target_service);
    taskEXIT_CRITICAL();
    return status;
}

R4_TickServiceStatus R4_TickServiceTarget_CompleteJob(void)
{
    R4_TickServiceStatus status;

    if (target_initialized == 0U) return R4_TICK_SERVICE_INVALID_STATE;
    taskENTER_CRITICAL();
    status = R4_TickService_CompleteJob(&target_service);
    taskEXIT_CRITICAL();
    return status;
}

R4_TickServiceStatus R4_TickServiceTarget_OnTickHook(void)
{
    if (target_initialized == 0U) return R4_TICK_SERVICE_INVALID_STATE;
    return R4_TickService_OnService(&target_service);
}

R4_TickServiceStatus R4_TickServiceTarget_GetSnapshot(R4_TickService *out)
{
    if ((target_initialized == 0U) || (out == NULL))
    {
        return R4_TICK_SERVICE_INVALID_ARGUMENT;
    }
    taskENTER_CRITICAL();
    *out = target_service;
    taskEXIT_CRITICAL();
    return target_service.first_error;
}
