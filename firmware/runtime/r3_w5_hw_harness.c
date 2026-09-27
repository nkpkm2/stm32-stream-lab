#include "r3_w5_hw_harness.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "r3_w3_runtime.h"

#ifndef R3_W5_HW_CASE_ID
#error "R3_W5_HW_CASE_ID must be supplied by CMake"
#endif

#define R3_W5_BOOT_ID 0x35570001UL
#define R3_W5_CONTROLLER_STACK_WORDS 1024U
#define R3_W5_CONTROLLER_PRIORITY (tskIDLE_PRIORITY + 4U)
#define R3_W5_STOP_ONE 0x00000051UL
#define R3_W5_STOP_TWO 0x00000052UL

#define INV_INIT (1UL << 0)
#define INV_OPERATION (1UL << 1)
#define INV_IDENTITY (1UL << 2)
#define INV_RESULT (1UL << 3)

volatile R3W5HwResult g_r3_w5_hw_result;
static StaticTask_t controller_tcb;
static StackType_t controller_stack[R3_W5_CONTROLLER_STACK_WORDS];

static R3LifecycleStartRequest StartRequest(uint32_t id, uint32_t generation)
{
    R3LifecycleStartRequest request;
    (void)memset(&request, 0, sizeof(request));
    request.boot_id = R3_W5_BOOT_ID;
    request.request_id = id;
    request.generation = generation;
    request.configuration_id = 1U;
    return request;
}

static R3LifecycleStopRequest StopRequest(R3LifecycleStartTicket ticket, uint32_t stop_id)
{
    R3LifecycleStopRequest request;
    request.stream_ticket = ticket.stream_ticket;
    request.stop_id = stop_id;
    return request;
}

static int Capture(void)
{
    R3W3RuntimeSnapshot snapshot;
    if (R3W3Runtime_GetSnapshot(&snapshot) != R3_W3_RUNTIME_OK) return 0;
    g_r3_w5_hw_result.lifecycle_state = (uint32_t)snapshot.lifecycle.state;
    g_r3_w5_hw_result.ledger_phase = (uint32_t)snapshot.command_ledger.phase;
    g_r3_w5_hw_result.ledger_run_id = snapshot.command_ledger.binding.request_id;
    g_r3_w5_hw_result.ledger_generation = snapshot.command_ledger.binding.generation;
    g_r3_w5_hw_result.start_ticket = snapshot.command_ledger.start_ticket.start_ticket;
    g_r3_w5_hw_result.commit_count = snapshot.lifecycle.commit_count;
    g_r3_w5_hw_result.rollback_count = snapshot.lifecycle.rollback_count;
    g_r3_w5_hw_result.result_valid = snapshot.result_store.valid;
    g_r3_w5_hw_result.result_references = snapshot.result_store.reference_count;
    g_r3_w5_hw_result.result_id = snapshot.result_store.result_id;
    g_r3_w5_hw_result.worker_faulted = snapshot.workers.faulted;
    g_r3_w5_hw_result.runtime_fault = snapshot.runtime_fault;
    return snapshot.workers.faulted == 0U && snapshot.runtime_fault == 0U;
}

static void Publish(uint32_t terminal)
{
    g_r3_w5_hw_result.terminal_code = terminal;
    __DMB();
    g_r3_w5_hw_result.completed_magic = R3_W5_HW_COMPLETED_MAGIC;
}

static void TerminalLoop(void)
{
    for (;;) { vTaskDelay(pdMS_TO_TICKS(1000U)); }
}

static int StartRun(uint32_t id, uint32_t generation, R3LifecycleStartTicket *ticket)
{
    R3LifecycleStartRequest request = StartRequest(id, generation);
    return R3W3Runtime_Start(&request, ticket) == R3_W3_RUNTIME_OK;
}

static int StopRun(R3LifecycleStartTicket ticket, uint32_t stop_id)
{
    R3LifecycleStopRequest request = StopRequest(ticket, stop_id);
    return R3W3Runtime_Stop(&request) == R3_W3_RUNTIME_OK;
}

