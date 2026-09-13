#include "r0_freertos_smoke.h"

#include "FreeRTOS.h"
#include "task.h"

#define R0_SMOKE_STACK_WORDS 256U

volatile uint32_t g_r0_task_a_count = 0U;
volatile uint32_t g_r0_task_b_count = 0U;
volatile uint32_t g_r0_tick_hook_count = 0U;
volatile uint32_t g_r0_scheduler_returned = 0U;

static StaticTask_t g_r0_task_a_tcb;
static StaticTask_t g_r0_task_b_tcb;

static StackType_t g_r0_task_a_stack[R0_SMOKE_STACK_WORDS];
static StackType_t g_r0_task_b_stack[R0_SMOKE_STACK_WORDS];

static StaticTask_t g_r0_idle_tcb;
static StackType_t g_r0_idle_stack[configMINIMAL_STACK_SIZE];

static void R0_TaskA(void *argument)
{
    (void)argument;

    for (;;)
    {
        g_r0_task_a_count++;
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

static void R0_TaskB(void *argument)
{
    (void)argument;

    for (;;)
    {
        g_r0_task_b_count++;
        vTaskDelay(pdMS_TO_TICKS(17U));
    }
}

void R0_FreeRTOS_StartSmoke(void)
{
    TaskHandle_t task_a;
    TaskHandle_t task_b;

    task_a = xTaskCreateStatic(
        R0_TaskA,
        "R0A",
        R0_SMOKE_STACK_WORDS,
        NULL,
        tskIDLE_PRIORITY + 2U,
        g_r0_task_a_stack,
        &g_r0_task_a_tcb);

    task_b = xTaskCreateStatic(
        R0_TaskB,
        "R0B",
        R0_SMOKE_STACK_WORDS,
        NULL,
        tskIDLE_PRIORITY + 1U,
        g_r0_task_b_stack,
        &g_r0_task_b_tcb);

    if ((task_a == NULL) || (task_b == NULL))
    {
        __disable_irq();

        for (;;)
        {
        }
    }

    vTaskStartScheduler();

    /* A successful scheduler start must never return here. */
    g_r0_scheduler_returned = 1U;

    __disable_irq();

    for (;;)
    {
    }
}

void vApplicationGetIdleTaskMemory(
    StaticTask_t **ppxIdleTaskTCBBuffer,
    StackType_t **ppxIdleTaskStackBuffer,
    configSTACK_DEPTH_TYPE *puxIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &g_r0_idle_tcb;
    *ppxIdleTaskStackBuffer = g_r0_idle_stack;
    *puxIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationTickHook(void)
{
    /*
     * R0 observation only.
     * This is NOT the full TickServiceAdapter implementation.
     */
    g_r0_tick_hook_count++;
}

void vApplicationStackOverflowHook(
    TaskHandle_t xTask,
    char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;

    __disable_irq();

    for (;;)
    {
    }
}
