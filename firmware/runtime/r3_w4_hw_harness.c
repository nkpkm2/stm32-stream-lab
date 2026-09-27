#include "r3_w4_hw_harness.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "r3_w3_runtime.h"

#ifndef R3_W4_HW_CASE_ID
#error "R3_W4_HW_CASE_ID must be supplied by CMake"
#endif

#define R3_W4_HW_BOOT_ID 0x34570001UL
#define R3_W4_HW_REQUEST_ID 0x00000041UL
#define R3_W4_HW_STOP_ID 0x00000051UL
#define R3_W4_HW_CONTROLLER_STACK_WORDS 1024U
#define R3_W4_HW_CONTROLLER_PRIORITY (tskIDLE_PRIORITY + 4U)
#define R3_W4_HW_PARTIAL_SPINS 600000U

#define R3_W4_INV_INITIALIZE (1UL << 0)
#define R3_W4_INV_START      (1UL << 1)
#define R3_W4_INV_PARTIAL    (1UL << 2)
#define R3_W4_INV_STOP       (1UL << 3)
#define R3_W4_INV_QUIESCED   (1UL << 4)
#define R3_W4_INV_WORKERS    (1UL << 5)
#define R3_W4_INV_CURRENT    (1UL << 6)

volatile R3W4HwResult g_r3_w4_hw_result;
static StaticTask_t controller_tcb;
static StackType_t controller_stack[R3_W4_HW_CONTROLLER_STACK_WORDS];

static void Capture(const R3W3RuntimeSnapshot *s)
{
    g_r3_w4_hw_result.lifecycle_state = (uint32_t)s->lifecycle.state;
    g_r3_w4_hw_result.acquisition_gate = s->lifecycle.acquisition_publish_allowed;
    g_r3_w4_hw_result.processing_gate = s->lifecycle.processing_claim_allowed;
    g_r3_w4_hw_result.interference_gate = s->lifecycle.interference_release_allowed;
    g_r3_w4_hw_result.driver_state = (uint32_t)s->driver.state;
    g_r3_w4_hw_result.driver_hardware_owned = s->driver.hardware_owned;
    g_r3_w4_hw_result.completion_count = s->driver.completion_count;
    g_r3_w4_hw_result.begin_valid = s->stop_begin_report_valid;
    g_r3_w4_hw_result.begin_captured_samples = s->stop_begin_report.captured_samples;
    g_r3_w4_hw_result.begin_dma_lisr = s->stop_begin_report.dma_lisr_at_begin;
    g_r3_w4_hw_result.begin_tim2_cr1 = s->stop_begin_report.tim2_cr1_after_begin;
    g_r3_w4_hw_result.finish_valid = s->stop_report_valid;
    g_r3_w4_hw_result.finish_dma_cr = s->stop_report.dma_cr_after_stop;
    g_r3_w4_hw_result.rollback_ack_mask = s->rollback_ack_mask;
    g_r3_w4_hw_result.processing_entered = s->processing_entered;
    g_r3_w4_hw_result.processing_complete_count = s->workers.processing_complete_count;
    g_r3_w4_hw_result.processing_cancel_count = s->workers.processing_cancel_count;
    g_r3_w4_hw_result.worker_faulted = s->workers.faulted;
    g_r3_w4_hw_result.worker_first_fault = (uint32_t)s->workers.first_fault;
    g_r3_w4_hw_result.runtime_fault = s->runtime_fault;
    g_r3_w4_hw_result.inactive_keep_count = s->driver.inactive_keep_success_count;
    g_r3_w4_hw_result.inactive_rebind_count = s->driver.inactive_rebind_success_count;
}

static int Snapshot(R3W3RuntimeSnapshot *s)
{
    if (R3W3Runtime_GetSnapshot(s) != R3_W3_RUNTIME_OK)
    {
        return 0;
    }
    Capture(s);
    return s->runtime_fault == 0U && s->workers.faulted == 0U;
}

static void Publish(uint32_t terminal)
{
    g_r3_w4_hw_result.terminal_code = terminal;
    __DMB();
    g_r3_w4_hw_result.completed_magic = R3_W4_HW_COMPLETED_MAGIC;
}