static void ControllerTask(void *argument)
{
    R3W3RuntimeConfig config;
    R3LifecycleStartTicket one;
    R3LifecycleStartTicket two;
    R3LifecycleStartTicket replay;
    R3LifecycleStopRequest stop;
    R3LifecycleStartRequest old;
    const uint8_t *bytes;
    uint32_t size;
    uint8_t copy[R3_RESULT_STORE_BYTES];
    (void)argument;
    /* Every target selector is compiled with -Werror; these declarations are
     * shared by the six selector branches below. */
    (void)two;
    (void)replay;
    (void)stop;
    (void)old;
    (void)bytes;
    (void)size;
    (void)copy;
    (void)memset(&config, 0, sizeof(config));
    config.boot_id = R3_W5_BOOT_ID;
    config.k = 1U;
    if (R3W3Runtime_Initialize(&config) != R3_W3_RUNTIME_OK)
    {
        g_r3_w5_hw_result.invariant_bits |= INV_INIT;
        Publish(2U); TerminalLoop();
    }

#if (R3_W5_HW_CASE_ID == R3_W5_HW_CASE_T13_A)
    if (!StartRun(0x41U, 1U, &one)) g_r3_w5_hw_result.invariant_bits |= INV_OPERATION;
    if (StartRun(0x41U, 1U, &replay) == 0 ||
        replay.start_ticket != one.start_ticket ||
        replay.stream_ticket.identity.generation != one.stream_ticket.identity.generation)
        g_r3_w5_hw_result.invariant_bits |= INV_IDENTITY;
    g_r3_w5_hw_result.duplicate_ticket_match =
        replay.start_ticket == one.start_ticket ? 1U : 0U;
    if (!StopRun(one, R3_W5_STOP_ONE)) g_r3_w5_hw_result.invariant_bits |= INV_OPERATION;
#elif (R3_W5_HW_CASE_ID == R3_W5_HW_CASE_T13_C)
    if (!StartRun(0x41U, 1U, &one)) g_r3_w5_hw_result.invariant_bits |= INV_OPERATION;
    if (!StopRun(one, R3_W5_STOP_ONE) || !StopRun(one, R3_W5_STOP_ONE))
        g_r3_w5_hw_result.invariant_bits |= INV_OPERATION;
#elif (R3_W5_HW_CASE_ID == R3_W5_HW_CASE_T13_D)
    old = StartRequest(0x41U, 1U);
    old.boot_id--;
    g_r3_w5_hw_result.old_boot_status = (uint32_t)R3W3Runtime_Start(&old, &one);
    if (g_r3_w5_hw_result.old_boot_status != R3_W3_RUNTIME_STALE_COMMAND ||
        !StartRun(0x41U, 1U, &one) || !StopRun(one, R3_W5_STOP_ONE))
        g_r3_w5_hw_result.invariant_bits |= INV_IDENTITY;
#elif (R3_W5_HW_CASE_ID == R3_W5_HW_CASE_T13_G)
    if (!StartRun(0x41U, 1U, &one) || !StopRun(one, R3_W5_STOP_ONE) ||
        !StartRun(0x42U, 2U, &two))
        g_r3_w5_hw_result.invariant_bits |= INV_OPERATION;
    /* Communication outranks workers.  The preceding STOP ACK may have
     * resumed this task before its sending worker returned; yield only after
     * the new run has been installed, then prove the old path cannot corrupt it. */
    vTaskDelay(1U);
    if (!StopRun(two, R3_W5_STOP_TWO)) g_r3_w5_hw_result.invariant_bits |= INV_OPERATION;
#elif (R3_W5_HW_CASE_ID == R3_W5_HW_CASE_T20_A)
    if (!StartRun(0x41U, 1U, &one) || !StopRun(one, R3_W5_STOP_ONE) ||
        R3W3Runtime_AcquireResult(R3_W5_STOP_ONE, &bytes, &size) != R3_W3_RUNTIME_OK ||
        size != R3_RESULT_STORE_BYTES)
        g_r3_w5_hw_result.invariant_bits |= INV_OPERATION;
    else
    {
        (void)memcpy(copy, bytes, sizeof(copy));
        g_r3_w5_hw_result.primary_status = (uint32_t)R3W3Runtime_Start(
            &(R3LifecycleStartRequest){R3_W5_BOOT_ID, 0x42U, 2U, 1U}, &two);
        if (g_r3_w5_hw_result.primary_status != R3_W3_RUNTIME_RESULT_BUSY ||
            memcmp(copy, bytes, sizeof(copy)) != 0)
            g_r3_w5_hw_result.invariant_bits |= INV_RESULT;
        else g_r3_w5_hw_result.bytes_immutable = 1U;
    }
#elif (R3_W5_HW_CASE_ID == R3_W5_HW_CASE_T20_B)
    if (!StartRun(0x41U, 1U, &one) || !StopRun(one, R3_W5_STOP_ONE) ||
        R3W3Runtime_AcquireResult(R3_W5_STOP_ONE, &bytes, &size) != R3_W3_RUNTIME_OK ||
        R3W3Runtime_ReleaseResult(R3_W5_STOP_ONE) != R3_W3_RUNTIME_OK ||
        !StartRun(0x42U, 2U, &two) || !StopRun(two, R3_W5_STOP_TWO))
        g_r3_w5_hw_result.invariant_bits |= INV_OPERATION;
#else
#error "unsupported R3 W5 target case"
#endif
    if (!Capture()) g_r3_w5_hw_result.invariant_bits |= INV_OPERATION;
    if ((g_r3_w5_hw_result.lifecycle_state != R3_LIFECYCLE_IDLE) ||
        (g_r3_w5_hw_result.ledger_phase != R3_COMMAND_LEDGER_STOPPED))
        g_r3_w5_hw_result.invariant_bits |= INV_IDENTITY;
    Publish(g_r3_w5_hw_result.invariant_bits == 0U ? 1U : 2U);
    TerminalLoop();
}

void R3_W5_HW_Start(void)
{
    TaskHandle_t controller;
    (void)memset((void *)&g_r3_w5_hw_result, 0, sizeof(g_r3_w5_hw_result));
    g_r3_w5_hw_result.magic = R3_W5_HW_RESULT_MAGIC;
    g_r3_w5_hw_result.schema_version = R3_W5_HW_RESULT_SCHEMA;
    g_r3_w5_hw_result.case_id = R3_W5_HW_CASE_ID;
    controller = xTaskCreateStatic(ControllerTask, "R3W5Ctrl",
        R3_W5_CONTROLLER_STACK_WORDS, NULL, R3_W5_CONTROLLER_PRIORITY,
        controller_stack, &controller_tcb);
    if (controller == NULL)
    {
        g_r3_w5_hw_result.invariant_bits |= INV_INIT;
        Publish(2U); __disable_irq(); for (;;) { __NOP(); }
    }
    vTaskStartScheduler();
    g_r3_w5_hw_result.invariant_bits |= INV_INIT;
    Publish(2U); __disable_irq(); for (;;) { __NOP(); }
}
