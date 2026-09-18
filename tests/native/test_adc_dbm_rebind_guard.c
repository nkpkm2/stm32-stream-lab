#include "adc_dbm_rebind_guard.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        (void)fprintf(stderr, "CHECK failed: %s (%s:%d)\n", \
            #condition, __FILE__, __LINE__); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static AdcDbmRebindGuardPolicy Policy(uint32_t n)
{
    AdcDbmRebindGuardPolicy p;
    CHECK(AdcDbmRebindGuard_DerivePolicy(n, &p) ==
        ADC_DBM_REBIND_GUARD_OK);
    return p;
}

static void CasePolicy256(void)
{
    AdcDbmRebindGuardPolicy p = Policy(256U);
    CHECK(p.block_cycles == 230400U);
    CHECK(p.write_limit_cycles == 57600U);
    CHECK(p.final_reserve_cycles == 3600U);
    CHECK(p.min_remaining_samples == 192U);
}

static void CasePolicy512(void)
{
    AdcDbmRebindGuardPolicy p = Policy(512U);
    CHECK(p.block_cycles == 460800U);
    CHECK(p.write_limit_cycles == 115200U);
    CHECK(p.final_reserve_cycles == 3600U);
    CHECK(p.min_remaining_samples == 384U);
}

static void CaseInvalidPolicy(void)
{
    AdcDbmRebindGuardPolicy p;
    CHECK(AdcDbmRebindGuard_DerivePolicy(0U, &p) ==
        ADC_DBM_REBIND_GUARD_INVALID_ARGUMENT);
    CHECK(AdcDbmRebindGuard_DerivePolicy(65536U, &p) ==
        ADC_DBM_REBIND_GUARD_INVALID_ARGUMENT);
    CHECK(AdcDbmRebindGuard_DerivePolicy(1U, &p) ==
        ADC_DBM_REBIND_GUARD_INVALID_ARGUMENT);
    CHECK(AdcDbmRebindGuard_DerivePolicy(256U, NULL) ==
        ADC_DBM_REBIND_GUARD_INVALID_ARGUMENT);
}

static void CaseSafe(void)
{
    AdcDbmRebindGuardPolicy p = Policy(256U);
    CHECK(AdcDbmRebindGuard_Check(
        &p, 1U, 0U, 1U, 230400U + 100U, 256U, 1U) ==
        ADC_DBM_REBIND_GUARD_OK);
    CHECK(AdcDbmRebindGuard_Check(
        &p, 2U, 1U, 0U, 460800U + 100U, 192U, 1U) ==
        ADC_DBM_REBIND_GUARD_OK);
}

static void CaseEvent(void)
{
    AdcDbmRebindGuardPolicy p = Policy(256U);
    CHECK(AdcDbmRebindGuard_Check(
        &p, 0U, 0U, 1U, 0U, 256U, 1U) ==
        ADC_DBM_REBIND_GUARD_EVENT);
    CHECK(AdcDbmRebindGuard_Check(
        &p, 1U, 2U, 1U, 230400U, 256U, 1U) ==
        ADC_DBM_REBIND_GUARD_EVENT);
}

static void CaseCt(void)
{
    AdcDbmRebindGuardPolicy p = Policy(256U);
    CHECK(AdcDbmRebindGuard_Check(
        &p, 1U, 0U, 0U, 230400U, 256U, 1U) ==
        ADC_DBM_REBIND_GUARD_CT);
    CHECK(AdcDbmRebindGuard_Check(
        &p, 2U, 1U, 1U, 460800U, 256U, 1U) ==
        ADC_DBM_REBIND_GUARD_CT);
}

static void CaseHardware(void)
{
    AdcDbmRebindGuardPolicy p = Policy(256U);
    CHECK(AdcDbmRebindGuard_Check(
        &p, 1U, 0U, 1U, 230400U, 256U, 0U) ==
        ADC_DBM_REBIND_GUARD_HARDWARE);
}

