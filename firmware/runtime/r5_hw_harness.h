#ifndef R5_HW_HARNESS_H
#define R5_HW_HARNESS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define R5_HW_MAGIC UINT32_C(0x52354857)
#define R5_HW_COMPLETE UINT32_C(0x5235434E)

typedef struct
{
    uint32_t magic;
    uint32_t init_status;
    uint32_t known_status;
    uint32_t cutoff_status;
    uint32_t seal_status;
    uint32_t complete_magic;
    uint32_t phase;
    uint32_t outcome_status;
    uint64_t raw_input_count;
    uint64_t cohort_input_count;
    uint64_t admitted_count;
    uint64_t drop_count;
    uint64_t on_time_count;
    uint64_t late_completed_count;
    uint64_t expired_unresolved_count;
    uint64_t completed_count;
    uint64_t post_cutoff_completion_count;
    uint64_t window_open_time;
    uint64_t window_close_time;
    uint32_t p99_completed_status;
    uint32_t p99_all_admitted_status;
    uint32_t s2_empty_status;
    uint32_t s2_empty_pass;
    uint64_t s2_empty_cohort_input_count;
    uint64_t s2_empty_drop_count;
    uint64_t s2_empty_admitted_count;
    uint32_t result_store_seal_status;
    uint32_t result_store_acquire_status;
    uint32_t result_store_release_status;
    uint32_t pass;
} R5HwHarnessResult;

extern volatile R5HwHarnessResult g_r5_hw_result;
void R5_HW_Start(void);

#ifdef __cplusplus
}
#endif

#endif /* R5_HW_HARNESS_H */
