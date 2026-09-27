#include "r3_w2_hw_harness.h"

#include <stddef.h>
#include <string.h>

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

#include "r3_w2_hw_authority_shim.h"
#include "r3_worker_tasks.h"

#define R3_W2_HW_CONTROLLER_STACK_WORDS 768U
#define R3_W2_HW_CONTROLLER_PRIORITY (tskIDLE_PRIORITY + 2U)
#define R3_W2_HW_ACK_PROCESSING_BIT (1UL << 8)
#define R3_W2_HW_ACK_INTERFERENCE_BIT (1UL << 9)
#define R3_W2_HW_ACK_MASK \
    (R3_W2_HW_ACK_PROCESSING_BIT | R3_W2_HW_ACK_INTERFERENCE_BIT)
#define R3_W2_HW_WAIT_LIMIT_TICKS 100U
#define R3_W2_HW_BARRIER_DELAY_TICKS 10U
#define R3_W2_HW_BOOT_ID 0xB0070001UL
#define R3_W2_HW_RUN_ID 1UL
#define R3_W2_HW_GENERATION 1UL

#if (R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T03_A)
#define R3_W2_HW_STOP_ID 31UL
#elif (R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T03_C)
#define R3_W2_HW_STOP_ID 92UL
#elif (R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T03_D)
#define R3_W2_HW_STOP_ID 94UL
#elif (R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T05_A)
#define R3_W2_HW_STOP_ID 91UL
#elif (R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T05_B)
#define R3_W2_HW_STOP_ID 95UL
#else
#error "Unsupported R3_W2_HW_CASE_ID"
#endif

typedef struct
{
    R3WorkerTaskControlView control;
    TaskHandle_t controller_task;
    volatile uint32_t sync_point;
    volatile uint32_t process_callback_count;
    volatile uint32_t interference_callback_count;
    volatile uint32_t claim_hook_count;
    volatile uint32_t interference_release_hook_count;
    volatile uint32_t synthetic_irq_entry_count;
    volatile uint32_t synthetic_irq_notify_status;
    uint32_t processing_ack_observed;
    uint32_t interference_ack_observed;
    R3WorkerQuiescedAck processing_ack;
    R3WorkerQuiescedAck interference_ack;
} R3W2HwStorage;

volatile R3W2HwResult g_r3_w2_hw_result;

static R3W2HwStorage storage;
static StaticTask_t controller_tcb;
static StackType_t controller_stack[R3_W2_HW_CONTROLLER_STACK_WORDS];

static int SameRunKey(const R3WorkerRunKey *a, const R3WorkerRunKey *b)
{
    return (a->boot_id == b->boot_id) &&
        (a->run_id == b->run_id) &&
        (a->generation == b->generation);
}

static int TicketMatchesRun(
    const StreamRunTicket *ticket,
    const R3WorkerRunKey *run)
{
    return (ticket->identity.boot_id == run->boot_id) &&
        (ticket->identity.run_id == run->run_id) &&
        (ticket->identity.generation == run->generation);
}

static void SetFirstFault(R3W2HwFault fault)
{
    if (g_r3_w2_hw_result.first_fault == (uint32_t)R3_W2_HW_FAULT_NONE)
    {
        g_r3_w2_hw_result.first_fault = (uint32_t)fault;
    }
}

static void CommitStop(uint32_t stop_id)
{
    taskENTER_CRITICAL();
    storage.control.processing_claim_allowed = 0U;
    storage.control.interference_release_allowed = 0U;
    storage.control.stop_valid = 1U;
    storage.control.stop.run = storage.control.run;
    storage.control.stop.stop_id = stop_id;
    taskEXIT_CRITICAL();
}

static R3WorkerTasksStatus ReadControl(
    void *context,
    R3WorkerTaskControlView *out_view)
{
    (void)context;
    if (out_view == NULL)
    {
        return R3_WORKER_TASKS_INVALID_ARGUMENT;
    }

    taskENTER_CRITICAL();
    *out_view = storage.control;
    taskEXIT_CRITICAL();
    return R3_WORKER_TASKS_OK;
}

#if (R3_W2_HW_CASE_ID != R3_W2_HW_CASE_T03_A)
static void PublishSyncPoint(R3W2HwSyncPoint point)
{
    storage.sync_point = (uint32_t)point;
    vTaskDelay((TickType_t)R3_W2_HW_BARRIER_DELAY_TICKS);
}
#endif

