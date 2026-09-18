#ifndef ADC_DBM_REBIND_GUARD_H
#define ADC_DBM_REBIND_GUARD_H

#include <stdint.h>

/*
 * Pure timing/phase policy for the R2-compatible inactive-slot completion path.
 * No register access and no ownership/mapping mutation occurs here.
 *
 * The foundation currently freezes fs=200 kS/s at 180 MHz, therefore each
 * sample interval is 900 core cycles.  The R2 service margin reserves the
 * first quarter of the next block for the rebind decision and retains a
 * 3600-cycle final protected-window allowance.
 */
#define ADC_DBM_REBIND_SAMPLE_CYCLES          900U
#define ADC_DBM_REBIND_FINAL_RESERVE_CYCLES 3600U

typedef enum
{
    ADC_DBM_REBIND_GUARD_OK = 0,
    ADC_DBM_REBIND_GUARD_INVALID_ARGUMENT,
    ADC_DBM_REBIND_GUARD_EVENT,
    ADC_DBM_REBIND_GUARD_CT,
    ADC_DBM_REBIND_GUARD_HARDWARE,
    ADC_DBM_REBIND_GUARD_NDTR,
    ADC_DBM_REBIND_GUARD_TIME
} AdcDbmRebindGuardStatus;

typedef struct
{
    uint32_t block_samples;
    uint32_t block_cycles;
    uint32_t write_limit_cycles;
    uint32_t final_reserve_cycles;
    uint32_t min_remaining_samples;
} AdcDbmRebindGuardPolicy;

static inline AdcDbmRebindGuardStatus AdcDbmRebindGuard_DerivePolicy(
    uint32_t block_samples,
    AdcDbmRebindGuardPolicy *out)
{
    AdcDbmRebindGuardPolicy policy;

    if ((out == 0) ||
        (block_samples == 0U) ||
        (block_samples > 65535U))
    {
        return ADC_DBM_REBIND_GUARD_INVALID_ARGUMENT;
    }

    policy.block_samples = block_samples;
    policy.block_cycles =
        block_samples * ADC_DBM_REBIND_SAMPLE_CYCLES;
    policy.write_limit_cycles = policy.block_cycles / 4U;
    policy.final_reserve_cycles =
        ADC_DBM_REBIND_FINAL_RESERVE_CYCLES;
    policy.min_remaining_samples =
        block_samples - (block_samples / 4U);

    if (policy.write_limit_cycles <= policy.final_reserve_cycles)
    {
        return ADC_DBM_REBIND_GUARD_INVALID_ARGUMENT;
    }

    *out = policy;
    return ADC_DBM_REBIND_GUARD_OK;
}

static inline AdcDbmRebindGuardStatus AdcDbmRebindGuard_Check(
    const AdcDbmRebindGuardPolicy *policy,
    uint32_t sequence,
    uint32_t completed_slot,
    uint32_t current_ct,
    uint32_t elapsed_from_start,
    uint32_t ndtr,
    uint32_t hardware_ok)
{
    uint32_t nominal;
    uint32_t latest_prewrite;

    if (policy == 0)
    {
        return ADC_DBM_REBIND_GUARD_INVALID_ARGUMENT;
    }

    if ((policy->block_samples == 0U) ||
        (policy->block_cycles == 0U) ||
        (policy->write_limit_cycles <= policy->final_reserve_cycles) ||
        (policy->min_remaining_samples > policy->block_samples))
    {
        return ADC_DBM_REBIND_GUARD_INVALID_ARGUMENT;
    }

    if ((sequence == 0U) || (completed_slot > 1U))
    {
        return ADC_DBM_REBIND_GUARD_EVENT;
    }

    if ((current_ct > 1U) ||
        (current_ct != (completed_slot ^ 1U)) ||
        (current_ct != (sequence & 1U)))
    {
        return ADC_DBM_REBIND_GUARD_CT;
    }

    if (hardware_ok == 0U)
    {
        return ADC_DBM_REBIND_GUARD_HARDWARE;
    }

    if ((ndtr < policy->min_remaining_samples) ||
        (ndtr > policy->block_samples))
    {
        return ADC_DBM_REBIND_GUARD_NDTR;
    }

    nominal = sequence * policy->block_cycles;
    latest_prewrite =
        policy->write_limit_cycles - policy->final_reserve_cycles;

    /* Unsigned subtraction is intentional: elapsed and nominal are both
     * modulo-2^32 DWT offsets, so this remains valid across CYCCNT wrap. */
    if ((elapsed_from_start - nominal) > latest_prewrite)
    {
        return ADC_DBM_REBIND_GUARD_TIME;
    }

    return ADC_DBM_REBIND_GUARD_OK;
}

#endif
