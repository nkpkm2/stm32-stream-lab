#ifndef R2_W3_GUARD_H
#define R2_W3_GUARD_H

#include <stdint.h>

/* W3 one-shot diagnostic envelope, not a general runtime timing service. */
#define R2_W3_K 8U
#define R2_W3_BUFFERS 10U
#define R2_W3_EVENTS 8U
#define R2_W3_BLOCK_SAMPLES 256U
#define R2_W3_BLOCK_CYCLES 230400U
#define R2_W3_WRITE_LIMIT_CYCLES 57600U
#define R2_W3_EXIT_LIMIT_CYCLES 80640U
#define R2_W3_FINAL_RESERVE_CYCLES 3600U
#define R2_W3_MIN_REMAINING_SAMPLES 192U

typedef enum
{
    R2_W3_GUARD_OK = 0,
    R2_W3_GUARD_EVENT,
    R2_W3_GUARD_CT,
    R2_W3_GUARD_HARDWARE,
    R2_W3_GUARD_NDTR,
    R2_W3_GUARD_TIME
} R2_W3_GuardStatus;

/* elapsed is unsigned DWT(now - epoch_before_timer_start), for this <=100ms
 * experiment only. CT equality alone does not detect two missed completions.
 * This function has no register access and never grants a hardware write by
 * itself. The caller must also check plan, addresses, owners and run gate.
 * FINAL_RESERVE is a diagnostic engineering allowance, not certified WCET.
 */
static inline R2_W3_GuardStatus R2_W3_CheckWindow(
    uint32_t sequence, uint32_t observed_ct, uint32_t current_ct,
    uint32_t elapsed, uint32_t ndtr, uint32_t hardware_ok)
{
    uint32_t nominal;
    if ((sequence == 0U) || (sequence > R2_W3_EVENTS))
    {
        return R2_W3_GUARD_EVENT;
    }
    if ((observed_ct > 1U) || (current_ct != observed_ct) ||
        (current_ct != (sequence & 1U)))
    {
        return R2_W3_GUARD_CT;
    }
    if (hardware_ok == 0U)
    {
        return R2_W3_GUARD_HARDWARE;
    }
    if ((ndtr < R2_W3_MIN_REMAINING_SAMPLES) || (ndtr > R2_W3_BLOCK_SAMPLES))
    {
        return R2_W3_GUARD_NDTR;
    }
    nominal = sequence * R2_W3_BLOCK_CYCLES;
    if ((elapsed < nominal) ||
        ((elapsed - nominal) >
         (R2_W3_WRITE_LIMIT_CYCLES - R2_W3_FINAL_RESERVE_CYCLES)))
    {
        return R2_W3_GUARD_TIME;
    }
    return R2_W3_GUARD_OK;
}

#endif