static void CaseNdtr(void)
{
    AdcDbmRebindGuardPolicy p = Policy(256U);
    CHECK(AdcDbmRebindGuard_Check(
        &p, 1U, 0U, 1U, 230400U, 191U, 1U) ==
        ADC_DBM_REBIND_GUARD_NDTR);
    CHECK(AdcDbmRebindGuard_Check(
        &p, 1U, 0U, 1U, 230400U, 257U, 1U) ==
        ADC_DBM_REBIND_GUARD_NDTR);
}

static void CaseTimeBoundary(void)
{
    AdcDbmRebindGuardPolicy p = Policy(256U);
    uint32_t latest = p.write_limit_cycles - p.final_reserve_cycles;

    CHECK(AdcDbmRebindGuard_Check(
        &p, 1U, 0U, 1U,
        p.block_cycles + latest, 256U, 1U) ==
        ADC_DBM_REBIND_GUARD_OK);
    CHECK(AdcDbmRebindGuard_Check(
        &p, 1U, 0U, 1U,
        p.block_cycles + latest + 1U, 256U, 1U) ==
        ADC_DBM_REBIND_GUARD_TIME);
    CHECK(AdcDbmRebindGuard_Check(
        &p, 1U, 0U, 1U,
        p.block_cycles - 1U, 256U, 1U) ==
        ADC_DBM_REBIND_GUARD_TIME);
}

static void CaseDwtWrap(void)
{
    AdcDbmRebindGuardPolicy p = Policy(256U);
    uint32_t sequence = UINT32_MAX / p.block_cycles + 2U;
    uint32_t nominal = sequence * p.block_cycles;
    uint32_t current_ct = sequence & 1U;
    uint32_t completed_slot = current_ct ^ 1U;

    CHECK(AdcDbmRebindGuard_Check(
        &p, sequence, completed_slot, current_ct,
        nominal + 100U, 256U, 1U) == ADC_DBM_REBIND_GUARD_OK);
    CHECK(AdcDbmRebindGuard_Check(
        &p, sequence, completed_slot, current_ct,
        nominal - 1U, 256U, 1U) == ADC_DBM_REBIND_GUARD_TIME);
}

static void CaseN512Boundary(void)
{
    AdcDbmRebindGuardPolicy p = Policy(512U);
    uint32_t latest = p.write_limit_cycles - p.final_reserve_cycles;

    CHECK(AdcDbmRebindGuard_Check(
        &p, 1U, 0U, 1U,
        p.block_cycles + latest, 384U, 1U) ==
        ADC_DBM_REBIND_GUARD_OK);
    CHECK(AdcDbmRebindGuard_Check(
        &p, 1U, 0U, 1U,
        p.block_cycles + latest + 1U, 384U, 1U) ==
        ADC_DBM_REBIND_GUARD_TIME);
}

typedef void (*CaseFn)(void);

typedef struct
{
    const char *name;
    CaseFn fn;
} Case;

static const Case cases[] = {
    {"policy_256", CasePolicy256},
    {"policy_512", CasePolicy512},
    {"invalid_policy", CaseInvalidPolicy},
    {"safe", CaseSafe},
    {"event", CaseEvent},
    {"ct", CaseCt},
    {"hardware", CaseHardware},
    {"ndtr", CaseNdtr},
    {"time_boundary", CaseTimeBoundary},
    {"dwt_wrap", CaseDwtWrap},
    {"n512_boundary", CaseN512Boundary}
};

int main(int argc, char **argv)
{
    size_t i;
    int ran = 0;

    if (argc > 2)
    {
        return EXIT_FAILURE;
    }

    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        if ((argc == 1) || (strcmp(argv[1], cases[i].name) == 0))
        {
            cases[i].fn();
            ran = 1;
        }
    }

    if (ran == 0)
    {
        (void)fprintf(stderr, "Unknown case: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    (void)puts("F0-2 AdcDbm completion guard native tests: PASS");
    return EXIT_SUCCESS;
}
