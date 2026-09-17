#ifndef R2_W3_REBIND_H
#define R2_W3_REBIND_H

#include <stdint.h>
#include "r2_w3_guard.h"
#include "r2_buffer_pool.h"
#include "r2_dma_slots.h"

typedef enum
{
    R2_W3_RESET = 0,
    R2_W3_TASK_CREATED,
    R2_W3_ARMED,
    R2_W3_RUNNING,
    R2_W3_QUIESCING,
    R2_W3_COMPLETE,
    R2_W3_FAILED
} R2_W3_Phase;

typedef enum
{
    R2_W3_FAULT_CONFIG = 1U << 0,
    R2_W3_FAULT_START = 1U << 1,
    R2_W3_FAULT_EVENT = 1U << 2,
    R2_W3_FAULT_DMA = 1U << 3,
    R2_W3_FAULT_OVR = 1U << 4,
    R2_W3_FAULT_MODEL = 1U << 5,
    R2_W3_FAULT_ADDRESS = 1U << 6,
    R2_W3_FAULT_PRECHECK = 1U << 7,
    R2_W3_FAULT_TIMING = 1U << 8,
    R2_W3_FAULT_STOP = 1U << 9,
    R2_W3_FAULT_TIMEOUT = 1U << 10,
    R2_W3_FAULT_SAMPLES = 1U << 11
} R2_W3_Fault;

typedef struct
{
    uint32_t sequence;
    uint32_t ct_entry;
    uint32_t completed_slot;
    uint32_t completed_id;
    uint32_t replacement_id;
    uint32_t m0_before;
    uint32_t m1_before;
    uint32_t m0_after;
    uint32_t m1_after;
    uint32_t ct_prewrite;
    uint32_t ct_postwrite;
    uint32_t ndtr_prewrite;
    uint32_t lisr_entry;
    uint32_t nominal_offset_cycles;
    uint32_t irq_entry_offset_cycles;
    uint32_t decision_offset_cycles;
    uint32_t prewrite_offset_cycles;
    uint32_t commit_offset_cycles;
    uint32_t irq_exit_offset_cycles;
    uint32_t final_window_cycles;
    uint32_t guard_status;
    uint32_t write_performed;
    uint32_t readback_ok;
    uint32_t active_address_unchanged;
    uint32_t mapping_committed;
    uint32_t ownership_committed;
    uint32_t free_after;
    uint32_t ready_after;
    uint32_t mapping_epoch_after;
} R2_W3_Trace;

typedef struct
{
    uint32_t magic;
    uint32_t task_created;
    uint32_t phase;
    uint32_t test_pass;
    uint32_t fault_bits;
    uint32_t first_fault_cycle;
    uint32_t first_fault_dma_cr;
    uint32_t first_fault_ndtr;
    uint32_t first_fault_lisr;
    uint32_t first_fault_adc_sr;
    uint32_t system_core_clock;
    uint32_t aircr;
    uint32_t epoch_before;
    uint32_t epoch_after;
    uint32_t start_status;
    uint32_t stop_status;
    uint32_t start_dma_cr;
    uint32_t start_dma_ndtr;
    uint32_t start_adc_cr2;
    uint32_t start_tim2_cr1;
    uint32_t buffer_address[R2_W3_BUFFERS];
    uint32_t irq_count;
    uint32_t full_tc_count;
    uint32_t rebind_count;
    uint32_t bounded_stop_requested;
    uint32_t suppressed_stop_irqs;
    uint32_t dma_error_flags_seen;
    uint32_t adc_ovr_seen;
    uint32_t max_nominal_to_commit_cycles;
    uint32_t max_nominal_to_irq_exit_cycles;
    uint32_t max_final_window_cycles;
    uint32_t stop_ct;
    uint32_t stop_ndtr;
    uint32_t stop_lisr_before_abort;
    uint32_t stop_lisr_after_abort;
    uint32_t stop_dma_cr;
    uint32_t stop_adc_cr2;
    uint32_t stop_tim2_cr1;
    uint32_t quiet_tc_count;
    uint32_t quiet_rebind_count;
    uint32_t quiet_irq_count;
    uint32_t full_sample_count;
    uint32_t sample_errors;
    uint32_t canary_errors;
    uint32_t raw_min[R2_W3_EVENTS];
    uint32_t raw_max[R2_W3_EVENTS];
    R2_BufferPoolSnapshot pool_at_stop;
    R2_DmaSlotsSnapshot slots_at_stop;
    R2_W3_Trace trace[R2_W3_EVENTS];
} R2_W3_Result;

/* Read-only diagnostic interface for debugger/host; never an ownership API.
 * Task: setup -> ISR: run/commit -> task: quiescent finalization.
 * No task queries W1/W2 while acquisition is RUNNING.
 */
extern volatile R2_W3_Result g_r2_w3_result;
void R2_W3_CreateTask(void);
void R2_W3_IrqEnter(uint32_t dma_lisr);
void R2_W3_IrqExit(void);

#endif
