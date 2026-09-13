#ifndef R0_FREERTOS_SMOKE_H
#define R0_FREERTOS_SMOKE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern volatile uint32_t g_r0_task_a_count;
extern volatile uint32_t g_r0_task_b_count;
extern volatile uint32_t g_r0_tick_hook_count;
extern volatile uint32_t g_r0_scheduler_returned;

void R0_FreeRTOS_StartSmoke(void);

#ifdef __cplusplus
}
#endif

#endif /* R0_FREERTOS_SMOKE_H */
