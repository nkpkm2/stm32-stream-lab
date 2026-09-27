#ifndef R6_HW_HARNESS_H
#define R6_HW_HARNESS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define R6_HW_MAGIC UINT32_C(0x52364857)
#define R6_HW_COMPLETE UINT32_C(0x5236434E)

typedef struct
{
    uint32_t magic;
    uint32_t prediction_status;
    uint32_t binding_status;
    uint32_t freeze_status;
    uint32_t second_freeze_status;
    uint32_t complete_magic;
    uint32_t t10_admitted;
    uint32_t unlock_epilogue_admitted;
    uint32_t pass;
    uint64_t t10_nominal;
    uint64_t t10_admission;
    uint64_t t10_lock;
    uint64_t first_unlock;
    uint64_t first_processing_available;
    uint64_t second_admission;
    uint64_t second_processing_start;
    uint64_t artifact_id;
    uint64_t resolved_config_hash;
    uint64_t model_hash;
    uint64_t calibration_hash;
    uint64_t instrumentation_profile_hash;
    uint64_t platform_profile_hash;
    uint64_t phase_window_hash;
    uint64_t result_schema_hash;
} R6HwHarnessResult;

extern volatile R6HwHarnessResult g_r6_hw_result;
void R6_HW_Start(void);

#ifdef __cplusplus
}
#endif

#endif /* R6_HW_HARNESS_H */
