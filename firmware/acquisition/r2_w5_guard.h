#ifndef R2_W5_GUARD_H
#define R2_W5_GUARD_H

#include <stdint.h>

#define R2_W5_K 1U
#define R2_W5_BUFFERS 3U
#define R2_W5_EVENTS 64U
#define R2_W5_BLOCK_SAMPLES 256U
#define R2_W5_BLOCK_CYCLES 230400U
#define R2_W5_WRITE_LIMIT_CYCLES 57600U
#define R2_W5_EXIT_LIMIT_CYCLES 80640U
#define R2_W5_FINAL_RESERVE_CYCLES 3600U
#define R2_W5_MIN_REMAINING_SAMPLES 192U
#define R2_W5_PROCESS_HOLD_BLOCKS 6U
#define R2_W5_PROCESS_HOLD_CYCLES \
    (R2_W5_PROCESS_HOLD_BLOCKS * R2_W5_BLOCK_CYCLES)
#define R2_W5_MIN_CAPACITY_DROPS 16U
#define R2_W5_MIN_ADMISSIONS 4U
#define R2_W5_MIN_DROP_STREAK 3U
#define R2_W5_MIN_RECOVERIES 3U

typedef enum
{
    R2_W5_GUARD_OK = 0,
    R2_W5_GUARD_EVENT,
    R2_W5_GUARD_CT,
    R2_W5_GUARD_HARDWARE,
    R2_W5_GUARD_NDTR,
    R2_W5_GUARD_TIME
} R2_W5_GuardStatus;

static inline R2_W5_GuardStatus R2_W5_CheckWindow(
    uint32_t sequence,
    uint32_t observed_ct,
    uint32_t current_ct,
    uint32_t elapsed,
    uint32_t ndtr,
    uint32_t hardware_ok)
{
    uint32_t nominal;

    if ((sequence == 0U) || (sequence > R2_W5_EVENTS))
    {
        return R2_W5_GUARD_EVENT;
    }
    if ((observed_ct > 1U) ||
        (current_ct != observed_ct) ||
        (current_ct != (sequence & 1U)))
    {
        return R2_W5_GUARD_CT;
    }
    if (hardware_ok == 0U)
    {
        return R2_W5_GUARD_HARDWARE;
    }
    if ((ndtr < R2_W5_MIN_REMAINING_SAMPLES) ||
        (ndtr > R2_W5_BLOCK_SAMPLES))
    {
        return R2_W5_GUARD_NDTR;
    }

    nominal = sequence * R2_W5_BLOCK_CYCLES;
    if ((elapsed < nominal) ||
        ((elapsed - nominal) >
         (R2_W5_WRITE_LIMIT_CYCLES - R2_W5_FINAL_RESERVE_CYCLES)))
    {
        return R2_W5_GUARD_TIME;
    }

    return R2_W5_GUARD_OK;
}

#endif
