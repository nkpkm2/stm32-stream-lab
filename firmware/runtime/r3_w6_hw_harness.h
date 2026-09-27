#ifndef R3_W6_HW_HARNESS_H
#define R3_W6_HW_HARNESS_H

#include <stdint.h>

#define R3_W6_HW_CYCLES 500U
#define R3_W6_HW_MAGIC 0x52335736UL
#define R3_W6_HW_COMPLETE 0xA66C0DE6UL

typedef struct
{
    uint32_t cycle_index, run_id, generation, stop_id;
    uint32_t start_status, stop_status, lifecycle_state, dma_hardware_owned;
    uint32_t ack_mask, ownership_active, worker_faulted, runtime_fault;
} R3W6HwCycle;

typedef struct
{
    uint32_t magic, schema, anchor, terminal, completed_cycles, fault_cycles;
    uint32_t total_keep, total_rebind, completed_magic;
    R3W6HwCycle cycles[R3_W6_HW_CYCLES];
} R3W6HwResult;

extern volatile R3W6HwResult g_r3_w6_hw_result;
void R3_W6_HW_Start(void);

#endif