static R3WorkerClaimDecision TryClaimHeld(
    void *context,
    const R3WorkerRunKey *run,
    const StreamRunTicket *ticket,
    R3WorkerStopContext *out_stop)
{
    R3WorkerTaskControlView view;
    StreamRunAuthorityStatus authority_status;

    (void)context;
    if ((run == NULL) || (ticket == NULL) || (out_stop == NULL))
    {
        return R3_WORKER_CLAIM_ERROR;
    }

    ++storage.claim_hook_count;

#if (R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T03_C)
    PublishSyncPoint(R3_W2_HW_SYNC_CLAIM_WINDOW);
#endif

    taskENTER_CRITICAL();
    view = storage.control;
    if ((view.run_valid == 0U) ||
        !SameRunKey(run, &view.run) ||
        !TicketMatchesRun(ticket, run))
    {
        taskEXIT_CRITICAL();
        return R3_WORKER_CLAIM_STALE;
    }
    if (view.stop_valid != 0U)
    {
        *out_stop = view.stop;
        taskEXIT_CRITICAL();
        return R3_WORKER_CLAIM_STOP_CURRENT;
    }
    if (view.processing_claim_allowed == 0U)
    {
        taskEXIT_CRITICAL();
        return R3_WORKER_CLAIM_CLOSED;
    }

    authority_status = StreamRunAuthority_ClaimHeldReady(ticket);
    taskEXIT_CRITICAL();
    return authority_status == STREAM_RUN_AUTHORITY_OK ?
        R3_WORKER_CLAIM_GRANTED : R3_WORKER_CLAIM_ERROR;
}

static R3InterferenceReleaseDecision TryBeginInterference(
    void *context,
    const R3WorkerRunKey *run,
    R3WorkerStopContext *out_stop)
{
    R3WorkerTaskControlView view;

    (void)context;
    if ((run == NULL) || (out_stop == NULL))
    {
        return R3_INTERFERENCE_RELEASE_ERROR;
    }

    ++storage.interference_release_hook_count;

#if (R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T05_A)
    PublishSyncPoint(R3_W2_HW_SYNC_INTERFERENCE_RELEASE_WINDOW);
#endif

    taskENTER_CRITICAL();
    view = storage.control;
    if ((view.run_valid == 0U) || !SameRunKey(run, &view.run))
    {
        taskEXIT_CRITICAL();
        return R3_INTERFERENCE_RELEASE_STALE;
    }
    if (view.stop_valid != 0U)
    {
        *out_stop = view.stop;
        taskEXIT_CRITICAL();
        return R3_INTERFERENCE_RELEASE_STOP_CURRENT;
    }
    if (view.interference_release_allowed == 0U)
    {
        taskEXIT_CRITICAL();
        return R3_INTERFERENCE_RELEASE_CLOSED;
    }
    taskEXIT_CRITICAL();
    return R3_INTERFERENCE_RELEASE_GRANTED;
}

static void ProcessBlock(
    void *context,
    const StreamOwnershipDescriptor *ownership)
{
    (void)context;
    (void)ownership;
    ++storage.process_callback_count;

#if (R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T03_D)
    PublishSyncPoint(R3_W2_HW_SYNC_PROCESSING_CALLBACK);
#endif
}

static uint32_t InterferenceSegment(void *context)
{
    (void)context;
    ++storage.interference_callback_count;

#if (R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T05_B)
    PublishSyncPoint(R3_W2_HW_SYNC_INTERFERENCE_SEGMENT);
    return 1U;
#else
    return 0U;
#endif
}

static int WaitForWorkersReady(void)
{
    uint32_t i;
    R3WorkerTasksSnapshot snapshot;

    for (i = 0U; i < R3_W2_HW_WAIT_LIMIT_TICKS; ++i)
    {
        if ((R3WorkerTasks_GetSnapshot(&snapshot) == R3_WORKER_TASKS_OK) &&
            (snapshot.processing_ready != 0U) &&
            (snapshot.interference_ready != 0U))
        {
            return 1;
        }
        vTaskDelay(1U);
    }
    return 0;
}

