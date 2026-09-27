# R4 IRQ and queue callsite table

Build profile: `STREAM_LAB_R4_RUNTIME=ON`, `STREAM_LAB_R4_HW=ON`, fixed
FreeRTOS Kernel V11.1.0 ARM_CM4F port.

| Path | Unique enter | Unique exit / state commit | Evidence |
|---|---|---|---|
| DMA2 Stream0 | `stm32f4xx_it.c:275`, `traceISR_ENTER()` | one `portYIELD_FROM_ISR(accumulated_woken)` at line 306; callbacks only accumulate | handler source |
| TIM6 T17 diagnostic | `stm32f4xx_it.c:233`, `traceISR_ENTER()` | one `portYIELD_FROM_ISR(pdFALSE)` at line 235 | handler source |
| SysTick | `port.c:569`, existing `traceISR_ENTER()` | exactly one mutually exclusive `traceISR_EXIT_TO_SCHEDULER()` / `traceISR_EXIT()` branch at lines 574/582 | fixed port source |
| Tick hook | no IRQ enter | no exit or `portYIELD_FROM_ISR`; it calls checkpoint then TickService only | `r0_freertos_smoke.c` |
| Queue successful COMPLETE | V11.1.0 `queue.c:962`, lock boundary | `traceQUEUE_SEND` performs atomic logical commit; the three queue exits all call unlock at 1068/1081/1105 | fixed queue source |

`FreeRTOSConfig.h:66-76` expands all three ISR trace macros to the sole
`R4_RuntimeTarget` endpoints and binds the lock/unlock hooks to
`StreamQueueAdapter`.  Thus a no-switch ISR still has one exit, a switch does
not synthesize a task-switch event, and callbacks cannot emit a second exit.

This table is source-callsite evidence; the hardware T17 run exercises the
TIM6 route, while the native nested/duplicate tests exercise ledger checking.
