# R4 measurement error budget

Status: **qualified instrumentation-profile budget; not an A/B perturbation
closure.**

This document binds all numerical statements below to the NUCLEO-F446RE,
STM32F446xx, 180 MHz, Release/LTO R4 hardware profile recorded in the cited
sealed attempts.  It describes a residency/accounting measurement, not an
un-instrumented physical CPU-time oracle.

## Directly measured costs

`docs/evidence/r4/microbenchmark/attempt-0002` is the current-source sealed
target run from `d709eb6`'s post-fix formal image.  It records 33 DWT samples
for the complete guarded `RuntimeEvent_Apply` transaction, with sample
collection and the diagnostic-ledger reset outside the timed interval.  Every
timed apply returned `R4_RUNTIME_OK`; the evidence ABI records zero failed
samples, so an invalid sealed-window reuse cannot be accepted as a timing
sample.

| Ledger path | Min / median / max cycles | Use in the budget |
|---|---:|---|
| task-context checkpoint | 412 / 417 / **439** | single task/checkpoint transaction |
| balanced IRQ enter + exit | 897 / 902 / **902** | two-event accounting transaction, excluding exception entry/return |
| window open + close | 876 / 881 / **2049** | two-event window transaction |
| nested enter + enter + exit + exit | 1595 / 1597 / **3358** | four-event nested accounting transaction |

These maxima are observed sample maxima, not statistical guarantees for a
different compiler, clock, FreeRTOS port, code revision, or instrumentation
profile.  They are intentionally never added to a result as a correction:
the result reports the instrumented residency that actually occurred.

## Fixed resolution and boundary terms

| Term | Bound / treatment |
|---|---|
| DWT timestamp granularity | 1 cycle per timestamp; a duration formed by two independently placed C boundaries has up to 2 cycles of endpoint quantization. |
| RuntimeEvent transaction boundary | The measured path above includes save-PRIMASK, Clock64 read, settlement, owner/window mutation and restore-PRIMASK.  The 3358-cycle nested row is a four-event sequence, not one uninterrupted mask interval.  `mask-timing/attempt-0001` separately observes 52 actual PRIMASK-held target spans, maximum **559 cycles**, against the frozen 1800-cycle limit. |
| q0 to physical TIM2 start | The frozen target acceptance bound is <=1800 cycles, directly bracketing the TIM2 CEN write in sealed `t15-q0/attempt-0004` and `t15-release/attempt-0005`; it is a compliance bound, not a post-hoc prediction input. |
| Completion lock interval | Sealed Release/LTO `t04-commit-budget-a/attempt-0002` and `t04-commit-budget-b/attempt-0001` each retain a maximum complete `t_lock` to `t_unlock` interval of 1715 cycles, below the 1800-cycle gate. |
| Combined production DMA service | Sealed Release/LTO `combined-service/attempt-0001` executes 65,000 ms of real DMA traffic.  All 50,781 IRQs have one full trace-entry to post-`RuntimeEvent` IRQ-exit service span; the observed maximum is **10,734 cycles**, below the frozen 23,040-cycle (10% of the 256-sample / 200 kSamples/s block period) margin.  Cortex-M exception entry/return remains explicitly outside this C trace interval. |

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

The direct costs bound representative isolated ledger sequences; they do not
equate the four-event nested aggregate to a single critical-mask duration or
to the completion critical-section gate.  The separately measured actual
PRIMASK span supplies that bounded-mask claim.  The fresh combined-service
witness separately establishes the frozen representative
DMA service margin in the R4 Release/LTO profile.  R2's existing `k8-normal`
trace reports a 1076-cycle `final_window` from a different instrumentation
state; it cannot be reused as an R4 perturbation proof.

Therefore this document supports R4-A20, the independently measured
RuntimeEvent critical-masking portion of R4-A27, and the bounded R4-profile
combined DMA-service claim.  It
does not close R4-A22.  Closure still requires representative,
same-configuration target A/B evidence comparing a minimal profile with the
R4 profile for DMA service, processing throughput, wall response,
drop/admission and CPU/residual accounting.

## Non-claims

* No claim is made that these numbers are absolute physical CPU time.
* No claim is made that R4 evidence validates R5 cohort semantics or R6
  prediction inputs.
* Any change to the compiler, clock, FreeRTOS port, trace wiring or R4 source
  invalidates the numerical performance portion of this budget until rerun.
