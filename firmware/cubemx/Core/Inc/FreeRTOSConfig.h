#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>
#include "stm32f4xx.h"

extern uint32_t SystemCoreClock;

/* Scheduler */
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_TICKLESS_IDLE                 0

/* Clock / tick */
#define configCPU_CLOCK_HZ                      ( SystemCoreClock )
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 )
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS

/* Tasks */
#define configMAX_PRIORITIES                    8
#define configMINIMAL_STACK_SIZE                ( ( uint16_t ) 128 )
#define configMAX_TASK_NAME_LEN                 16
#define configIDLE_SHOULD_YIELD                 1

/* Allocation policy */
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        0

/* Hooks */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     1
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            0

/* Queue-commit adapters. Exactly one active build profile owns the functional
 * traceQUEUE_SEND binding. FreeRTOS V11.1.0 invokes this hook inside the
 * successful task-send critical section before the token copy. */
#if defined(STREAM_LAB_FOUNDATION_QUEUE_ADAPTER)
#define INCLUDE_xTaskGetSchedulerState          1
void StreamQueueAdapter_TraceQueueSend(void *queue_handle);
#define traceQUEUE_SEND(pxQueue) \
    StreamQueueAdapter_TraceQueueSend((void *)(pxQueue))
#elif defined(STREAM_LAB_R2_W6)
void R2_W6_TraceQueueSend(void *queue_handle);
#define traceQUEUE_SEND(pxQueue) R2_W6_TraceQueueSend((void *)(pxQueue))
#elif defined(STREAM_LAB_R2_W5)
void R2_W5_TraceQueueSend(void *queue_handle);
#define traceQUEUE_SEND(pxQueue) R2_W5_TraceQueueSend((void *)(pxQueue))
#elif defined(STREAM_LAB_R2_W4)
void R2_W4_TraceQueueSend(void *queue_handle);
#define traceQUEUE_SEND(pxQueue) R2_W4_TraceQueueSend((void *)(pxQueue))
#endif

/* Kernel features needed by the approved architecture */
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               8
#define configUSE_QUEUE_SETS                    0
#define configUSE_APPLICATION_TASK_TAG          0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0
#define configUSE_NEWLIB_REENTRANT              0
#define configENABLE_BACKWARD_COMPATIBILITY     0

/* Not used in the R0 minimal platform */
#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         1
#define configUSE_TIMERS                        0
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

/* API inclusion */
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_vTaskDelete                     0
#define INCLUDE_vTaskSuspend                    0
#define INCLUDE_uxTaskPriorityGet               0
#define INCLUDE_vTaskPrioritySet                0
#define INCLUDE_eTaskGetState                   0
#define INCLUDE_xTaskGetIdleTaskHandle          0
#define INCLUDE_xTaskGetHandle                  0

/* STM32F446 / Cortex-M4 interrupt-priority contract.
 * R0-F will verify the actual runtime/NVIC state before platform freeze.
 */
#ifdef __NVIC_PRIO_BITS
    #define configPRIO_BITS                     __NVIC_PRIO_BITS
#else
    #define configPRIO_BITS                     4
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY     5

#define configKERNEL_INTERRUPT_PRIORITY \
    ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

/* The ARM_CM4F port owns these exception vectors directly. */
#define vPortSVCHandler                         SVC_Handler
#define xPortPendSVHandler                      PendSV_Handler
#define xPortSysTickHandler                     SysTick_Handler

#define configASSERT( x )                       \
    do                                          \
    {                                           \
        if( ( x ) == 0 )                       \
        {                                       \
            __disable_irq();                    \
            for( ;; )                           \
            {                                   \
            }                                   \
        }                                       \
    } while( 0 )

#endif /* FREERTOS_CONFIG_H */
