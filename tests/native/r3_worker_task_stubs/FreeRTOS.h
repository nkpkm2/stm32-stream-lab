#ifndef FREERTOS_H
#define FREERTOS_H

#include <stdint.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef uint32_t StackType_t;

typedef struct
{
    uint32_t opaque;
} StaticTask_t;

#define pdPASS 1
#define pdFAIL 0
#define pdTRUE 1
#define pdFALSE 0

#define portMAX_DELAY 0xFFFFFFFFUL
#define tskIDLE_PRIORITY 0U

typedef enum
{
    eNoAction = 0,
    eSetBits = 1
} eNotifyAction;

void R3Test_EnterCritical(void);
void R3Test_ExitCritical(void);
UBaseType_t R3Test_EnterCriticalFromISR(void);
void R3Test_ExitCriticalFromISR(UBaseType_t saved_interrupt_status);

#define taskENTER_CRITICAL() R3Test_EnterCritical()
#define taskEXIT_CRITICAL() R3Test_ExitCritical()
#define taskENTER_CRITICAL_FROM_ISR() R3Test_EnterCriticalFromISR()
#define taskEXIT_CRITICAL_FROM_ISR(saved_interrupt_status) \
    R3Test_ExitCriticalFromISR(saved_interrupt_status)

#endif
