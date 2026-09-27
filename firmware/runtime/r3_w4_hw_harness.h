#ifndef R3_W4_HW_HARNESS_H
#define R3_W4_HW_HARNESS_H

#include <stdint.h>

#define R3_W4_HW_RESULT_MAGIC 0x52335734UL
#define R3_W4_HW_COMPLETED_MAGIC 0xA44C0DE4UL
#define R3_W4_HW_RESULT_SCHEMA 1UL

#define R3_W4_HW_CASE_T04_A  1UL
#define R3_W4_HW_CASE_STOP_A 2UL
#define R3_W4_HW_CASE_STOP_B 3UL
#define R3_W4_HW_CASE_STOP_D 4UL
#define R3_W4_HW_CASE_T04_B  5UL
#define R3_W4_HW_CASE_STOP_C 6UL
#define R3_W4_HW_CASE_STOP_E 7UL

#define R3_W4_HW_TERMINAL_RUNNING 0UL
#define R3_W4_HW_TERMINAL_PASS    1UL
#define R3_W4_HW_TERMINAL_FAIL    2UL

typedef struct
{
    uint32_t magic;
    uint32_t schema_version;
    uint32_t case_id;
    uint32_t terminal_code;
    uint32_t invariant_bits;
    uint32_t lifecycle_state;
    uint32_t acquisition_gate;
    uint32_t processing_gate;
    uint32_t interference_gate;
    uint32_t driver_state;
    uint32_t driver_hardware_owned;
    uint32_t completion_count;
    uint32_t begin_valid;
    uint32_t begin_captured_samples;
    uint32_t begin_dma_lisr;
    uint32_t begin_tim2_cr1;
    uint32_t finish_valid;
    uint32_t finish_dma_cr;
    uint32_t rollback_ack_mask;
    uint32_t processing_entered;
    uint32_t processing_complete_count;
    uint32_t processing_cancel_count;
    uint32_t worker_faulted;
    uint32_t worker_first_fault;
    uint32_t runtime_fault;
    uint32_t duplicate_stop_ok;
    uint32_t inactive_keep_count;
    uint32_t inactive_rebind_count;
    uint32_t injected_error_observed;
    uint32_t completed_magic;
} R3W4HwResult;

extern volatile R3W4HwResult g_r3_w4_hw_result;
void R3_W4_HW_Start(void);

#endif
