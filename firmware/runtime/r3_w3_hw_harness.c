#include "r3_w3_hw_harness.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "r3_w3_runtime.h"

#ifndef R3_W3_HW_CASE_ID
#error "R3_W3_HW_CASE_ID must be supplied by CMake"
#endif

#define R3_W3_HW_BOOT_ID 0x33570001UL
#define R3_W3_HW_REQUEST_ID 0x00000017UL
#define R3_W3_HW_GENERATION 0x00000001UL
#define R3_W3_HW_CONTROLLER_STACK_WORDS 1024U
#define R3_W3_HW_CONTROLLER_PRIORITY (tskIDLE_PRIORITY + 4U)

#define R3_W3_INV_INITIALIZE       (1UL << 0)
#define R3_W3_INV_PREPARE          (1UL << 1)
#define R3_W3_INV_COMMIT           (1UL << 2)
#define R3_W3_INV_PRECOMMIT_VIEW   (1UL << 3)
#define R3_W3_INV_RUNNING_VIEW     (1UL << 4)
#define R3_W3_INV_ROLLBACK_VIEW    (1UL << 5)
#define R3_W3_INV_RUNTIME_FAULT    (1UL << 6)

volatile R3W3HwResult g_r3_w3_hw_result;
static StaticTask_t controller_tcb;
static StackType_t controller_stack[R3_W3_HW_CONTROLLER_STACK_WORDS];

static void Capture(const R3W3RuntimeSnapshot *snapshot)
{
    g_r3_w3_hw_result.lifecycle_state = (uint32_t)snapshot->lifecycle.state;
    g_r3_w3_hw_result.start_ticket_valid = snapshot->lifecycle.start_ticket_valid;
    g_r3_w3_hw_result.acquisition_gate = snapshot->lifecycle.acquisition_publish_allowed;
    g_r3_w3_hw_result.processing_gate = snapshot->lifecycle.processing_claim_allowed;
    g_r3_w3_hw_result.interference_gate = snapshot->lifecycle.interference_release_allowed;
    g_r3_w3_hw_result.driver_state = (uint32_t)snapshot->driver.state;
    g_r3_w3_hw_result.driver_hardware_owned = snapshot->driver.hardware_owned;
    g_r3_w3_hw_result.driver_tim2_cr1 = snapshot->driver.tim2_cr1;
    g_r3_w3_hw_result.driver_dma_cr = snapshot->driver.dma_cr;
    g_r3_w3_hw_result.worker_processing_phase =
        (uint32_t)snapshot->workers.processing_contract.phase;
    g_r3_w3_hw_result.worker_interference_phase =
        (uint32_t)snapshot->workers.interference_contract.phase;
    g_r3_w3_hw_result.rollback_ack_mask = snapshot->rollback_ack_mask;
    g_r3_w3_hw_result.runtime_fault = snapshot->runtime_fault;
    g_r3_w3_hw_result.lifecycle_failure_count = snapshot->lifecycle.failure_count;
    g_r3_w3_hw_result.lifecycle_rollback_count = snapshot->lifecycle.rollback_count;
}

static int ReadSnapshot(R3W3RuntimeSnapshot *out)
{
    if (R3W3Runtime_GetSnapshot(out) != R3_W3_RUNTIME_OK)
    {
        g_r3_w3_hw_result.invariant_bits |= R3_W3_INV_ROLLBACK_VIEW;
        return 0;
    }
    Capture(out);
    if (out->runtime_fault != 0U)
    {
        g_r3_w3_hw_result.invariant_bits |= R3_W3_INV_RUNTIME_FAULT;
        return 0;
    }
    return 1;
}

