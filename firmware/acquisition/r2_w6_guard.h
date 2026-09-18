#ifndef R2_W6_GUARD_H
#define R2_W6_GUARD_H

#include <stdint.h>

#ifndef R2_W6_K
#error "R2_W6_K must be defined as 1, 2, 4, or 8"
#endif

#if ((R2_W6_K != 1U) && (R2_W6_K != 2U) && \
     (R2_W6_K != 4U) && (R2_W6_K != 8U))
#error "R2_W6_K must be 1, 2, 4, or 8"
#endif

#ifndef R2_W6_DROP_MODE
#error "R2_W6_DROP_MODE must be defined as 0 or 1"
#endif

#if ((R2_W6_DROP_MODE != 0U) && (R2_W6_DROP_MODE != 1U))
#error "R2_W6_DROP_MODE must be 0 or 1"
#endif

#define R2_W6_BUFFERS (R2_W6_K + 2U)
#ifndef R2_W6_EVENTS
#define R2_W6_EVENTS 96U
#endif
#define R2_W6_BLOCK_SAMPLES 256U
#define R2_W6_BLOCK_CYCLES 230400U
#define R2_W6_WRITE_LIMIT_CYCLES 57600U
#define R2_W6_EXIT_LIMIT_CYCLES 80640U
#define R2_W6_FINAL_RESERVE_CYCLES 3600U
#define R2_W6_MIN_REMAINING_SAMPLES 192U

/* DROP mode deliberately keeps one buffer in Processing long enough for all K
 * free tokens to be consumed. NORMAL mode performs no deliberate hold. */
#define R2_W6_DROP_HOLD_BLOCKS (R2_W6_K + 5U)
#define R2_W6_PROCESS_HOLD_BLOCKS \
    (R2_W6_DROP_MODE != 0U ? R2_W6_DROP_HOLD_BLOCKS : 0U)
#define R2_W6_PROCESS_HOLD_CYCLES \
    (R2_W6_PROCESS_HOLD_BLOCKS * R2_W6_BLOCK_CYCLES)

#define R2_W6_MIN_CAPACITY_DROPS 8U
#define R2_W6_MIN_ADMISSIONS (R2_W6_K + 3U)
#define R2_W6_MIN_DROP_STREAK 3U
#define R2_W6_MIN_RECOVERIES 3U

typedef enum
{
    R2_W6_GUARD_OK = 0,
    R2_W6_GUARD_EVENT,
    R2_W6_GUARD_CT,
    R2_W6_GUARD_HARDWARE,
    R2_W6_GUARD_NDTR,
    R2_W6_GUARD_TIME
} R2_W6_GuardStatus;

static inline R2_W6_GuardStatus R2_W6_CheckWindow(
    uint32_t sequence,
    uint32_t observed_ct,
    uint32_t current_ct,
    uint32_t elapsed,
    uint32_t ndtr,
    uint32_t hardware_ok)
{
    uint32_t nominal;

    if ((sequence == 0U) || (sequence > R2_W6_EVENTS))
    {
        return R2_W6_GUARD_EVENT;
    }
    if ((observed_ct > 1U) ||
        (current_ct != observed_ct) ||
        (current_ct != (sequence & 1U)))
    {
        return R2_W6_GUARD_CT;
    }
    if (hardware_ok == 0U)
    {
        return R2_W6_GUARD_HARDWARE;
    }
    if ((ndtr < R2_W6_MIN_REMAINING_SAMPLES) ||
        (ndtr > R2_W6_BLOCK_SAMPLES))
    {
        return R2_W6_GUARD_NDTR;
    }

    nominal = sequence * R2_W6_BLOCK_CYCLES;
    if ((elapsed < nominal) ||
        ((elapsed - nominal) >
         (R2_W6_WRITE_LIMIT_CYCLES - R2_W6_FINAL_RESERVE_CYCLES)))
    {
        return R2_W6_GUARD_TIME;
    }

    return R2_W6_GUARD_OK;
}

#endif
