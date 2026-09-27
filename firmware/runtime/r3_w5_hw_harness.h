#ifndef R3_W5_HW_HARNESS_H
#define R3_W5_HW_HARNESS_H

#include <stdint.h>

#define R3_W5_HW_RESULT_MAGIC 0x52335735UL
#define R3_W5_HW_COMPLETED_MAGIC 0xA55C0DE5UL
#define R3_W5_HW_RESULT_SCHEMA 1UL

#define R3_W5_HW_CASE_T13_A 1UL
#define R3_W5_HW_CASE_T13_C 2UL
#define R3_W5_HW_CASE_T13_D 3UL
#define R3_W5_HW_CASE_T13_G 4UL
#define R3_W5_HW_CASE_T20_A 5UL
#define R3_W5_HW_CASE_T20_B 6UL

typedef struct
{
    uint32_t magic, schema_version, case_id, terminal_code, invariant_bits;
    uint32_t primary_status, lifecycle_state, ledger_phase, ledger_run_id;
    uint32_t ledger_generation, start_ticket, commit_count, rollback_count;
    uint32_t result_valid, result_references, result_id, bytes_immutable;
    uint32_t old_boot_status, duplicate_ticket_match, worker_faulted;
    uint32_t runtime_fault, completed_magic;
} R3W5HwResult;

extern volatile R3W5HwResult g_r3_w5_hw_result;
void R3_W5_HW_Start(void);

#endif