static void Publish(uint32_t terminal)
{
    g_r3_w3_hw_result.terminal_code = terminal;
    __DMB();
    g_r3_w3_hw_result.completed_magic = R3_W3_HW_COMPLETED_MAGIC;
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
    R3W3RuntimeConfig config;
    R3LifecycleStartRequest request;
    R3LifecycleStartTicket ticket;
    R3W3RuntimeSnapshot snapshot;
    R3W3RuntimeStatus status;
    (void)argument;

    (void)memset(&config, 0, sizeof(config));
    config.boot_id = R3_W3_HW_BOOT_ID;
    config.k = 1U;
#if (R3_W3_HW_CASE_ID == R3_W3_HW_CASE_T06_A)
    config.inject_arm_failure = 1U;
#elif (R3_W3_HW_CASE_ID == R3_W3_HW_CASE_START_C)
    config.inject_commit_failure = 1U;
#endif
    if (R3W3Runtime_Initialize(&config) != R3_W3_RUNTIME_OK)
    {
        g_r3_w3_hw_result.invariant_bits |= R3_W3_INV_INITIALIZE;
        Publish(R3_W3_HW_TERMINAL_FAIL);
        StableTerminalLoop();
    }
    (void)memset(&request, 0, sizeof(request));
    request.boot_id = R3_W3_HW_BOOT_ID;
    request.request_id = R3_W3_HW_REQUEST_ID;
    request.generation = R3_W3_HW_GENERATION;
    request.configuration_id = config.k;
    status = R3W3Runtime_PrepareStart(&request, &ticket);

#if (R3_W3_HW_CASE_ID == R3_W3_HW_CASE_T06_A)
    if (status == R3_W3_RUNTIME_OK)
    {
        g_r3_w3_hw_result.invariant_bits |= R3_W3_INV_PREPARE;
    }
    if (!ReadSnapshot(&snapshot) ||
        (snapshot.lifecycle.state != R3_LIFECYCLE_IDLE) ||
        (snapshot.driver.hardware_owned != 0U) ||
        ((snapshot.driver.tim2_cr1 & TIM_CR1_CEN) != 0U) ||
        (snapshot.rollback_ack_mask != ((1UL << 8) | (1UL << 9))))
    {
        g_r3_w3_hw_result.invariant_bits |= R3_W3_INV_ROLLBACK_VIEW;
    }
#else
    if (status != R3_W3_RUNTIME_OK)
    {
        g_r3_w3_hw_result.invariant_bits |= R3_W3_INV_PREPARE;
    }
    if (!ReadSnapshot(&snapshot) ||
        (snapshot.lifecycle.state != R3_LIFECYCLE_PREPARING) ||
        (snapshot.lifecycle.acquisition_publish_allowed != 0U) ||
        (snapshot.lifecycle.processing_claim_allowed != 0U) ||
        (snapshot.lifecycle.interference_release_allowed != 0U) ||
        (snapshot.driver.state != ADC_DBM_DRIVER_STATE_ARMED) ||
        (snapshot.driver.hardware_owned == 0U) ||
        ((snapshot.driver.tim2_cr1 & TIM_CR1_CEN) != 0U))
    {
        g_r3_w3_hw_result.invariant_bits |= R3_W3_INV_PRECOMMIT_VIEW;
    }
#if (R3_W3_HW_CASE_ID == R3_W3_HW_CASE_T06_B)
    if (R3W3Runtime_StopBeforeCommit(&ticket) != R3_W3_RUNTIME_OK ||
        !ReadSnapshot(&snapshot) ||
        (snapshot.lifecycle.state != R3_LIFECYCLE_IDLE) ||
        (snapshot.driver.hardware_owned != 0U) ||
        ((snapshot.driver.tim2_cr1 & TIM_CR1_CEN) != 0U))
    {
        g_r3_w3_hw_result.invariant_bits |= R3_W3_INV_ROLLBACK_VIEW;
    }
#else
    status = R3W3Runtime_CommitStart(&ticket);
#if (R3_W3_HW_CASE_ID == R3_W3_HW_CASE_START_C)
    if (status == R3_W3_RUNTIME_OK || !ReadSnapshot(&snapshot) ||
        (snapshot.lifecycle.state != R3_LIFECYCLE_IDLE) ||
        (snapshot.driver.hardware_owned != 0U) ||
        ((snapshot.driver.tim2_cr1 & TIM_CR1_CEN) != 0U))
    {
        g_r3_w3_hw_result.invariant_bits |= R3_W3_INV_ROLLBACK_VIEW;
    }
#else
    if (status != R3_W3_RUNTIME_OK || !ReadSnapshot(&snapshot) ||
        (snapshot.lifecycle.state != R3_LIFECYCLE_RUNNING) ||
        (snapshot.lifecycle.acquisition_publish_allowed == 0U) ||
        (snapshot.lifecycle.processing_claim_allowed == 0U) ||
        (snapshot.lifecycle.interference_release_allowed == 0U) ||
        (snapshot.driver.state != ADC_DBM_DRIVER_STATE_RUNNING) ||
        (snapshot.driver.hardware_owned == 0U) ||
        ((snapshot.driver.tim2_cr1 & TIM_CR1_CEN) == 0U))
    {
        g_r3_w3_hw_result.invariant_bits |= R3_W3_INV_RUNNING_VIEW;
    }
#endif
#endif
#endif

    Publish(g_r3_w3_hw_result.invariant_bits == 0U ?
        R3_W3_HW_TERMINAL_PASS : R3_W3_HW_TERMINAL_FAIL);
    StableTerminalLoop();
}

void R3_W3_HW_Start(void)
{
    TaskHandle_t controller;
    (void)memset((void *)&g_r3_w3_hw_result, 0, sizeof(g_r3_w3_hw_result));
    g_r3_w3_hw_result.magic = R3_W3_HW_RESULT_MAGIC;
    g_r3_w3_hw_result.schema_version = R3_W3_HW_RESULT_SCHEMA;
    g_r3_w3_hw_result.case_id = R3_W3_HW_CASE_ID;
    g_r3_w3_hw_result.terminal_code = R3_W3_HW_TERMINAL_RUNNING;
    controller = xTaskCreateStatic(ControllerTask, "R3W3Ctrl",
        R3_W3_HW_CONTROLLER_STACK_WORDS, NULL, R3_W3_HW_CONTROLLER_PRIORITY,
        controller_stack, &controller_tcb);
    if (controller == NULL)
    {
        g_r3_w3_hw_result.invariant_bits |= R3_W3_INV_INITIALIZE;
        Publish(R3_W3_HW_TERMINAL_FAIL);
        __disable_irq();
        for (;;) { __NOP(); }
    }
    vTaskStartScheduler();
    g_r3_w3_hw_result.invariant_bits |= R3_W3_INV_INITIALIZE;
    Publish(R3_W3_HW_TERMINAL_FAIL);
    __disable_irq();
    for (;;) { __NOP(); }
}
