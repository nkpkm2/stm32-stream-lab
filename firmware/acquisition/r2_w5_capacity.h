#ifndef R2_W5_CAPACITY_H
#define R2_W5_CAPACITY_H

#include <stdint.h>

#include "r2_w5_guard.h"
#include "r2_buffer_pool.h"
#include "r2_dma_slots.h"

typedef enum
{
    R2_W5_RESET = 0,
    R2_W5_TASKS_CREATED,
    R2_W5_ARMED,
    R2_W5_RUNNING,
    R2_W5_QUIESCING,
    R2_W5_COMPLETE,
    R2_W5_FAILED
} R2_W5_Phase;

typedef enum
{
    R2_W5_DECISION_NONE = 0,
    R2_W5_DECISION_ADMIT,
    R2_W5_DECISION_DROP
} R2_W5_Decision;

typedef enum
{
    R2_W5_FAULT_CONFIG = 1U << 0,
    R2_W5_FAULT_START = 1U << 1,
    R2_W5_FAULT_EVENT = 1U << 2,
    R2_W5_FAULT_DMA = 1U << 3,
    R2_W5_FAULT_OVR = 1U << 4,
    R2_W5_FAULT_MODEL = 1U << 5,
    R2_W5_FAULT_ADDRESS = 1U << 6,
    R2_W5_FAULT_PRECHECK = 1U << 7,
    R2_W5_FAULT_TIMING = 1U << 8,
    R2_W5_FAULT_DROP = 1U << 9,
    R2_W5_FAULT_READY_FULL = 1U << 10,
    R2_W5_FAULT_PROCESSING = 1U << 11,
    R2_W5_FAULT_RELEASE = 1U << 12,
    R2_W5_FAULT_QUEUE_SOURCE = 1U << 13,
    R2_W5_FAULT_TOKEN_LEDGER = 1U << 14,
    R2_W5_FAULT_STOP = 1U << 15,
    R2_W5_FAULT_TIMEOUT = 1U << 16,
    R2_W5_FAULT_SAMPLES = 1U << 17
} R2_W5_Fault;

typedef struct
{
    uint32_t sequence;
    uint32_t decision;
    uint32_t ct_entry;
    uint32_t completed_slot;
    uint32_t completed_id;
    uint32_t replacement_id;
    uint32_t ndtr_guard;
    uint32_t free_depth_after_take;
    uint32_t ready_depth_after_publish;
    uint32_t nominal_to_decision_cycles;
    uint32_t nominal_to_irq_exit_cycles;
    uint32_t final_window_cycles;
    uint32_t mapping_epoch_after;
    uint32_t m0_before;
    uint32_t m1_before;
    uint32_t m0_after;
    uint32_t m1_after;
    uint32_t processing_begin_offset_cycles;
    uint32_t processing_end_offset_cycles;
    uint32_t release_commit_offset_cycles;
    uint32_t raw_min;
    uint32_t raw_max;
    uint32_t processed_ok;
} R2_W5_Trace;

typedef struct
{
    uint32_t magic;
    uint32_t control_task_created;
    uint32_t processing_task_created;
    uint32_t phase;
    uint32_t test_pass;
    uint32_t fault_bits;
    uint32_t system_core_clock;
    uint32_t aircr;
    uint32_t start_status;
    uint32_t stop_status;
    uint32_t start_dma_cr;
    uint32_t start_dma_ndtr;
    uint32_t start_adc_cr2;
    uint32_t start_tim2_cr1;
    uint32_t epoch_before;
    uint32_t epoch_after;
    uint32_t irq_count;
    uint32_t input_count;
    uint32_t admitted_count;
    uint32_t capacity_drop_count;
    uint32_t processed_count;
    uint32_t released_count;
    uint32_t recovered_admission_after_drop_count;
    uint32_t current_drop_streak;
    uint32_t max_drop_streak;
    uint32_t init_hook_count;
    uint32_t completion_hook_count;
    uint32_t illegal_free_send_count;
    uint32_t ready_send_fail_count;
    uint32_t notification_fail_count;
    uint32_t token_ledger_errors;
    uint32_t dma_error_flags_seen;
    uint32_t adc_ovr_seen;
    uint32_t max_nominal_to_decision_cycles;
    uint32_t max_nominal_to_irq_exit_cycles;
    uint32_t max_final_window_cycles;
    uint32_t max_ready_depth;
    uint32_t min_free_depth;
    uint32_t free_queue_depth_final;
    uint32_t ready_queue_depth_final;
    uint32_t free_token_mask_final;
    uint32_t ready_token_mask_final;
    uint32_t full_sample_count;
    uint32_t sample_errors;
    uint32_t canary_errors;
    uint32_t quiet_irq_count;
    uint32_t quiet_input_count;
    uint32_t quiet_admitted_count;
    uint32_t quiet_drop_count;
    uint32_t quiet_processed_count;
    uint32_t buffer_address[R2_W5_BUFFERS];
    uint32_t admitted_by_buffer[R2_W5_BUFFERS];
    uint32_t dropped_by_buffer[R2_W5_BUFFERS];
    uint32_t processed_by_buffer[R2_W5_BUFFERS];
    uint32_t released_by_buffer[R2_W5_BUFFERS];
    R2_BufferPoolSnapshot pool_at_stop;
    R2_DmaSlotsSnapshot slots_at_stop;
    R2_W5_Trace trace[R2_W5_EVENTS];
} R2_W5_Result;

extern volatile R2_W5_Result g_r2_w5_result;

void R2_W5_CreateTasks(void);
void R2_W5_IrqEnter(uint32_t dma_lisr);
void R2_W5_IrqExit(void);

/* Called only through FreeRTOS V11.1.0 traceQUEUE_SEND when W5 is enabled. */
void R2_W5_TraceQueueSend(void *queue_handle);

#endif
