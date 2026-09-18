#ifndef R2_CT_CONTROL_H
#define R2_CT_CONTROL_H

#include <stdint.h>

#include "r2_ct_protocol.h"

#define R2_CT_MAGIC 0x52324354UL
#define R2_CT_TRACE_CAPACITY 64U
#define R2_CT_RX_RING_BYTES 1024U
#define R2_CT_MAX_ACCEPTED_PER_SECOND 10U

typedef enum
{
    R2_CT_FAULT_TASK_CREATE = 1U << 0,
    R2_CT_FAULT_UART_RX = 1U << 1,
    R2_CT_FAULT_RX_OVERFLOW = 1U << 2,
    R2_CT_FAULT_TX_START = 1U << 3,
    R2_CT_FAULT_TX_DMA = 1U << 4,
    R2_CT_FAULT_TRACE_OVERFLOW = 1U << 5
} R2_CT_Fault;

typedef struct
{
    uint32_t request_id;
    uint32_t payload_length;
    uint32_t rx_complete_cycle;
    uint32_t processed_cycle;
    uint32_t reply_start_cycle;
    uint32_t reply_complete_cycle;
    uint32_t w6_input_at_process;
    uint32_t w6_phase_at_process;
    uint32_t payload_crc32;
    uint32_t processed_while_running;
} R2_CT_Trace;

typedef struct
{
    uint32_t magic;
    uint32_t task_created;
    uint32_t task_started;
    uint32_t fault_bits;
    uint32_t rx_byte_count;
    uint32_t rx_frame_count;
    uint32_t rx_length_error_count;
    uint32_t rx_crc_error_count;
    uint32_t rx_version_error_count;
    uint32_t rx_unsupported_count;
    uint32_t rx_overflow_count;
    uint32_t uart_error_count;
    uint32_t rate_limited_count;
    uint32_t processed_count;
    uint32_t processed_while_running_count;
    uint32_t reply_attempt_count;
    uint32_t reply_success_count;
    uint32_t reply_error_count;
    uint32_t first_processed_w6_input;
    uint32_t last_processed_w6_input;
    uint32_t trace_count;
    R2_CT_Trace trace[R2_CT_TRACE_CAPACITY];
} R2_CT_Result;

extern volatile R2_CT_Result g_r2_ct_result;

void R2_CT_CreateTask(void);
uint32_t R2_CT_UsartIrqHandler(void);
uint32_t R2_CT_TxDmaIrqHandler(void);

#endif