static void TerminalLoop(void)
{
    for (;;) { vTaskDelay(pdMS_TO_TICKS(1000U)); }
}

#if (R3_W4_HW_CASE_ID == R3_W4_HW_CASE_T04_A) || \
    (R3_W4_HW_CASE_ID == R3_W4_HW_CASE_T04_B)
static int WaitForPartial(R3W3RuntimeSnapshot *s)
{
    uint32_t spins;
    for (spins = 0U; spins < R3_W4_HW_PARTIAL_SPINS; ++spins)
    {
        if (!Snapshot(s)) return 0;
        if ((s->driver.completion_count == 0U) &&
            (s->driver.dma_ndtr > 0U) &&
            (s->driver.dma_ndtr < R3_W3_RUNTIME_BLOCK_SAMPLES)) return 1;
    }
    return 0;
}
#endif

#if (R3_W4_HW_CASE_ID == R3_W4_HW_CASE_STOP_B)
static int WaitForProcessingEntry(R3W3RuntimeSnapshot *s)
{
    uint32_t ticks;
    for (ticks = 0U; ticks < 40U; ++ticks)
    {
        /* The controller outranks Processing. Leave a full intervening tick
         * after its wake boundary so the DMA-notified Processing task can run
         * and enter its deliberate bounded hold before we issue STOP. */
        vTaskDelay(pdMS_TO_TICKS(2U));
        if (!Snapshot(s)) return 0;
        if (s->processing_entered != 0U) return 1;
    }
    return 0;
}
#endif

