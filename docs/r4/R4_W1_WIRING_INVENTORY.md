# R4-W1 measurement contract and wiring inventory

Status: **REVIEW COMPLETE — NOT YET PASS**

This is a source-level inventory for the fixed R4 build profile.  It freezes
what was observed at `ecb46730f123d9b89975d826b79160ceb14249ef`; it does not
claim that a source route has been exercised on target.

## Fixed platform assumptions

| Item | Observed source | Result |
|---|---|---|
| Kernel | `Middlewares/Third_Party/FreeRTOS-Kernel/include/task.h` defines `tskKERNEL_VERSION_NUMBER` as `V11.1.0` | fixed |
| Port | `Middlewares/Third_Party/FreeRTOS-Kernel/portable/GCC/ARM_CM4F` | fixed |
| SysTick vector | `FreeRTOSConfig.h` maps `xPortSysTickHandler` to `SysTick_Handler` | port-owned |
| Tick hook | `configUSE_TICK_HOOK == 1` | enabled |

## Runtime ownership wiring table

| Source / actual call site | Runtime semantic | Owner of exactly-one exit | W1 finding |
|---|---|---|---|
| `traceTASK_SWITCHED_OUT()` in `tasks.c`; macro in `Core/Inc/FreeRTOSConfig.h` | `TASK_SWITCHED_OUT(current TCB)` | not applicable | one configured mapping |
| `traceTASK_SWITCHED_IN()` in scheduler start and context-switch paths; macro in `FreeRTOSConfig.h` | `TASK_SWITCHED_IN(current TCB)` | not applicable | one configured mapping; first scheduler entry must be target-tested in W4 |
| `xPortSysTickHandler()` in ARM_CM4F `port.c` | `IRQ_ENTER`, then exactly one port `IRQ_EXIT` or `IRQ_EXIT_TO_SCHEDULER` | ARM_CM4F port | no application wrapper observed |
| `vApplicationTickHook()` in `Core/Src/r0_freertos_smoke.c` | checkpoint plus `TickServiceTarget_OnTickHook()` | SysTick port | hook creates no IRQ pair |
| `DMA2_Stream0_IRQHandler()` in `Core/Src/stm32f4xx_it.c` | one outer `IRQ_ENTER`; tail emits `portYIELD_FROM_ISR(woken)` | ARM_CM4F port macro | callback chain does not add an R4 enter/exit; target-path candidate |
| `TIM6_DAC_IRQHandler()` (R4 hardware diagnostic only) | one outer `IRQ_ENTER`; `portYIELD_FROM_ISR(pdFALSE)` tail | ARM_CM4F port macro | target-only nesting diagnostic candidate |
| `TIM7_IRQHandler()` | one outer `IRQ_ENTER`; `portYIELD_FROM_ISR(pdFALSE)` tail after `HAL_TIM_IRQHandler` | ARM_CM4F port macro | fixed source route; target no-yield evidence remains W5 |
| `HAL_TIM_PeriodElapsedCallback()` | HAL tick update only | none | no second R4 IRQ pair observed |
| `DMA1_Stream6_IRQHandler()` / `USART2_IRQHandler()` under `STREAM_LAB_R2_CT` | conditional outer `IRQ_ENTER`; `portYIELD_FROM_ISR` tail | ARM_CM4F port macro | source route is R4-safe if composed; the formal R4 foundation profile intentionally excludes the historical R2_CT profile |
| `R4_RuntimeTarget_OpenWindow/CloseWindow()` called by `r4_hw_harness.c` | `WINDOW_OPEN` / `WINDOW_CLOSE` RuntimeEvent | not applicable | **gap:** harness-driven, not production S0/S1 DMA-boundary wiring |
| `StreamQueueAdapter_TraceQueueSendLock/Commit/Unlock()` | completion `t_lock`, `t_commit`, `t_unlock` | queue adapter | one adapter path observed; W8 must prove actual kernel lock boundaries |

## Mapping implementation

