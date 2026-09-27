#ifndef R3_W2_HW_HARNESS_H
#define R3_W2_HW_HARNESS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define R3_W2_HW_RESULT_MAGIC      0x52335732UL
#define R3_W2_HW_RESULT_SCHEMA     1UL
#define R3_W2_HW_COMPLETED_MAGIC   0xC04D17EDUL

#define R3_W2_HW_CASE_T03_A 1UL
#define R3_W2_HW_CASE_T03_C 2UL
#define R3_W2_HW_CASE_T03_D 3UL
#define R3_W2_HW_CASE_T05_A 4UL
#define R3_W2_HW_CASE_T05_B 5UL

#ifndef R3_W2_HW_CASE_ID
#error "R3_W2_HW_CASE_ID must be provided by STREAM_LAB_R3_W2_HW"
#endif

#if ((R3_W2_HW_CASE_ID < R3_W2_HW_CASE_T03_A) || \
     (R3_W2_HW_CASE_ID > R3_W2_HW_CASE_T05_B))
#error "R3_W2_HW_CASE_ID is outside the frozen W2 hardware case set"
#endif

typedef enum
{
    R3_W2_HW_TERMINAL_RUNNING = 0,
    R3_W2_HW_TERMINAL_PASS = 1,
    R3_W2_HW_TERMINAL_FAIL = 2
} R3W2HwTerminalCode;

typedef enum
{
    R3_W2_HW_FAULT_NONE = 0,
    R3_W2_HW_FAULT_CONTROLLER_CREATE,
    R3_W2_HW_FAULT_WORKER_CREATE,
    R3_W2_HW_FAULT_AUTHORITY_RESET,
    R3_W2_HW_FAULT_WORKER_READY_TIMEOUT,
    R3_W2_HW_FAULT_START_NOTIFY,
    R3_W2_HW_FAULT_RUN_BOUND_TIMEOUT,
    R3_W2_HW_FAULT_READY_INJECT,
    R3_W2_HW_FAULT_ISR_NOTIFY,
    R3_W2_HW_FAULT_SYNC_TIMEOUT,
    R3_W2_HW_FAULT_STOP_NOTIFY,
    R3_W2_HW_FAULT_ACK_TIMEOUT,
    R3_W2_HW_FAULT_ACK_TAKE,
    R3_W2_HW_FAULT_ACK_IDENTITY,
    R3_W2_HW_FAULT_CASE_INVARIANT,
    R3_W2_HW_FAULT_WORKER_FAULT,
    R3_W2_HW_FAULT_SCHEDULER_RETURNED
} R3W2HwFault;

typedef enum
{
    R3_W2_HW_SYNC_NONE = 0,
    R3_W2_HW_SYNC_CLAIM_WINDOW,
    R3_W2_HW_SYNC_PROCESSING_CALLBACK,
    R3_W2_HW_SYNC_INTERFERENCE_RELEASE_WINDOW,
    R3_W2_HW_SYNC_INTERFERENCE_SEGMENT
} R3W2HwSyncPoint;

#define R3_W2_HW_INV_WORKER_FAULT        (1UL << 0)
#define R3_W2_HW_INV_ACK_PROCESSING      (1UL << 1)
#define R3_W2_HW_INV_ACK_INTERFERENCE    (1UL << 2)
#define R3_W2_HW_INV_ACK_IDENTITY        (1UL << 3)
#define R3_W2_HW_INV_AUTHORITY_STATE     (1UL << 4)
#define R3_W2_HW_INV_CASE_COUNTERS       (1UL << 5)
#define R3_W2_HW_INV_ISR_PATH            (1UL << 6)
#define R3_W2_HW_INV_SYNC_TIMEOUT        (1UL << 7)
#define R3_W2_HW_INV_WORKER_PHASE        (1UL << 8)

typedef struct
{
    uint32_t magic;
    uint32_t schema_version;
    uint32_t case_id;
    uint32_t terminal_code;
    uint32_t first_fault;
    uint32_t invariant_bits;

    uint32_t boot_id;
    uint32_t run_id;
    uint32_t generation;
    uint32_t stop_id;

    uint32_t processing_wake_count;
    uint32_t processing_cancel_count;
    uint32_t processing_complete_count;
    uint32_t interference_wake_count;
    uint32_t interference_segment_count;

    uint32_t processing_ack_observed;
    uint32_t interference_ack_observed;

    uint32_t authority_publish_count;
    uint32_t authority_take_count;
    uint32_t authority_claim_count;
    uint32_t authority_cancel_count;
    uint32_t authority_complete_count;
    uint32_t authority_ready_valid;
    uint32_t authority_held_valid;
    uint32_t authority_processing_valid;

    uint32_t process_callback_count;
    uint32_t interference_callback_count;
    uint32_t claim_hook_count;
    uint32_t interference_release_hook_count;

    uint32_t synthetic_irq_entry_count;
    uint32_t synthetic_irq_notify_status;
    uint32_t last_sync_point;

    uint32_t worker_faulted;
    uint32_t worker_first_fault;
    uint32_t processing_phase;
    uint32_t interference_phase;

    uint32_t completed_magic;
} R3W2HwResult;

extern volatile R3W2HwResult g_r3_w2_hw_result;

/* Target-only entry. On a valid W2-HW profile this function never returns. */
void R3_W2_HW_Start(void);

#ifdef __cplusplus
}
#endif

#endif /* R3_W2_HW_HARNESS_H */
