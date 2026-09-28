# R4 T04 engineering-deviation package

Status: **PROPOSED FOR PRINCIPAL DECISION**. This package does not mark T04
PASS and does not change the 1800-cycle architecture target.

## Decision

The focused RC3 review selects **Path B: explicit engineering deviation**.
The current production record is a valid `1919 > 1800` inner-interval failure.
The corrected diagnostic record independently finds a same-event maximum of
`1904 = 1286 + 618` cycles. The long path is therefore primarily pre-commit,
not a suffix-only artifact.

The pre-commit region contains the successful free-queue decision, adapter
validation, Processing lease release, TokenLedger FREE record, and atomic
RuntimeEvent ledger checkpoint. These are the safety invariants shared by R4,
R5, and R6. No isolated, demonstrably low-risk removal of about 150 cycles was
found in source or in the Release/LTO ARM disassembly. A production change here
would alter shared ownership or accounting semantics, so this package does not
trade those invariants for an unproven timing improvement.

## D1 — conservative full critical-interval engineering bound

The formal production measurement is an inner interval: `t_lock` is after
FreeRTOS has raised BASEPRI and `t_unlock` is before FreeRTOS lowers it. The
following is consequently an engineering ceiling, not evidence of formal
1800-cycle compliance:

| Component | Conservative bound (cycles) | Basis |
|---|---:|---|
| pre-edge: effective BASEPRI to `t_lock` | 4096 | Release/LTO trace path from the BASEPRI write in `SendFreeInternal` through the inlined lock predicate and `CompletionBoundaryNow`; no loop or callback is reachable on the successful COMPLETE path. The 512-instruction ceiling at 6 cycles/instruction plus 1024 cycles for DWT/data/control headroom is deliberately rounded up. |
| measured production inner interval | 1919 | Immutable valid formal-production observation at `238fa89`; diagnostic timing is not substituted. |
| post-edge: `t_unlock` to effective BASEPRI release | 2048 | Release/LTO `CompletionBoundaryNow`, unlock accounting, trace return, and `taskEXIT_CRITICAL` route; no loop/callback on the successful path. The 256-instruction ceiling at 6 cycles/instruction plus 512 cycles headroom is rounded up. |
| **full engineering ceiling** | **8064** | `4096 + 1919 + 2048 = 8063`, rounded upward. |

The accounting is tied to the F446 Release/LTO image and its configuration:
180 MHz HCLK and `FLASH_LATENCY_5` in `Core/Src/main.c`; `FLASH_LATENCY_5` is
defined by the supplied STM32 HAL as five flash wait states. The disassembly
anchors are `CompletionBoundaryNow` at `0x08003160`, the successful send path
at `SendFreeInternal`, and `StreamQueueAdapter_TraceQueueSendUnlock.part.0` at
`0x080031dc` in diagnostic candidate ELF
`E3C798E69D978C4179A34B2E89E2F5DB4159E79FB988EE29466841CDEA033064`.

This bound is intentionally too coarse to prove the 1800-cycle target and may
not be used for that purpose. It is a conservative engineering screen for the
present R4 profile. A formal T04 PASS still requires either independently
validated static edge bounds or a reviewed measurement that covers the actual
full interval.

## D2 — relation to real service/admission limits

`8064` cycles is below the sealed combined DMA service ceiling of 23,040
cycles (10% of the 256-sample, 200 kSamples/s block period) and below its
observed 10,734-cycle maximum in
`docs/evidence/r4/combined-service/attempt-0001`. It consumes about 3.5% of
the 230,400-cycle block period. It is not used to relax that DMA service limit,
the admission rules, or the 1800-cycle T04 target.

## D3 — integrity effect

No production timing or ownership source changed for this decision. The valid
diagnostic attempt reports 50,796 COMPLETE events, zero witness consistency
failures, zero non-timing invariant failures, and expected same-event flags.
The production 1919-cycle failure remains immutable. BufferPool, DMA,
generation, lease, TokenLedger, RuntimeEvent, and IRQ-accounting contracts are
unchanged.

## D4 — R5 compatibility

R5's S2/cutoff, cohort, classification, and sealed-result semantics are not
changed by this package. R5 consumes completion ordering rather than a claim
that T04 met 1800 cycles. Its existing evidence remains representative only
for that unchanged semantic boundary.

## D5 — R6 cost-input rule

The existing R6 harness's `5/5` commit prefix/suffix values are explicitly
synthetic semantic vectors, not physical T04 measurements. Any R6 prediction
or benchmark profile that models this R4 completion path must use either the
same-event measured inner costs `prefix=1286`, `suffix=618` (total 1904), or
the 8064-cycle conservative full-critical ceiling where mask-release latency
matters. It must not use a fictional `<=1800` physical input. This rule is a
binding cross-stage condition before an R6 physical-cost claim.

## D6 — R7 revalidation

R7 must rebuild its final DSP profile and re-establish the full-critical bound
or a reviewed fuller measurement before it may inherit any R4 timing claim.
This package provides no shortcut around the architecture's final R2--R4
benchmark regression requirement.

## Principal action requested

Accept or reject `T04 = ACCEPTED_ENGINEERING_DEVIATION` for the current R4
profile. Until that decision, the factual timing status remains
`CONFIRMED_VALID_FAIL / 1919 > 1800`; no code, threshold, hardware attempt, or
automatic retry is authorized by this document.
