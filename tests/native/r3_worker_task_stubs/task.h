#ifndef TASK_H
#define TASK_H

#include "FreeRTOS.h"

typedef void * TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

TaskHandle_t xTaskCreateStatic(
    TaskFunction_t task_code,
    const char *name,
    uint32_t stack_depth,
    void *parameters,
    UBaseType_t priority,
    StackType_t *stack_buffer,
    StaticTask_t *task_buffer);

BaseType_t xTaskNotify(
    TaskHandle_t task,
    uint32_t value,
    eNotifyAction action);

BaseType_t xTaskNotifyFromISR(
    TaskHandle_t task,
    uint32_t value,
    eNotifyAction action,
    BaseType_t *higher_priority_task_woken);

BaseType_t xTaskNotifyWait(
    uint32_t clear_on_entry,
    uint32_t clear_on_exit,
    uint32_t *value,
    TickType_t ticks_to_wait);

TaskHandle_t xTaskGetCurrentTaskHandle(void);

#endif
