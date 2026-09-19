#ifndef R3_WORKER_CONTRACT_H
#define R3_WORKER_CONTRACT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define R3_WORKER_WAKE_WORK  (1UL << 0)
#define R3_WORKER_WAKE_STOP  (1UL << 1)
#define R3_WORKER_WAKE_START (1UL << 2)
#define R3_WORKER_WAKE_MASK \
    (R3_WORKER_WAKE_WORK | R3_WORKER_WAKE_STOP | R3_WORKER_WAKE_START)

typedef enum
{
    R3_WORKER_PROCESSING = 1,
    R3_WORKER_INTERFERENCE = 2
} R3WorkerId;

typedef struct
{
    uint32_t boot_id;
    uint32_t run_id;
    uint32_t generation;
} R3WorkerRunKey;

typedef struct
{
    R3WorkerRunKey run;
    uint32_t stop_id;
} R3WorkerStopContext;

typedef struct
{
    uint32_t boot_id;
    uint32_t run_id;
    uint32_t generation;
    uint32_t stop_id;
    R3WorkerId worker_id;
} R3WorkerQuiescedAck;

typedef enum
{
    R3_WORKER_PHASE_UNBOUND = 0,
    R3_WORKER_PHASE_RUN_BOUND,
    R3_WORKER_PHASE_STOP_PENDING,
    R3_WORKER_PHASE_QUIESCED
} R3WorkerPhase;

typedef enum
{
    R3_WORKER_OK = 0,
    R3_WORKER_INVALID_ARGUMENT,
    R3_WORKER_INVALID_STATE,
    R3_WORKER_STALE_RUN,
    R3_WORKER_STOP_CONFLICT,
    R3_WORKER_NOT_READY,
    R3_WORKER_ALREADY_ACKED
} R3WorkerStatus;

typedef enum
{
    R3_WORKER_WAKE_NONE = 0,
    R3_WORKER_WAKE_WORK_SELECTED,
    R3_WORKER_WAKE_START_SELECTED,
    R3_WORKER_WAKE_STOP_SELECTED
} R3WorkerWakeSelection;

typedef enum
{
    R3_PROCESSING_RESOURCE_NONE = 0,
    R3_PROCESSING_RESOURCE_HELD_READY,
    R3_PROCESSING_RESOURCE_PROCESSING
} R3ProcessingResourceState;

typedef enum
{
    R3_PROCESSING_STOP_INVALID = 0,
    R3_PROCESSING_STOP_CANCEL_HELD,
    R3_PROCESSING_STOP_FINISH_CURRENT,
    R3_PROCESSING_STOP_DRAIN_ONE_READY,
    R3_PROCESSING_STOP_EMIT_ACK
} R3ProcessingStopAction;

typedef enum
{
    R3_INTERFERENCE_ACTIVITY_NONE = 0,
    R3_INTERFERENCE_ACTIVITY_PENDING,
    R3_INTERFERENCE_ACTIVITY_RUNNING
} R3InterferenceActivity;

typedef enum
{
    R3_INTERFERENCE_STOP_INVALID = 0,
    R3_INTERFERENCE_STOP_CANCEL_PENDING,
    R3_INTERFERENCE_STOP_FINISH_SEGMENT,
    R3_INTERFERENCE_STOP_EMIT_ACK
} R3InterferenceStopAction;

typedef struct
{
    R3WorkerId worker_id;
    R3WorkerPhase phase;
    R3WorkerRunKey run;
    uint32_t stop_id;
    uint32_t ack_emitted;
    uint32_t bind_count;
    uint32_t stop_latch_count;
    uint32_t ack_count;
    uint32_t reject_count;
} R3WorkerContract;

typedef struct
{
    R3WorkerId worker_id;
    R3WorkerPhase phase;
    R3WorkerRunKey run;
    uint32_t stop_id;
    uint32_t ack_emitted;
    uint32_t bind_count;
    uint32_t stop_latch_count;
    uint32_t ack_count;
    uint32_t reject_count;
} R3WorkerContractSnapshot;

/*
 * W2-A owns worker-local lifecycle identity only.
 * It deliberately does NOT own BufferPool/queue/lease truth and does not call
 * FreeRTOS. The worker task is the sole mutator of its R3WorkerContract.
 */
R3WorkerStatus R3WorkerContract_Initialize(
    R3WorkerContract *worker,
    R3WorkerId worker_id);

R3WorkerStatus R3WorkerContract_BindRun(
    R3WorkerContract *worker,
    const R3WorkerRunKey *run);

R3WorkerStatus R3WorkerContract_RequestStop(
    R3WorkerContract *worker,
    const R3WorkerStopContext *stop);

R3WorkerStatus R3WorkerContract_BuildQuiescedAck(
    R3WorkerContract *worker,
    uint32_t resources_released,
    R3WorkerQuiescedAck *out_ack);

R3WorkerStatus R3WorkerContract_GetSnapshot(
    const R3WorkerContract *worker,
    R3WorkerContractSnapshot *out);

uint32_t R3WorkerContract_AllowsBoundRunMutation(
    const R3WorkerContract *worker);

uint32_t R3WorkerContract_NewWorkAllowed(
    const R3WorkerContract *worker,
    uint32_t lifecycle_gate_open);

R3WorkerWakeSelection R3WorkerContract_SelectWake(uint32_t notification_bits);

uint32_t R3WorkerAck_MatchesStop(
    const R3WorkerQuiescedAck *ack,
    const R3WorkerStopContext *stop,
    R3WorkerId expected_worker);

R3ProcessingStopAction R3ProcessingWorker_StopAction(
    const R3WorkerContract *worker,
    R3ProcessingResourceState resource_state,
    uint32_t ready_available);

R3InterferenceStopAction R3InterferenceWorker_StopAction(
    const R3WorkerContract *worker,
    R3InterferenceActivity activity);

#ifdef __cplusplus
}
#endif

#endif