static void ControllerTask(void *argument)
{
    R3W3RuntimeConfig config;
    R3LifecycleStartRequest request;
    R3LifecycleStartTicket ticket;
    R3W3RuntimeSnapshot snapshot;
    (void)argument;
    (void)memset(&config, 0, sizeof(config));
    config.boot_id = R3_W4_HW_BOOT_ID;
    config.k = 1U;
#if (R3_W4_HW_CASE_ID == R3_W4_HW_CASE_STOP_A)
    config.suppress_processing_notify = 1U;
#elif (R3_W4_HW_CASE_ID == R3_W4_HW_CASE_STOP_C)
    config.suppress_processing_notify = 1U;
#elif (R3_W4_HW_CASE_ID == R3_W4_HW_CASE_STOP_B)
    config.processing_hold_ticks = 12U;
#endif
    if (R3W3Runtime_Initialize(&config) != R3_W3_RUNTIME_OK)
    {
        g_r3_w4_hw_result.invariant_bits |= R3_W4_INV_INITIALIZE;
        Publish(R3_W4_HW_TERMINAL_FAIL); TerminalLoop();
    }
    (void)memset(&request, 0, sizeof(request));
    request.boot_id = R3_W4_HW_BOOT_ID;
    request.request_id = R3_W4_HW_REQUEST_ID;
    request.generation = 1U;
    request.configuration_id = config.k;
    if (R3W3Runtime_PrepareStart(&request, &ticket) != R3_W3_RUNTIME_OK ||
        R3W3Runtime_CommitStart(&ticket) != R3_W3_RUNTIME_OK)
    {
        g_r3_w4_hw_result.invariant_bits |= R3_W4_INV_START;
        Publish(R3_W4_HW_TERMINAL_FAIL); TerminalLoop();
    }
#if (R3_W4_HW_CASE_ID == R3_W4_HW_CASE_T04_A)
    if (!WaitForPartial(&snapshot))
        g_r3_w4_hw_result.invariant_bits |= R3_W4_INV_PARTIAL;
#elif (R3_W4_HW_CASE_ID == R3_W4_HW_CASE_STOP_E)
    {
        uint32_t ticks;
        CLEAR_BIT(ADC1->CR2, ADC_CR2_DMA);
        for (ticks = 0U; ticks < 20U; ++ticks)
        {
            vTaskDelay(pdMS_TO_TICKS(1U));
            if (!Snapshot(&snapshot)) break;
            if ((snapshot.driver.adc_sr & ADC_SR_OVR) != 0U)
            {
                g_r3_w4_hw_result.injected_error_observed = 1U;
                break;
            }
        }
        if (g_r3_w4_hw_result.injected_error_observed == 0U)
            g_r3_w4_hw_result.invariant_bits |= R3_W4_INV_PARTIAL;
    }
#elif (R3_W4_HW_CASE_ID == R3_W4_HW_CASE_T04_B)
    HAL_NVIC_DisableIRQ(DMA2_Stream0_IRQn);
    if (!WaitForPartial(&snapshot))
        g_r3_w4_hw_result.invariant_bits |= R3_W4_INV_PARTIAL;
    else
    {
        uint32_t spins;
        for (spins = 0U; spins < R3_W4_HW_PARTIAL_SPINS; ++spins)
        {
            if (!Snapshot(&snapshot)) break;
            if (((snapshot.driver.dma_lisr & DMA_LISR_TCIF0) != 0U) &&
                (snapshot.driver.completion_count == 0U)) break;
        }
        if (((snapshot.driver.dma_lisr & DMA_LISR_TCIF0) == 0U) ||
            (snapshot.driver.completion_count != 0U))
            g_r3_w4_hw_result.invariant_bits |= R3_W4_INV_PARTIAL;
    }
#elif (R3_W4_HW_CASE_ID == R3_W4_HW_CASE_STOP_A)
    vTaskDelay(pdMS_TO_TICKS(8U));
    if (!Snapshot(&snapshot) || (snapshot.authority.publish_count == 0U) ||
        (snapshot.authority.cancel_count != 0U))
        g_r3_w4_hw_result.invariant_bits |= R3_W4_INV_PARTIAL;
#elif (R3_W4_HW_CASE_ID == R3_W4_HW_CASE_STOP_B)
    if (!WaitForProcessingEntry(&snapshot))
        g_r3_w4_hw_result.invariant_bits |= R3_W4_INV_CURRENT;
#elif (R3_W4_HW_CASE_ID == R3_W4_HW_CASE_STOP_C)
    vTaskDelay(pdMS_TO_TICKS(12U));
    if (!Snapshot(&snapshot) || (snapshot.driver.completion_count < 3U) ||
        (snapshot.driver.inactive_rebind_success_count == 0U) ||
        (snapshot.driver.inactive_keep_success_count == 0U))
        g_r3_w4_hw_result.invariant_bits |= R3_W4_INV_PARTIAL;
#endif
#if (R3_W4_HW_CASE_ID == R3_W4_HW_CASE_STOP_E)
    if (R3W3Runtime_StopRunning(R3_W4_HW_STOP_ID) != R3_W3_RUNTIME_RESET_REQUIRED ||
        !Snapshot(&snapshot))
#else
    if (R3W3Runtime_StopRunning(R3_W4_HW_STOP_ID) != R3_W3_RUNTIME_OK ||
        !Snapshot(&snapshot))
#endif
    {
        g_r3_w4_hw_result.invariant_bits |= R3_W4_INV_STOP;
    }
#if (R3_W4_HW_CASE_ID == R3_W4_HW_CASE_STOP_D)
    if (R3W3Runtime_StopRunning(R3_W4_HW_STOP_ID) != R3_W3_RUNTIME_OK ||
        !Snapshot(&snapshot) || snapshot.lifecycle.rollback_count != 1U)
    {
        g_r3_w4_hw_result.invariant_bits |= R3_W4_INV_STOP;
    }
    else
    {
        g_r3_w4_hw_result.duplicate_stop_ok = 1U;
    }
#endif
#if (R3_W4_HW_CASE_ID == R3_W4_HW_CASE_STOP_E)
    if ((snapshot.lifecycle.state != R3_LIFECYCLE_RESET_REQUIRED) ||
        (snapshot.driver.state != ADC_DBM_DRIVER_STATE_ERROR) ||
        (snapshot.driver.hardware_owned != 0U) ||
        (snapshot.stop_begin_report_valid == 0U) ||
        (snapshot.stop_report_valid == 0U))
    {
        g_r3_w4_hw_result.invariant_bits |= R3_W4_INV_QUIESCED;
    }
#else
    if ((snapshot.lifecycle.state != R3_LIFECYCLE_IDLE) ||
        (snapshot.lifecycle.acquisition_publish_allowed != 0U) ||
        (snapshot.lifecycle.processing_claim_allowed != 0U) ||
        (snapshot.driver.hardware_owned != 0U) ||
        (snapshot.stop_begin_report_valid == 0U) ||
        (snapshot.stop_report_valid == 0U) ||
        ((snapshot.stop_report.dma_cr_after_stop & DMA_SxCR_EN) != 0U))
    {
        g_r3_w4_hw_result.invariant_bits |= R3_W4_INV_QUIESCED;
    }
#endif
    if ((snapshot.rollback_ack_mask != ((1UL << 8) | (1UL << 9))) ||
        (snapshot.workers.faulted != 0U))
    {
        g_r3_w4_hw_result.invariant_bits |= R3_W4_INV_WORKERS;
    }
#if (R3_W4_HW_CASE_ID == R3_W4_HW_CASE_T04_A)
    if ((snapshot.stop_begin_report.captured_samples == 0U) ||
        (snapshot.stop_begin_report.captured_samples >= R3_W3_RUNTIME_BLOCK_SAMPLES) ||
        (snapshot.driver.completion_count != 0U) ||
        (snapshot.workers.processing_cancel_count != 0U))
        g_r3_w4_hw_result.invariant_bits |= R3_W4_INV_PARTIAL;
#elif (R3_W4_HW_CASE_ID == R3_W4_HW_CASE_T04_B)
    HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
    if (((snapshot.stop_begin_report.dma_lisr_at_begin & DMA_LISR_TCIF0) == 0U) ||
        (snapshot.driver.completion_count != 0U) ||
        (snapshot.workers.processing_cancel_count != 0U))
        g_r3_w4_hw_result.invariant_bits |= R3_W4_INV_PARTIAL;
#elif (R3_W4_HW_CASE_ID == R3_W4_HW_CASE_STOP_A)
    if (snapshot.workers.processing_cancel_count == 0U)
        g_r3_w4_hw_result.invariant_bits |= R3_W4_INV_WORKERS;
#elif (R3_W4_HW_CASE_ID == R3_W4_HW_CASE_STOP_B)
    if ((snapshot.processing_entered == 0U) ||
        (snapshot.workers.processing_complete_count == 0U))
        g_r3_w4_hw_result.invariant_bits |= R3_W4_INV_CURRENT;
#elif (R3_W4_HW_CASE_ID == R3_W4_HW_CASE_STOP_C)
    if ((snapshot.driver.inactive_rebind_success_count == 0U) ||
        (snapshot.driver.inactive_keep_success_count == 0U) ||
        (snapshot.workers.processing_cancel_count == 0U))
        g_r3_w4_hw_result.invariant_bits |= R3_W4_INV_WORKERS;
#endif
    Publish(g_r3_w4_hw_result.invariant_bits == 0U ?
        R3_W4_HW_TERMINAL_PASS : R3_W4_HW_TERMINAL_FAIL);
    TerminalLoop();
}