static int WaitForRunBound(void)
{
    uint32_t i;
    R3WorkerTasksSnapshot snapshot;

    for (i = 0U; i < R3_W2_HW_WAIT_LIMIT_TICKS; ++i)
    {
        if ((R3WorkerTasks_GetSnapshot(&snapshot) == R3_WORKER_TASKS_OK) &&
            (snapshot.processing_contract.phase == R3_WORKER_PHASE_RUN_BOUND) &&
            (snapshot.interference_contract.phase == R3_WORKER_PHASE_RUN_BOUND))
        {
            return 1;
        }
        vTaskDelay(1U);
    }
    return 0;
}

#if (R3_W2_HW_CASE_ID != R3_W2_HW_CASE_T03_A)
static int WaitForSync(R3W2HwSyncPoint point)
{
    uint32_t i;

    for (i = 0U; i < R3_W2_HW_WAIT_LIMIT_TICKS; ++i)
    {
        if (storage.sync_point == (uint32_t)point)
        {
            return 1;
        }
        vTaskDelay(1U);
    }
    return 0;
}
#endif

static int TakeAndCheckAck(
    R3WorkerId worker_id,
    R3WorkerQuiescedAck *out_ack)
{
    if (R3WorkerTasks_TakeAck(worker_id, out_ack) != R3_WORKER_TASKS_OK)
    {
        SetFirstFault(R3_W2_HW_FAULT_ACK_TAKE);
        return 0;
    }
    if ((out_ack->boot_id != storage.control.run.boot_id) ||
        (out_ack->run_id != storage.control.run.run_id) ||
        (out_ack->generation != storage.control.run.generation) ||
        (out_ack->stop_id != storage.control.stop.stop_id) ||
        (out_ack->worker_id != worker_id))
    {
        SetFirstFault(R3_W2_HW_FAULT_ACK_IDENTITY);
        return 0;
    }
    return 1;
}

static int WaitForBothAcks(void)
{
    uint32_t observed = 0U;
    uint32_t bits;
    uint32_t i;

    for (i = 0U; i < R3_W2_HW_WAIT_LIMIT_TICKS; ++i)
    {
        bits = 0U;
        (void)xTaskNotifyWait(
            0U,
            R3_W2_HW_ACK_MASK,
            &bits,
            1U);
        observed |= bits & R3_W2_HW_ACK_MASK;
        if (observed == R3_W2_HW_ACK_MASK)
        {
            break;
        }
    }
    if (observed != R3_W2_HW_ACK_MASK)
    {
        SetFirstFault(R3_W2_HW_FAULT_ACK_TIMEOUT);
        return 0;
    }

    storage.processing_ack_observed = 1U;
    storage.interference_ack_observed = 1U;
    if (!TakeAndCheckAck(
            R3_WORKER_PROCESSING, &storage.processing_ack))
    {
        return 0;
    }
    if (!TakeAndCheckAck(
            R3_WORKER_INTERFERENCE, &storage.interference_ack))
    {
        return 0;
    }
    return 1;
}

#if ((R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T03_C) || \
     (R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T03_D))
static int InjectReadyAndProcessingIRQ(
    uint32_t sequence,
    R2_BufferId buffer_id)
{
    StreamOwnershipDescriptor descriptor;

    (void)memset(&descriptor, 0, sizeof(descriptor));
    descriptor.sequence = sequence;
    descriptor.buffer_id = buffer_id;
    descriptor.completed_slot = 0U;
    descriptor.mapping_epoch = sequence;

    if (R3_W2_HW_AuthorityShim_PublishReady(
            &storage.control.stream_ticket,
            &descriptor) != STREAM_RUN_AUTHORITY_OK)
    {
        SetFirstFault(R3_W2_HW_FAULT_READY_INJECT);
        return 0;
    }

    NVIC_ClearPendingIRQ(TIM6_DAC_IRQn);
    NVIC_SetPendingIRQ(TIM6_DAC_IRQn);
    return 1;
}
#endif

static int NotifyStop(void)
{
    CommitStop(R3_W2_HW_STOP_ID);
    if (R3WorkerTasks_NotifyStop() != R3_WORKER_TASKS_OK)
    {
        SetFirstFault(R3_W2_HW_FAULT_STOP_NOTIFY);
        return 0;
    }
    return 1;
}

