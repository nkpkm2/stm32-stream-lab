#ifndef R3_W3_HW_HARNESS_H
#define R3_W3_HW_HARNESS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define R3_W3_HW_RESULT_MAGIC 0x52335733UL
#define R3_W3_HW_COMPLETED_MAGIC 0xA33C0DE3UL
#define R3_W3_HW_RESULT_SCHEMA 1UL

#define R3_W3_HW_CASE_START_A 1UL
#define R3_W3_HW_CASE_T06_A   2UL
#define R3_W3_HW_CASE_T06_B   3UL
#define R3_W3_HW_CASE_START_C 4UL

#define R3_W3_HW_TERMINAL_RUNNING 0UL
#define R3_W3_HW_TERMINAL_PASS    1UL
#define R3_W3_HW_TERMINAL_FAIL    2UL

typedef struct
{
    uint32_t magic;
    uint32_t schema_version;
    uint32_t case_id;
    uint32_t terminal_code;
    uint32_t invariant_bits;
    uint32_t lifecycle_state;
    uint32_t start_ticket_valid;
    uint32_t acquisition_gate;
    uint32_t processing_gate;
    uint32_t interference_gate;
    uint32_t driver_state;
    uint32_t driver_hardware_owned;
    uint32_t driver_tim2_cr1;
    uint32_t driver_dma_cr;
    uint32_t worker_processing_phase;
    uint32_t worker_interference_phase;
    uint32_t rollback_ack_mask;
    uint32_t runtime_fault;
    uint32_t lifecycle_failure_count;
    uint32_t lifecycle_rollback_count;
    uint32_t completed_magic;
} R3W3HwResult;

extern volatile R3W3HwResult g_r3_w3_hw_result;

void R3_W3_HW_Start(void);

#ifdef __cplusplus
}
#endif

#endif
