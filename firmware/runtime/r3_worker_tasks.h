#ifndef R3_WORKER_TASKS_H
#define R3_WORKER_TASKS_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "r3_worker_contract.h"
#include "stream_run_authority.h"

#ifdef __cplusplus
extern "C" {
#endif

#define R3_WORKER_TASKS_PROCESS_STACK_WORDS 768U
#define R3_WORKER_TASKS_INTERFERENCE_STACK_WORDS 512U
#define R3_WORKER_TASKS_PROCESS_PRIORITY (tskIDLE_PRIORITY + 3U)
#define R3_WORKER_TASKS_INTERFERENCE_PRIORITY (tskIDLE_PRIORITY + 1U)

typedef enum
{
    R3_WORKER_TASKS_OK = 0,
    R3_WORKER_TASKS_EMPTY,
    R3_WORKER_TASKS_INVALID_ARGUMENT,
    R3_WORKER_TASKS_INVALID_STATE,
    R3_WORKER_TASKS_CONTROL_ERROR,
    R3_WORKER_TASKS_AUTHORITY_ERROR,
    R3_WORKER_TASKS_CALLBACK_ERROR,
    R3_WORKER_TASKS_ACK_BACKPRESSURE,
    R3_WORKER_TASKS_FREERTOS_ERROR,
    R3_WORKER_TASKS_FAULTED
} R3WorkerTasksStatus;

typedef enum
{
    R3_WORKER_CLAIM_GRANTED = 0,
    R3_WORKER_CLAIM_STOP_CURRENT,
    R3_WORKER_CLAIM_CLOSED,
    R3_WORKER_CLAIM_STALE,
    R3_WORKER_CLAIM_ERROR
} R3WorkerClaimDecision;

typedef enum
{
    R3_INTERFERENCE_RELEASE_GRANTED = 0,
    R3_INTERFERENCE_RELEASE_STOP_CURRENT,
    R3_INTERFERENCE_RELEASE_CLOSED,
    R3_INTERFERENCE_RELEASE_STALE,
    R3_INTERFERENCE_RELEASE_ERROR
} R3InterferenceReleaseDecision;

typedef struct
{
    uint32_t run_valid;
    R3WorkerRunKey run;
    StreamRunTicket stream_ticket;

    uint32_t processing_claim_allowed;
    uint32_t interference_release_allowed;

    uint32_t stop_valid;
    R3WorkerStopContext stop;
} R3WorkerTaskControlView;

typedef R3WorkerTasksStatus (*R3WorkerReadControlFn)(
    void *context,
    R3WorkerTaskControlView *out_view);

/*
 * Linearization boundary for READY -> PROCESSING.
 *
 * GRANTED means the callback validated current run/generation and the
 * processing-claim gate, then called StreamRunAuthority_ClaimHeldReady()
 * in the same serialized lifecycle decision boundary.
 *
 * STOP_CURRENT means STOP was already current at that boundary. out_stop must
 * identify that exact stop transaction; the Processing worker will CANCEL the
 * exact HELD_READY descriptor instead of claiming it.
 */
typedef R3WorkerClaimDecision (*R3WorkerTryClaimHeldFn)(
    void *context,
    const R3WorkerRunKey *run,
    const StreamRunTicket *ticket,
    R3WorkerStopContext *out_stop);

/*
 * Linearization boundary for release of one interference segment.
 *
 * GRANTED means the release gate was open at the serialized decision point.
 * If STOP commits after this point, the segment is treated as already running
 * and may finish at its bounded segmentation point.
 */
typedef R3InterferenceReleaseDecision (*R3WorkerTryBeginInterferenceFn)(
    void *context,
    const R3WorkerRunKey *run,
    R3WorkerStopContext *out_stop);

typedef void (*R3WorkerProcessBlockFn)(
    void *context,
    const StreamOwnershipDescriptor *ownership);

/* Return nonzero when another bounded segment remains pending. */
typedef uint32_t (*R3WorkerInterferenceSegmentFn)(void *context);

typedef struct
{
    void *context;
    R3WorkerReadControlFn read_control;
    R3WorkerTryClaimHeldFn try_claim_held;
    R3WorkerTryBeginInterferenceFn try_begin_interference;
    R3WorkerProcessBlockFn process_block;
    R3WorkerInterferenceSegmentFn interference_segment;
} R3WorkerTaskHooks;

typedef struct
{
    TaskHandle_t communication_task;
    uint32_t processing_ack_notify_bit;
    uint32_t interference_ack_notify_bit;
    R3WorkerTaskHooks hooks;
} R3WorkerTasksConfig;

typedef struct
{
    uint32_t create_attempted;
    uint32_t created;
    uint32_t faulted;
    R3WorkerTasksStatus first_fault;

    uint32_t processing_ready;
    uint32_t interference_ready;

    TaskHandle_t processing_task;
    TaskHandle_t interference_task;

    uint32_t processing_ack_pending;
    uint32_t interference_ack_pending;

    uint32_t processing_wake_count;
    uint32_t interference_wake_count;
    uint32_t processing_complete_count;
    uint32_t processing_cancel_count;
    uint32_t interference_segment_count;

    R3InterferenceActivity interference_activity;

    R3WorkerContractSnapshot processing_contract;
    R3WorkerContractSnapshot interference_contract;
} R3WorkerTasksSnapshot;

/*
 * Persistent worker creation only. The lifecycle coordinator owns START/STOP
 * state and must provide the hooks above. Worker contracts are initialized by
 * the worker tasks themselves after the scheduler starts.
 */
R3WorkerTasksStatus R3WorkerTasks_Create(
    const R3WorkerTasksConfig *config);

R3WorkerTasksStatus R3WorkerTasks_GetHandles(
    TaskHandle_t *out_processing,
    TaskHandle_t *out_interference);

R3WorkerTasksStatus R3WorkerTasks_GetSnapshot(
    R3WorkerTasksSnapshot *out);

/* Communication-task-only lifecycle wake publication. */
R3WorkerTasksStatus R3WorkerTasks_NotifyStart(void);
R3WorkerTasksStatus R3WorkerTasks_NotifyStop(void);
R3WorkerTasksStatus R3WorkerTasks_NotifyInterferenceWork(void);

/* Acquisition/ISR wake publication. */
R3WorkerTasksStatus R3WorkerTasks_NotifyProcessingWorkFromISR(
    BaseType_t *higher_priority_task_woken);

/*
 * Communication-task-only ACK transport. This consumes a worker-owned ACK
 * mailbox; lifecycle acceptance/duplicate/stale policy remains in R3Lifecycle.
 */
R3WorkerTasksStatus R3WorkerTasks_TakeAck(
    R3WorkerId worker_id,
    R3WorkerQuiescedAck *out_ack);

#ifdef R3_WORKER_TASKS_TESTING
/*
 * Native deterministic entry points. They execute the exact production service
 * routines without blocking in xTaskNotifyWait().
 */
R3WorkerTasksStatus R3WorkerTasks_TestInstall(
    const R3WorkerTasksConfig *config,
    TaskHandle_t processing_task,
    TaskHandle_t interference_task);

R3WorkerTasksStatus R3WorkerTasks_TestServiceProcessing(
    uint32_t notification_bits);

R3WorkerTasksStatus R3WorkerTasks_TestServiceInterference(
    uint32_t notification_bits);
#endif

#ifdef __cplusplus
}
#endif

#endif
