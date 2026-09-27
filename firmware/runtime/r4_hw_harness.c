#include "r4_hw_harness.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "r4_runtime_target.h"
#include "r4_tick_service_target.h"
#include "r3_w3_runtime.h"

#define R4_HW_STACK_WORDS 1280U
#define R4_HW_BOOT UINT32_C(0x52340001)
#define R4_HW_COMPLETE_BUDGET_CYCLES 1800U
#define R4_HW_TICK_CYCLES UINT64_C(180000)
#define R4_HW_TICK_INTERVAL_LIMIT_CYCLES UINT64_C(270000)
#ifndef R4_HW_SOAK_MS
#define R4_HW_SOAK_MS 0U
#endif

volatile R4HwHarnessResult g_r4_hw_result;
static StaticTask_t harness_tcb;
static StackType_t harness_stack[R4_HW_STACK_WORDS];
static StaticTask_t idle_tcb;
static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
static volatile uint32_t tick_start_callback_count;
static volatile uint32_t tick_release_callback_count;

static int HarnessTickStart(uint64_t service_seq, void *context)
{
    (void)service_seq;
    (void)context;
    ++tick_start_callback_count;
    return 1;
}

static int HarnessTickRelease(uint64_t planned_service_seq,
    uint64_t actual_cycle, void *context)
{
    (void)planned_service_seq;
    (void)actual_cycle;
    (void)context;
    ++tick_release_callback_count;
    return 1;
}