static int RunSelectedCase(void)
{
#if (R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T03_A)
    vTaskDelay(2U);
    return NotifyStop() && WaitForBothAcks();
#elif (R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T03_C)
    if (!InjectReadyAndProcessingIRQ(2U, (R2_BufferId)3U))
    {
        return 0;
    }
    if (!WaitForSync(R3_W2_HW_SYNC_CLAIM_WINDOW))
    {
        g_r3_w2_hw_result.invariant_bits |= R3_W2_HW_INV_SYNC_TIMEOUT;
        SetFirstFault(R3_W2_HW_FAULT_SYNC_TIMEOUT);
        return 0;
    }
    return NotifyStop() && WaitForBothAcks();
#elif (R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T03_D)
    if (!InjectReadyAndProcessingIRQ(3U, (R2_BufferId)4U))
    {
        return 0;
    }
    if (!WaitForSync(R3_W2_HW_SYNC_PROCESSING_CALLBACK))
    {
        g_r3_w2_hw_result.invariant_bits |= R3_W2_HW_INV_SYNC_TIMEOUT;
        SetFirstFault(R3_W2_HW_FAULT_SYNC_TIMEOUT);
        return 0;
    }
    return NotifyStop() && WaitForBothAcks();
#elif (R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T05_A)
    if (R3WorkerTasks_NotifyInterferenceWork() != R3_WORKER_TASKS_OK)
    {
        SetFirstFault(R3_W2_HW_FAULT_CASE_INVARIANT);
        return 0;
    }
    if (!WaitForSync(R3_W2_HW_SYNC_INTERFERENCE_RELEASE_WINDOW))
    {
        g_r3_w2_hw_result.invariant_bits |= R3_W2_HW_INV_SYNC_TIMEOUT;
        SetFirstFault(R3_W2_HW_FAULT_SYNC_TIMEOUT);
        return 0;
    }
    return NotifyStop() && WaitForBothAcks();
#elif (R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T05_B)
    if (R3WorkerTasks_NotifyInterferenceWork() != R3_WORKER_TASKS_OK)
    {
        SetFirstFault(R3_W2_HW_FAULT_CASE_INVARIANT);
        return 0;
    }
    if (!WaitForSync(R3_W2_HW_SYNC_INTERFERENCE_SEGMENT))
    {
        g_r3_w2_hw_result.invariant_bits |= R3_W2_HW_INV_SYNC_TIMEOUT;
        SetFirstFault(R3_W2_HW_FAULT_SYNC_TIMEOUT);
        return 0;
    }
    return NotifyStop() && WaitForBothAcks();
#else
    return 0;
#endif
}

