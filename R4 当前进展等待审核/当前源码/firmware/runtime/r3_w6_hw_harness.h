#ifndef R3_W6_HW_HARNESS_H
#define R3_W6_HW_HARNESS_H

#include <stdint.h>

#define R3_W6_HW_CYCLES 500U
#define R3_W6_HW_MAGIC 0x52335736UL
#define R3_W6_HW_COMPLETE 0xA66C0DE6UL
#define R3_W6_HW_SCHEMA_R4_REGRESSION 2U

typedef struct
{
    uint32_t cycle_index, run_id, generation, stop_id;
    uint32_t start_status, stop_status, lifecycle_state, dma_hardware_owned;
    uint32_t ack_mask, ownership_active, worker_faulted, runtime_fault;
    uint32_t r4_checkpoint_status;
} R3W6HwCycle;

typedef struct
{
    uint32_t magic, schema, anchor, terminal, completed_cycles, fault_cycles;
    uint32_t total_keep, total_rebind, completed_magic;
    R3W6HwCycle cycles[R3_W6_HW_CYCLES];
    /* Schema 2 appends the R4 witness after the immutable schema-1 payload.
     * This keeps the lifecycle records contiguous and makes the target-memory
     * ABI explicit for the host evidence reader. */
    uint32_t r4_initialize_status, r4_window_open_status;
    uint32_t r4_window_close_status, r4_final_checkpoint_status;
    uint32_t r4_ledger_status, r4_health_status, r4_health_first_fault;
    uint64_t r4_window_cycles, r4_window_task_cycles, r4_window_irq_cycles;
    uint64_t r4_window_idle_cycles, r4_window_unclassified_cycles;
    uint64_t r4_event_serial;
} R3W6HwResult;

extern volatile R3W6HwResult g_r3_w6_hw_result;
void R3_W6_HW_Start(void);

#endif