static void HarnessTask(void *argument)
{
    const R4_RuntimeLedger *ledger;
    R4_CompletionTimingSnapshot timing;
    R3W3RuntimeConfig config;
    R3LifecycleStartRequest start;
    R3LifecycleStartTicket ticket;
    R3LifecycleStopRequest stop;
    R4_TickService tick_snapshot;
    R4_TickServiceTiming tick_timing;
    R4_DmaTailSnapshot dma_tail;
    uint64_t soak_now;
    R4_TickServiceTargetCallbacks tick_callbacks;

    (void)argument;
    g_r4_hw_result.window_open_status = (uint32_t)R4_RuntimeTarget_OpenWindow(0U);
    g_r4_hw_result.tick_snapshot_status = (uint32_t)
        R4_TickServiceTarget_GetSnapshot(&tick_snapshot);
    tick_callbacks.commit_start = HarnessTickStart;
    tick_callbacks.release_job = HarnessTickRelease;
    tick_callbacks.context = NULL;
    g_r4_hw_result.tick_register_status = (uint32_t)
        R4_TickServiceTarget_Register(&tick_callbacks);
    g_r4_hw_result.tick_timing_configure_status = (uint32_t)
        R4_TickServiceTarget_ConfigureTiming(R4_HW_TICK_CYCLES,
            R4_HW_TICK_INTERVAL_LIMIT_CYCLES);
    if (g_r4_hw_result.tick_register_status == (uint32_t)R4_TICK_SERVICE_OK)
    {
        g_r4_hw_result.tick_arm_status = (uint32_t)
            R4_TickServiceTarget_ArmStart(tick_snapshot.service_seq + 2U, 2U);
    }
    else
    {
        g_r4_hw_result.tick_arm_status = (uint32_t)R4_TICK_SERVICE_INVALID_STATE;
    }
    if (g_r4_hw_result.tick_arm_status == (uint32_t)R4_TICK_SERVICE_OK)
    {
        uint32_t suspended_at = DWT->CYCCNT;

        /* T15 target case: hold the scheduler past q0 and two release slots.
         * SysTick still enters its real hook; only task dispatch is delayed.
         * No tick is manufactured when xTaskResumeAll() processes its backlog. */
        vTaskSuspendAll();
        while ((uint32_t)(DWT->CYCCNT - suspended_at) < UINT32_C(1080000))
        {
        }
        (void)xTaskResumeAll();
        if (R4_TickServiceTarget_GetSnapshot(&tick_snapshot) ==
            R4_TICK_SERVICE_OK)
        {
            g_r4_hw_result.tick_service_seq_after_suspension =
                tick_snapshot.service_seq;
        }
    }
    ledger = R4_RuntimeTarget_GetLedger();
    if (ledger != NULL)
    {
        g_r4_hw_result.t17_serial_before = ledger->event_serial;
    }
    g_r4_hw_result.t17_arm_status =
        (uint32_t)R4_RuntimeTarget_TestArmPendingIrq();
    g_r4_hw_result.t17_checkpoint_status =
        (uint32_t)R4_RuntimeTarget_Checkpoint();
    ledger = R4_RuntimeTarget_GetLedger();
    if (ledger != NULL)
    {
        /* This includes exactly the checkpoint plus TIM6 enter/exit; other
         * scheduling events may exist but can never make the delta smaller. */
        g_r4_hw_result.t17_serial_after_pending_irq = ledger->event_serial;
    }
    (void)memset(&config, 0, sizeof(config));
    config.boot_id = R4_HW_BOOT;
    config.k = 4U;
    g_r4_hw_result.lifecycle_init_status = (uint32_t)R3W3Runtime_Initialize(&config);
    if (g_r4_hw_result.lifecycle_init_status == (uint32_t)R3_W3_RUNTIME_OK)
    {
        start.boot_id = R4_HW_BOOT;
        start.request_id = 1U;
        start.generation = 1U;
        start.configuration_id = 4U;
        g_r4_hw_result.lifecycle_start_status =
            (uint32_t)R3W3Runtime_Start(&start, &ticket);
        if (g_r4_hw_result.lifecycle_start_status == (uint32_t)R3_W3_RUNTIME_OK)
        {
            ledger = R4_RuntimeTarget_GetLedger();
            g_r4_hw_result.soak_configured_ms = R4_HW_SOAK_MS;
            if (ledger != NULL)
            {
                g_r4_hw_result.soak_start_clock_high_word = ledger->clock->high_word;
            }
            (void)R4_RuntimeTarget_ReadNow(&soak_now);
            g_r4_hw_result.soak_start_cycle = soak_now;
#if R4_HW_SOAK_MS > 0U
            vTaskDelay(pdMS_TO_TICKS(R4_HW_SOAK_MS));
#endif
            (void)R4_RuntimeTarget_ReadNow(&soak_now);
            g_r4_hw_result.soak_end_cycle = soak_now;
            ledger = R4_RuntimeTarget_GetLedger();
            if (ledger != NULL)
            {
                g_r4_hw_result.soak_end_clock_high_word = ledger->clock->high_word;
            }
            g_r4_hw_result.dma_tail_snapshot_status = (uint32_t)
                R4_RuntimeTarget_GetDmaTailSnapshot(&dma_tail);
            g_r4_hw_result.dma_irq_count = dma_tail.dma_irq_count;
            g_r4_hw_result.dma_yield_requested_count =
                dma_tail.dma_yield_requested_count;
            g_r4_hw_result.dma_no_yield_count = dma_tail.dma_no_yield_count;
            vTaskDelay(pdMS_TO_TICKS(20U));
            stop.stream_ticket = ticket.stream_ticket;
            stop.stop_id = UINT32_C(0x52340002);
            g_r4_hw_result.lifecycle_stop_status =
                (uint32_t)R3W3Runtime_Stop(&stop);
            /* T12's no-switch arm: the real DMA vector is pended only after
             * sampling is stopped and with no DMA status bit set.  It is not
             * a fabricated completion; it proves the common handler's
             * no-event tail still emits exactly one IRQ exit and requests no
             * scheduler switch. */
            if (g_r4_hw_result.lifecycle_stop_status == (uint32_t)R3_W3_RUNTIME_OK)
            {
                g_r4_hw_result.dma_no_event_snapshot_status = (uint32_t)
                    R4_RuntimeTarget_GetDmaTailSnapshot(&dma_tail);
                g_r4_hw_result.dma_no_event_irq_before = dma_tail.dma_irq_count;
                g_r4_hw_result.dma_no_event_no_yield_before =
                    dma_tail.dma_no_yield_count;
                NVIC_SetPendingIRQ(DMA2_Stream0_IRQn);
                vTaskDelay(pdMS_TO_TICKS(2U));
                g_r4_hw_result.dma_no_event_snapshot_status = (uint32_t)
                    R4_RuntimeTarget_GetDmaTailSnapshot(&dma_tail);
                g_r4_hw_result.dma_no_event_irq_after = dma_tail.dma_irq_count;
                g_r4_hw_result.dma_no_event_no_yield_after =
                    dma_tail.dma_no_yield_count;
            }
        }
    }
    else
    {
        g_r4_hw_result.lifecycle_start_status = (uint32_t)R3_W3_RUNTIME_INVALID_STATE;
        g_r4_hw_result.lifecycle_stop_status = (uint32_t)R3_W3_RUNTIME_INVALID_STATE;
    }
    g_r4_hw_result.checkpoint_status = (uint32_t)R4_RuntimeTarget_Checkpoint();
    g_r4_hw_result.window_close_status = (uint32_t)R4_RuntimeTarget_CloseWindow(0U);
    ledger = R4_RuntimeTarget_GetLedger();
    if (ledger != NULL)
    {
        g_r4_hw_result.t17_window_cycles_at_close = ledger->window_cycles[0];
    }
    /* A post-close settlement may update global ownership totals, but never
     * the sealed window field. */
    g_r4_hw_result.t17_post_close_status =
        (uint32_t)R4_RuntimeTarget_Checkpoint();
    g_r4_hw_result.tick_snapshot_status = (uint32_t)
        R4_TickServiceTarget_GetSnapshot(&tick_snapshot);
    g_r4_hw_result.tick_start_callback_count = tick_start_callback_count;
    g_r4_hw_result.tick_release_callback_count = tick_release_callback_count;
    g_r4_hw_result.tick_service_seq = tick_snapshot.service_seq;
    g_r4_hw_result.tick_start_count = tick_snapshot.start_count;
    g_r4_hw_result.tick_release_count = tick_snapshot.release_count;
    g_r4_hw_result.tick_skipped_count = tick_snapshot.skipped_count;
    g_r4_hw_result.tick_timing_snapshot_status = (uint32_t)
        R4_TickServiceTarget_GetTiming(&tick_timing);
    g_r4_hw_result.tick_timing_service_count = tick_timing.service_count;
    g_r4_hw_result.tick_timing_max_interval_cycles = tick_timing.max_interval_cycles;
    g_r4_hw_result.tick_timing_max_phase_error_cycles =
        tick_timing.max_phase_error_cycles;
    g_r4_hw_result.tick_timing_over_limit_count =
        tick_timing.over_limit_interval_count;
    ledger = R4_RuntimeTarget_GetLedger();
    if (ledger != NULL)
    {
        g_r4_hw_result.t17_window_cycles_after_close = ledger->window_cycles[0];
        g_r4_hw_result.runtime_status = (uint32_t)R4_RuntimeLedger_GetStatus(ledger);
        g_r4_hw_result.event_serial = ledger->event_serial;
        g_r4_hw_result.last_time = ledger->last_time;
        g_r4_hw_result.task_cycles = ledger->task_cycles;
        g_r4_hw_result.irq_cycles = ledger->irq_cycles;
        g_r4_hw_result.window_cycles = ledger->window_cycles[0];
        g_r4_hw_result.irq_depth = ledger->irq_depth;
    }
    if (R4_RuntimeTarget_GetCompletionTiming(&timing) == R4_RUNTIME_OK)
    {
        g_r4_hw_result.completion_malformed_count = timing.malformed_count;
        g_r4_hw_result.completion_discarded_count = timing.discarded_count;
        g_r4_hw_result.completion_count = timing.completed_count;
        g_r4_hw_result.completion_lock_count = timing.lock_count;
        g_r4_hw_result.completion_commit_count = timing.commit_count;
        g_r4_hw_result.completion_unlock_count = timing.unlock_count;
        g_r4_hw_result.completion_max_total_cycles = timing.max_total_cycles;
        g_r4_hw_result.completion_max_prefix_cycles = timing.max_prefix_cycles;
        g_r4_hw_result.completion_max_suffix_cycles = timing.max_suffix_cycles;
        g_r4_hw_result.completion_budget_pass =
            (timing.completed_count != 0U) && (timing.malformed_count == 0U) &&
            (timing.max_total_cycles <= R4_HW_COMPLETE_BUDGET_CYCLES) ? 1U : 0U;
    }
    /* Deliberately repeat the already-completed TIM6 exit.  This is final:
     * RuntimeEvent correctly latches the fault and no later event may hide it. */
    g_r4_hw_result.t17_duplicate_exit_status =
        (uint32_t)R4_RuntimeTarget_TestInjectDuplicateExit(
            (uint32_t)TIM6_DAC_IRQn + 16U);
    __DMB();
    g_r4_hw_result.completed_magic = R4_HW_COMPLETE;
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

void R4_HW_Start(void)
{
    TaskHandle_t task;

    (void)memset((void *)&g_r4_hw_result, 0, sizeof(g_r4_hw_result));
    g_r4_hw_result.magic = R4_HW_MAGIC;
    g_r4_hw_result.init_status = (uint32_t)R4_RuntimeTarget_Initialize();
    if (g_r4_hw_result.init_status != (uint32_t)R4_RUNTIME_OK)
    {
        __DMB();
        g_r4_hw_result.completed_magic = R4_HW_COMPLETE;
        return;
    }
    task = xTaskCreateStatic(HarnessTask, "R4HW", R4_HW_STACK_WORDS, NULL,
        tskIDLE_PRIORITY + 3U, harness_stack, &harness_tcb);
    if (task == NULL)
    {
        g_r4_hw_result.runtime_status = (uint32_t)R4_RUNTIME_PLATFORM_ERROR;
        __DMB();
        g_r4_hw_result.completed_magic = R4_HW_COMPLETE;
        return;
    }
    vTaskStartScheduler();
    g_r4_hw_result.runtime_status = (uint32_t)R4_RUNTIME_PLATFORM_ERROR;
    __DMB();
    g_r4_hw_result.completed_magic = R4_HW_COMPLETE;
}

void vApplicationGetIdleTaskMemory(
    StaticTask_t **ppxIdleTaskTCBBuffer,
    StackType_t **ppxIdleTaskStackBuffer,
    configSTACK_DEPTH_TYPE *puxIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &idle_tcb;
    *ppxIdleTaskStackBuffer = idle_stack;
    *puxIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}