void R3_W4_HW_Start(void)
{
    TaskHandle_t controller;
    (void)memset((void *)&g_r3_w4_hw_result, 0, sizeof(g_r3_w4_hw_result));
    g_r3_w4_hw_result.magic = R3_W4_HW_RESULT_MAGIC;
    g_r3_w4_hw_result.schema_version = R3_W4_HW_RESULT_SCHEMA;
    g_r3_w4_hw_result.case_id = R3_W4_HW_CASE_ID;
    g_r3_w4_hw_result.terminal_code = R3_W4_HW_TERMINAL_RUNNING;
    controller = xTaskCreateStatic(ControllerTask, "R3W4Ctrl",
        R3_W4_HW_CONTROLLER_STACK_WORDS, NULL, R3_W4_HW_CONTROLLER_PRIORITY,
        controller_stack, &controller_tcb);
    if (controller == NULL)
    {
        g_r3_w4_hw_result.invariant_bits |= R3_W4_INV_INITIALIZE;
        Publish(R3_W4_HW_TERMINAL_FAIL);
        __disable_irq(); for (;;) { __NOP(); }
    }
    vTaskStartScheduler();
    g_r3_w4_hw_result.invariant_bits |= R3_W4_INV_INITIALIZE;
    Publish(R3_W4_HW_TERMINAL_FAIL);
    __disable_irq(); for (;;) { __NOP(); }
}
