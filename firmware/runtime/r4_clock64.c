#include "r4_clock64.h"

#include <stddef.h>

R4_Clock64Status R4_Clock64_InitializeLocked(R4_Clock64 *clock, uint32_t raw)
{
    if (clock == NULL)
    {
        return R4_CLOCK64_INVALID_ARGUMENT;
    }

    clock->initialized = 1U;
    clock->last_raw = raw;
    clock->high_word = 0U;
    clock->last_time = (uint64_t)raw;
    clock->read_count = 1U;
    clock->wrap_count = 0U;
    clock->first_error = R4_CLOCK64_OK;
    return R4_CLOCK64_OK;
}

R4_Clock64Status R4_Clock64_ReadLocked(
    R4_Clock64 *clock,
    uint32_t raw,
    uint64_t *out_time)
{
    uint64_t extended;

    if ((clock == NULL) || (out_time == NULL))
    {
        return R4_CLOCK64_INVALID_ARGUMENT;
    }
    if (clock->initialized == 0U)
    {
        return R4_CLOCK64_NOT_INITIALIZED;
    }
    if (clock->first_error != R4_CLOCK64_OK)
    {
        return clock->first_error;
    }

    if (raw < clock->last_raw)
    {
        ++clock->high_word;
        ++clock->wrap_count;
    }

    extended = ((uint64_t)clock->high_word << 32U) | (uint64_t)raw;
    if (extended < clock->last_time)
    {
        clock->first_error = R4_CLOCK64_TIME_REGRESSION;
        return clock->first_error;
    }

    clock->last_raw = raw;
    clock->last_time = extended;
    ++clock->read_count;
    *out_time = extended;
    return R4_CLOCK64_OK;
}

R4_Clock64Status R4_Clock64_GetStatus(const R4_Clock64 *clock)
{
    if (clock == NULL)
    {
        return R4_CLOCK64_INVALID_ARGUMENT;
    }
    if (clock->initialized == 0U)
    {
        return R4_CLOCK64_NOT_INITIALIZED;
    }
    return clock->first_error;
}
