#include "r3_w6_hw_harness.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "r3_w3_runtime.h"

#ifndef R3_W6_HW_ANCHOR
#error "R3_W6_HW_ANCHOR must be supplied by CMake"
#endif

#define W6_BOOT 0x36570001UL
#define W6_STACK_WORDS 1280U
#define W6_PRIORITY (tskIDLE_PRIORITY + 4U)
#define W6_STOP_BASE 0x60000000UL

volatile R3W6HwResult g_r3_w6_hw_result;
static StaticTask_t controller_tcb;
static StackType_t controller_stack[W6_STACK_WORDS];

static R3LifecycleStartRequest StartRequest(uint32_t index)
{
    R3LifecycleStartRequest r;
    r.boot_id = W6_BOOT;
    r.request_id = index;
    r.generation = index;
    r.configuration_id = R3_W6_HW_ANCHOR == 1U ? 8U : 1U;
    return r;
}

static void StopRequest(R3LifecycleStopRequest *out, R3LifecycleStartTicket ticket,
    uint32_t index)
{
    out->stream_ticket = ticket.stream_ticket;
    out->stop_id = W6_STOP_BASE + index;
}

static void Record(uint32_t index, R3W3RuntimeStatus start_status,
    R3W3RuntimeStatus stop_status, const R3LifecycleStartTicket *ticket)
{
    R3W3RuntimeSnapshot snapshot;
    R3W6HwCycle *cycle = (R3W6HwCycle *)&g_r3_w6_hw_result.cycles[index - 1U];
    (void)memset(cycle, 0, sizeof(*cycle));
    cycle->cycle_index = index;
    cycle->run_id = ticket->stream_ticket.identity.run_id;
    cycle->generation = ticket->stream_ticket.identity.generation;
    cycle->stop_id = W6_STOP_BASE + index;
    cycle->start_status = (uint32_t)start_status;
    cycle->stop_status = (uint32_t)stop_status;
    if (R3W3Runtime_GetSnapshot(&snapshot) != R3_W3_RUNTIME_OK)
    {
        ++g_r3_w6_hw_result.fault_cycles;
        return;
    }
    cycle->lifecycle_state = (uint32_t)snapshot.lifecycle.state;
    cycle->dma_hardware_owned = snapshot.driver.hardware_owned;
    cycle->ack_mask = snapshot.rollback_ack_mask;
    cycle->ownership_active = snapshot.ownership.initialized;
    cycle->worker_faulted = snapshot.workers.faulted;
    cycle->runtime_fault = snapshot.runtime_fault;
    g_r3_w6_hw_result.total_keep += snapshot.driver.inactive_keep_success_count;
    g_r3_w6_hw_result.total_rebind += snapshot.driver.inactive_rebind_success_count;
    if ((start_status != R3_W3_RUNTIME_OK) || (stop_status != R3_W3_RUNTIME_OK) ||
        (cycle->lifecycle_state != R3_LIFECYCLE_IDLE) ||
        (cycle->dma_hardware_owned != 0U) || (cycle->ack_mask != 0x300U) ||
        (cycle->worker_faulted != 0U) || (cycle->runtime_fault != 0U))
    {
        ++g_r3_w6_hw_result.fault_cycles;
    }
}

static void ControllerTask(void *argument)
{
    R3W3RuntimeConfig config;
    uint32_t index;
    (void)argument;
    (void)memset(&config, 0, sizeof(config));
    config.boot_id = W6_BOOT;
    config.k = R3_W6_HW_ANCHOR == 1U ? 8U : 1U;
    config.suppress_processing_notify = R3_W6_HW_ANCHOR == 2U ? 1U : 0U;
    if (R3W3Runtime_Initialize(&config) != R3_W3_RUNTIME_OK)
    {
        g_r3_w6_hw_result.fault_cycles = R3_W6_HW_CYCLES;
        g_r3_w6_hw_result.terminal = 2U;
        __DMB(); g_r3_w6_hw_result.completed_magic = R3_W6_HW_COMPLETE;
        for (;;) { vTaskDelay(pdMS_TO_TICKS(1000U)); }
    }
    for (index = 1U; index <= R3_W6_HW_CYCLES; ++index)
    {
        R3LifecycleStartRequest start = StartRequest(index);
        R3LifecycleStartTicket ticket;
        R3LifecycleStopRequest stop;
        R3W3RuntimeStatus start_status = R3W3Runtime_Start(&start, &ticket);
        R3W3RuntimeStatus stop_status = R3_W3_RUNTIME_INVALID_STATE;
        if (start_status == R3_W3_RUNTIME_OK)
        {
            /* A normal cycle gives a worker a bounded execution opportunity.
             * Anchor B deliberately withholds notification long enough to make
             * K=1 use its controlled KEEP/drop path. */
            vTaskDelay(pdMS_TO_TICKS(R3_W6_HW_ANCHOR == 1U ? 2U : 8U));
            StopRequest(&stop, ticket, index);
            stop_status = R3W3Runtime_Stop(&stop);
        }
        Record(index, start_status, stop_status, &ticket);
        g_r3_w6_hw_result.completed_cycles = index;
        if (g_r3_w6_hw_result.fault_cycles != 0U) break;
    }
    g_r3_w6_hw_result.terminal =
        (g_r3_w6_hw_result.completed_cycles == R3_W6_HW_CYCLES &&
         g_r3_w6_hw_result.fault_cycles == 0U) ? 1U : 2U;
    __DMB();
    g_r3_w6_hw_result.completed_magic = R3_W6_HW_COMPLETE;
    for (;;) { vTaskDelay(pdMS_TO_TICKS(1000U)); }
}

void R3_W6_HW_Start(void)
{
    TaskHandle_t controller;
    (void)memset((void *)&g_r3_w6_hw_result, 0, sizeof(g_r3_w6_hw_result));
    g_r3_w6_hw_result.magic = R3_W6_HW_MAGIC;
    g_r3_w6_hw_result.schema = 1U;
    g_r3_w6_hw_result.anchor = R3_W6_HW_ANCHOR;
    controller = xTaskCreateStatic(ControllerTask, "R3W6Ctrl", W6_STACK_WORDS,
        NULL, W6_PRIORITY, controller_stack, &controller_tcb);
    if (controller == NULL)
    {
        g_r3_w6_hw_result.terminal = 2U;
        g_r3_w6_hw_result.fault_cycles = R3_W6_HW_CYCLES;
        __DMB(); g_r3_w6_hw_result.completed_magic = R3_W6_HW_COMPLETE;
        __disable_irq(); for (;;) { __NOP(); }
    }
    vTaskStartScheduler();
    g_r3_w6_hw_result.terminal = 2U;
    __DMB(); g_r3_w6_hw_result.completed_magic = R3_W6_HW_COMPLETE;
    __disable_irq(); for (;;) { __NOP(); }
}