static void EvaluateInvariants(void)
{
    R3WorkerTasksSnapshot worker;
    R3W2HwAuthorityShimSnapshot authority;

    if (R3WorkerTasks_GetSnapshot(&worker) != R3_WORKER_TASKS_OK)
    {
        g_r3_w2_hw_result.invariant_bits |= R3_W2_HW_INV_WORKER_FAULT;
        SetFirstFault(R3_W2_HW_FAULT_WORKER_FAULT);
        return;
    }
    if (R3_W2_HW_AuthorityShim_GetHarnessSnapshot(&authority) !=
        STREAM_RUN_AUTHORITY_OK)
    {
        g_r3_w2_hw_result.invariant_bits |= R3_W2_HW_INV_AUTHORITY_STATE;
        SetFirstFault(R3_W2_HW_FAULT_CASE_INVARIANT);
        return;
    }

    g_r3_w2_hw_result.processing_wake_count = worker.processing_wake_count;
    g_r3_w2_hw_result.processing_cancel_count = worker.processing_cancel_count;
    g_r3_w2_hw_result.processing_complete_count = worker.processing_complete_count;
    g_r3_w2_hw_result.interference_wake_count = worker.interference_wake_count;
    g_r3_w2_hw_result.interference_segment_count = worker.interference_segment_count;
    g_r3_w2_hw_result.worker_faulted = worker.faulted;
    g_r3_w2_hw_result.worker_first_fault = (uint32_t)worker.first_fault;
    g_r3_w2_hw_result.processing_phase =
        (uint32_t)worker.processing_contract.phase;
    g_r3_w2_hw_result.interference_phase =
        (uint32_t)worker.interference_contract.phase;

    g_r3_w2_hw_result.authority_publish_count = authority.publish_count;
    g_r3_w2_hw_result.authority_take_count = authority.take_count;
    g_r3_w2_hw_result.authority_claim_count = authority.claim_count;
    g_r3_w2_hw_result.authority_cancel_count = authority.cancel_count;
    g_r3_w2_hw_result.authority_complete_count = authority.complete_count;
    g_r3_w2_hw_result.authority_ready_valid = authority.ready_valid;
    g_r3_w2_hw_result.authority_held_valid = authority.held_valid;
    g_r3_w2_hw_result.authority_processing_valid = authority.processing_valid;

    g_r3_w2_hw_result.process_callback_count = storage.process_callback_count;
    g_r3_w2_hw_result.interference_callback_count =
        storage.interference_callback_count;
    g_r3_w2_hw_result.claim_hook_count = storage.claim_hook_count;
    g_r3_w2_hw_result.interference_release_hook_count =
        storage.interference_release_hook_count;
    g_r3_w2_hw_result.synthetic_irq_entry_count =
        storage.synthetic_irq_entry_count;
    g_r3_w2_hw_result.synthetic_irq_notify_status =
        storage.synthetic_irq_notify_status;
    g_r3_w2_hw_result.last_sync_point = storage.sync_point;

    if (worker.faulted != 0U)
    {
        g_r3_w2_hw_result.invariant_bits |= R3_W2_HW_INV_WORKER_FAULT;
        SetFirstFault(R3_W2_HW_FAULT_WORKER_FAULT);
    }
    if ((worker.processing_contract.phase != R3_WORKER_PHASE_QUIESCED) ||
        (worker.interference_contract.phase != R3_WORKER_PHASE_QUIESCED))
    {
        g_r3_w2_hw_result.invariant_bits |= R3_W2_HW_INV_WORKER_PHASE;
    }
    if (storage.processing_ack_observed == 0U)
    {
        g_r3_w2_hw_result.invariant_bits |= R3_W2_HW_INV_ACK_PROCESSING;
    }
    if (storage.interference_ack_observed == 0U)
    {
        g_r3_w2_hw_result.invariant_bits |= R3_W2_HW_INV_ACK_INTERFERENCE;
    }
    if ((storage.processing_ack_observed != 0U) &&
        (storage.interference_ack_observed != 0U) &&
        ((storage.processing_ack.stop_id != R3_W2_HW_STOP_ID) ||
         (storage.interference_ack.stop_id != R3_W2_HW_STOP_ID)))
    {
        g_r3_w2_hw_result.invariant_bits |= R3_W2_HW_INV_ACK_IDENTITY;
    }
    if ((authority.ready_valid != 0U) ||
        (authority.held_valid != 0U) ||
        (authority.processing_valid != 0U))
    {
        g_r3_w2_hw_result.invariant_bits |= R3_W2_HW_INV_AUTHORITY_STATE;
    }

#if (R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T03_A)
    if ((worker.processing_cancel_count != 0U) ||
        (worker.processing_complete_count != 0U) ||
        (authority.take_count != 0U) ||
        (storage.process_callback_count != 0U) ||
        (storage.synthetic_irq_entry_count != 0U))
    {
        g_r3_w2_hw_result.invariant_bits |= R3_W2_HW_INV_CASE_COUNTERS;
    }
#elif (R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T03_C)
    if ((worker.processing_cancel_count != 1U) ||
        (worker.processing_complete_count != 0U) ||
        (authority.take_count != 1U) ||
        (authority.claim_count != 0U) ||
        (authority.cancel_count != 1U) ||
        (storage.claim_hook_count != 1U) ||
        (storage.process_callback_count != 0U) ||
        (storage.synthetic_irq_entry_count != 1U) ||
        (storage.synthetic_irq_notify_status != (uint32_t)R3_WORKER_TASKS_OK))
    {
        g_r3_w2_hw_result.invariant_bits |= R3_W2_HW_INV_CASE_COUNTERS;
    }
#elif (R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T03_D)
    if ((worker.processing_cancel_count != 0U) ||
        (worker.processing_complete_count != 1U) ||
        (authority.take_count != 1U) ||
        (authority.claim_count != 1U) ||
        (authority.complete_count != 1U) ||
        (storage.claim_hook_count != 1U) ||
        (storage.process_callback_count != 1U) ||
        (storage.synthetic_irq_entry_count != 1U) ||
        (storage.synthetic_irq_notify_status != (uint32_t)R3_WORKER_TASKS_OK))
    {
        g_r3_w2_hw_result.invariant_bits |= R3_W2_HW_INV_CASE_COUNTERS;
    }
#elif (R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T05_A)
    if ((worker.interference_segment_count != 0U) ||
        (storage.interference_callback_count != 0U) ||
        (storage.interference_release_hook_count != 1U) ||
        (storage.synthetic_irq_entry_count != 0U))
    {
        g_r3_w2_hw_result.invariant_bits |= R3_W2_HW_INV_CASE_COUNTERS;
    }
#elif (R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T05_B)
    if ((worker.interference_segment_count != 1U) ||
        (storage.interference_callback_count != 1U) ||
        (storage.interference_release_hook_count != 1U) ||
        (storage.synthetic_irq_entry_count != 0U))
    {
        g_r3_w2_hw_result.invariant_bits |= R3_W2_HW_INV_CASE_COUNTERS;
    }
#endif

    if (((R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T03_C) ||
         (R3_W2_HW_CASE_ID == R3_W2_HW_CASE_T03_D)) &&
        ((storage.synthetic_irq_entry_count != 1U) ||
         (storage.synthetic_irq_notify_status != (uint32_t)R3_WORKER_TASKS_OK)))
    {
        g_r3_w2_hw_result.invariant_bits |= R3_W2_HW_INV_ISR_PATH;
        SetFirstFault(R3_W2_HW_FAULT_ISR_NOTIFY);
    }

    if (g_r3_w2_hw_result.invariant_bits != 0U)
    {
        SetFirstFault(R3_W2_HW_FAULT_CASE_INVARIANT);
    }
}

