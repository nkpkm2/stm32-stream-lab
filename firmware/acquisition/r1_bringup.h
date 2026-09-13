#ifndef R1_BRINGUP_H
#define R1_BRINGUP_H

#include <stdint.h>

#include "r1_acquisition.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    R1_BRINGUP_PHASE_RESET = 0,
    R1_BRINGUP_PHASE_TASK_CREATED,
    R1_BRINGUP_PHASE_INITIALIZED,
    R1_BRINGUP_PHASE_RUNNING,
    R1_BRINGUP_PHASE_COMPLETE,
    R1_BRINGUP_PHASE_TASK_CREATE_FAILED,
    R1_BRINGUP_PHASE_START_FAILED,
    R1_BRINGUP_PHASE_STOP_FAILED
} R1_BringupPhase;

typedef struct
{
    uint32_t magic;
    uint32_t task_created;
    uint32_t short_run_pass;
    uint32_t phase;

    uint32_t start_status;
    uint32_t stop_status;

    uint32_t tim2_cr1_after_start;

    uint32_t dbm_bit_seen;
    uint32_t start_ndtr_is_256;
    uint32_t start_ct_is_m0;
    uint32_t m0ar_matches_buffer0;
    uint32_t m1ar_matches_buffer1;

    uint32_t raw0_min;
    uint32_t raw0_max;
    uint32_t raw1_min;
    uint32_t raw1_max;

    R1_AcquisitionDiagnostics diagnostics;
} R1_BringupResult;

extern volatile R1_BringupResult g_r1_bringup_result;

void R1_Bringup_CreateTask(void);

#ifdef __cplusplus
}
#endif

#endif
