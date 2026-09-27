# R4 measurement error budget

Status: **qualified instrumentation-profile budget; not an A/B perturbation
closure.**

This document binds all numerical statements below to the NUCLEO-F446RE,
STM32F446xx, 180 MHz, Release/LTO R4 hardware profile recorded in the cited
sealed attempts.  It describes a residency/accounting measurement, not an
un-instrumented physical CPU-time oracle.

## Directly measured costs

`docs/evidence/r4/microbenchmark/attempt-0001` is a sealed target run from
`3ed3df7084d3c0b553151be2e569c8e3ddb08366`.  It records 33 DWT samples for
the complete guarded `RuntimeEvent_Apply` transaction, with sample collection
outside the timed interval.

| Ledger path | Min / median / max cycles | Use in the budget |
|---|---:|---|
| task-context checkpoint | 308 / 308 / **379** | single task/checkpoint transaction |
| balanced IRQ enter + exit | 778 / 784 / **906** | two-event accounting transaction, excluding exception entry/return |
| window open + close | 691 / 691 / **822** | two-event window transaction |
| nested enter + enter + exit + exit | 1478 / 1484 / **1600** | four-event nested accounting transaction |

These maxima are observed sample maxima, not statistical guarantees for a
different compiler, clock, FreeRTOS port, code revision, or instrumentation
profile.  They are intentionally never added to a result as a correction:
the result reports the instrumented residency that actually occurred.

## Fixed resolution and boundary terms

| Term | Bound / treatment |
|---|---|
| DWT timestamp granularity | 1 cycle per timestamp; a duration formed by two independently placed C boundaries has up to 2 cycles of endpoint quantization. |
| RuntimeEvent transaction boundary | The measured path above includes save-PRIMASK, Clock64 read, settlement, owner/window mutation and restore-PRIMASK.  The largest exercised nested ledger transaction is 1600 cycles.  Fresh `mask-timing/attempt-0001` separately observes 52 actual PRIMASK-held target spans, maximum **559 cycles**, against the frozen 1800-cycle limit. |
| q0 to physical TIM2 start | The frozen target acceptance bound is <=1800 cycles, directly bracketing the TIM2 CEN write in sealed `t15-q0/attempt-0004` and `t15-release/attempt-0005`; it is a compliance bound, not a post-hoc prediction input. |
| Completion lock interval | Sealed Release/LTO `t04-commit-budget-a/attempt-0002` and `t04-commit-budget-b/attempt-0001` each retain a maximum complete `t_lock` to `t_unlock` interval of 1715 cycles, below the 1800-cycle gate. |

## Terms deliberately not claimed as zero

The following are not fully attributable by the C trace endpoints and must
not be silently folded into a task or instrumented IRQ result:

* Cortex-M exception entry/return cycles before/after the trace hook;
* PendSV and SVC machine cycles not represented by a task-selection hook;
* compiler scheduling and branch/cache effects outside a timed transaction;
* debugger halt/resume or changed core-clock conditions.

R4 exposes an `unclassified`/platform residual bucket instead of forcing
these cycles into task, idle or instrumented-IRQ ownership.  A failure of the
Clock64 service history is not corrected: the tick-gap case latches
fail-closed.  A logically backwards ledger boundary is likewise fail-closed
in `time-regression/attempt-0001`.

## Budget use and remaining gates

The direct costs establish that one complete nested ledger path is below the
same 1800-cycle scale used for the completion critical-section gate.  They do
**not** establish that all combined production ISR work is below every DMA
service margin.  In particular, R2's existing `k8-normal` trace reports a
1076-cycle `final_window` from a different instrumentation state; it cannot
be reused as an R4 perturbation proof.

Therefore this document supports R4-A20 and the RuntimeEvent critical-masking
portion of R4-A27, but does not close R4-A22 or the remaining combined-service
system-budget claim.  Closure still requires representative,
same-configuration target A/B evidence comparing a minimal profile with the
R4 profile for DMA service, processing throughput, wall response,
drop/admission and CPU/residual accounting.

## Non-claims

* No claim is made that these numbers are absolute physical CPU time.
* No claim is made that R4 evidence validates R5 cohort semantics or R6
  prediction inputs.
* Any change to the compiler, clock, FreeRTOS port, trace wiring or R4 source
  invalidates the numerical performance portion of this budget until rerun.