static void PublishTerminal(uint32_t terminal_code)
{
    NVIC_DisableIRQ(TIM6_DAC_IRQn);
    NVIC_ClearPendingIRQ(TIM6_DAC_IRQn);

    g_r3_w2_hw_result.terminal_code = terminal_code;
    g_r3_w2_hw_result.processing_ack_observed =
        storage.processing_ack_observed;
    g_r3_w2_hw_result.interference_ack_observed =
        storage.interference_ack_observed;
    __DMB();
    g_r3_w2_hw_result.completed_magic = R3_W2_HW_COMPLETED_MAGIC;
}

static void StableTerminalLoop(void)
{
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

static void ControllerTask(void *argument)
{
    int case_ok;
    (void)argument;

    if (!WaitForWorkersReady())
    {
        SetFirstFault(R3_W2_HW_FAULT_WORKER_READY_TIMEOUT);
        PublishTerminal(R3_W2_HW_TERMINAL_FAIL);
        StableTerminalLoop();
    }

    if (R3WorkerTasks_NotifyStart() != R3_WORKER_TASKS_OK)
    {
        SetFirstFault(R3_W2_HW_FAULT_START_NOTIFY);
        PublishTerminal(R3_W2_HW_TERMINAL_FAIL);
        StableTerminalLoop();
    }
    if (!WaitForRunBound())
    {
        SetFirstFault(R3_W2_HW_FAULT_RUN_BOUND_TIMEOUT);
        PublishTerminal(R3_W2_HW_TERMINAL_FAIL);
        StableTerminalLoop();
    }

    case_ok = RunSelectedCase();
    EvaluateInvariants();
    if ((case_ok == 0) || (g_r3_w2_hw_result.invariant_bits != 0U) ||
        (g_r3_w2_hw_result.first_fault != (uint32_t)R3_W2_HW_FAULT_NONE))
    {
        PublishTerminal(R3_W2_HW_TERMINAL_FAIL);
    }
    else
    {
        PublishTerminal(R3_W2_HW_TERMINAL_PASS);
    }
    StableTerminalLoop();
}

void TIM6_DAC_IRQHandler(void)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    R3WorkerTasksStatus status;

    ++storage.synthetic_irq_entry_count;
    status = R3WorkerTasks_NotifyProcessingWorkFromISR(
        &higher_priority_task_woken);
    storage.synthetic_irq_notify_status = (uint32_t)status;
    if (status != R3_WORKER_TASKS_OK)
    {
        g_r3_w2_hw_result.invariant_bits |= R3_W2_HW_INV_ISR_PATH;
        if (g_r3_w2_hw_result.first_fault ==
            (uint32_t)R3_W2_HW_FAULT_NONE)
        {
            g_r3_w2_hw_result.first_fault =
                (uint32_t)R3_W2_HW_FAULT_ISR_NOTIFY;
        }
    }
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static void InitializeResultAndControl(void)
{
    (void)memset(&storage, 0, sizeof(storage));
    (void)memset((void *)&g_r3_w2_hw_result, 0, sizeof(g_r3_w2_hw_result));

    g_r3_w2_hw_result.magic = R3_W2_HW_RESULT_MAGIC;
    g_r3_w2_hw_result.schema_version = R3_W2_HW_RESULT_SCHEMA;
    g_r3_w2_hw_result.case_id = R3_W2_HW_CASE_ID;
    g_r3_w2_hw_result.terminal_code = R3_W2_HW_TERMINAL_RUNNING;
    g_r3_w2_hw_result.first_fault = R3_W2_HW_FAULT_NONE;
    g_r3_w2_hw_result.boot_id = R3_W2_HW_BOOT_ID;
    g_r3_w2_hw_result.run_id = R3_W2_HW_RUN_ID;
    g_r3_w2_hw_result.generation = R3_W2_HW_GENERATION;
    g_r3_w2_hw_result.stop_id = R3_W2_HW_STOP_ID;

    storage.control.run_valid = 1U;
    storage.control.run.boot_id = R3_W2_HW_BOOT_ID;
    storage.control.run.run_id = R3_W2_HW_RUN_ID;
    storage.control.run.generation = R3_W2_HW_GENERATION;
    storage.control.stream_ticket.identity.boot_id = R3_W2_HW_BOOT_ID;
    storage.control.stream_ticket.identity.run_id = R3_W2_HW_RUN_ID;
    storage.control.stream_ticket.identity.generation = R3_W2_HW_GENERATION;
    storage.control.processing_claim_allowed = 1U;
    storage.control.interference_release_allowed = 1U;
}

static void PreSchedulerFail(R3W2HwFault fault)
{
    SetFirstFault(fault);
    PublishTerminal(R3_W2_HW_TERMINAL_FAIL);
    __disable_irq();
    for (;;)
    {
        __NOP();
    }
}

void R3_W2_HW_Start(void)
{
    R3WorkerTasksConfig config;

    InitializeResultAndControl();
    if (R3_W2_HW_AuthorityShim_Reset(&storage.control.stream_ticket) !=
        STREAM_RUN_AUTHORITY_OK)
    {
        PreSchedulerFail(R3_W2_HW_FAULT_AUTHORITY_RESET);
    }

    NVIC_DisableIRQ(TIM6_DAC_IRQn);
    NVIC_ClearPendingIRQ(TIM6_DAC_IRQn);
    NVIC_SetPriority(TIM6_DAC_IRQn, 5U);
    NVIC_EnableIRQ(TIM6_DAC_IRQn);

    storage.controller_task = xTaskCreateStatic(
        ControllerTask,
        "R3W2Ctrl",
        R3_W2_HW_CONTROLLER_STACK_WORDS,
        NULL,
        R3_W2_HW_CONTROLLER_PRIORITY,
        controller_stack,
        &controller_tcb);
    if (storage.controller_task == NULL)
    {
        PreSchedulerFail(R3_W2_HW_FAULT_CONTROLLER_CREATE);
    }

    (void)memset(&config, 0, sizeof(config));
    config.communication_task = storage.controller_task;
    config.processing_ack_notify_bit = R3_W2_HW_ACK_PROCESSING_BIT;
    config.interference_ack_notify_bit = R3_W2_HW_ACK_INTERFERENCE_BIT;
    config.hooks.context = &storage;
    config.hooks.read_control = ReadControl;
    config.hooks.try_claim_held = TryClaimHeld;
    config.hooks.try_begin_interference = TryBeginInterference;
    config.hooks.process_block = ProcessBlock;
    config.hooks.interference_segment = InterferenceSegment;

    if (R3WorkerTasks_Create(&config) != R3_WORKER_TASKS_OK)
    {
        PreSchedulerFail(R3_W2_HW_FAULT_WORKER_CREATE);
    }

    vTaskStartScheduler();
    PreSchedulerFail(R3_W2_HW_FAULT_SCHEDULER_RETURNED);
}