`FreeRTOSConfig.h` maps all configured trace macros to one target adapter:
`R4_RuntimeTarget_TraceIsrEnter`, `R4_RuntimeTarget_TraceIsrExit`,
`R4_RuntimeTarget_TraceTaskSwitchedOut`, and
`R4_RuntimeTarget_TraceTaskSwitchedIn`.  The adapter emits `RuntimeEvent`
without accepting a caller-provided timestamp.  `traceISR_EXIT_TO_SCHEDULER`
currently maps to the same exit event as `traceISR_EXIT`; ownership remains
the interrupted context until the later scheduler task-switch hook.

## W1 gates

| Gate | State | Reason |
|---|---|---|
| Fixed FreeRTOS V11.1.0 / ARM_CM4F port identified | DONE | source identity established |
| SysTick is not application double-instrumented | IMPLEMENTED BUT UNVERIFIED | source route is singular; target evidence remains W5/W9 |
| Tick hook creates no extra IRQ event | IMPLEMENTED BUT UNVERIFIED | source route is singular; target evidence remains W5/W7 |
| DMA outer entry/common-tail contract | IMPLEMENTED BUT UNVERIFIED | source route is singular; target no-yield/yield proof remains W5 |
| All production peripheral paths have frozen unique wiring | IMPLEMENTED BUT UNVERIFIED | TIM7 and optional R2_CT routes use a one-entry/port-tail source pattern; active R4 target paths still require W5 hardware audit |
| Formal window source is S0/S1 DMA boundary | MISSING | current calls are harness-local, not production boundary wiring |

## Existing R4 asset disposition

R4 is not restarted from zero.  The following disposition prevents both
discarding valid work and accidentally treating narrow/obsolete evidence as a
Principal closure claim.

| Existing asset | Disposition | Why |
|---|---|---|
| Clock64 implementation, >60 s T12 raw attempts, and monitor service-gap data | **REUSE** | still establishes one clock authority, real DWT wrap behaviour and a service-margin baseline |
| RuntimeEvent PRIMASK transaction, fault latch, event serial, and native fault tests | **REUSE** | these are direct W3 foundations; later target cases extend rather than replace them |
| Real TIM7→TIM6 nesting source and T17 target record | **REUSE** | it is the required real-NVIC nesting basis; new source revisions need a fresh final run |
| T15 q0/release attempts and TickServiceAdapter | **REUSE** | they are the W7 starting evidence; add the missing suspension/integrity scope rather than rewrite the adapter |
| Release full `t_lock`--`t_unlock` measurements | **REUSE** | they remain the W8 benchmark baseline; pending-IRQ and composition evidence is additive |
| H0--H5 evidence runner, manifests, failed-attempt retention | **REUSE** | it is the correct committed-source provenance mechanism for subsequent directed cases |
| Pre-`ecb4673` aggregate-only window fields and their PASS attempts | **SUPERSEDE FOR FINAL CLOSURE** | they cannot report sealed task/IRQ/idle/residual contributions and were built from an earlier firmware identity; retain them as historical evidence, do not delete them |
| Harness-local OPEN/CLOSE calls as production CPU-window semantics | **REPLACE** | they are useful diagnostics but cannot stand in for the Architecture-defined S0/S1 DMA boundaries |
| Manual TIM7 direct IRQ exit | **REPLACED** | TIM7 now uses the same port-owned `pdFALSE` common tail as other peripheral IRQs |

Historical evidence is immutable and remains in Git.  “Supersede” means only
that it is excluded from the final Principal acceptance matrix, never that
raw data is erased or hidden.

## W1 conclusion and next action

The peripheral source pattern is now frozen as one outer entry plus one
port-owned common exit, including the optional R2_CT routes.  W1 remains
unclosed until the static/target audit demonstrates those paths under their
actual build profiles.  Production S0/S1 window-boundary integration is
deferred to W6.  No conclusion in this document closes W2--W10.

### Profile-isolation observation

An attempted `R2_W6 + R2_CT + R4 foundation/runtime` target configuration is
rejected by the pre-existing CMake gate: the F0-3 ownership-core foundation
must not be combined with historical R2 profiles.  This is expected profile
isolation, not a failed R4 firmware build.  R2_CT therefore remains a
separate R2 regression profile; the R4 formal target profile does not claim a
combined hardware result for it.
