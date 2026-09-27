# R4 Principal acceptance audit

Status: **IN PROGRESS — this document supersedes any narrower R4 closure
claim.**

Source: the Principal acceptance objective dated 2026-09-27.  A row is PASS
only when its specified evidence exists; implemented code or an adjacent
hardware test is not a substitute.

| Domain | Current evidence | Status | Closure required |
|---|---|---|---|
| A01–A03 Clock64 authority, >60 s wrap, service bound | `t12-soak-a/b` sealed target attempts | PARTIAL | add source-uniqueness and invalid-run audit; retain fresh 60 s records |
| A04–A06 RuntimeEvent atomicity/mask/error ordering | T17 pending IRQ and duplicate-exit evidence; native event tests | PARTIAL | explicit PRIMASK-entry-state and time-regression target/injection evidence |
| A07–A13 IRQ pairing, no-yield/yield, SysTick | T12 wiring/tails; T17 duplicate exit | PARTIAL | real nested IRQ, exact task-switch attribution and SysTick one-pair evidence |
| A14–A15 task/idle known workload | `task-synthetic/attempt-0004` sealed target PASS on the corrected ABI reader; H4 checks fixed A/B iterations, owner ratio, Idle residency, and formal conservation | PARTIAL | add known IRQ contribution and retain this workload in final integrated regression |
| A16–A24 CPU window clipping, immutability, conservation | `dma-window/attempt-0001` sealed target PASS at `cf78c83`: real DMA completion sequence opens at S0=1, seals at S1=4, and observes a later sequence >=5 without a boundary fault; per-window buckets and directed native source cases remain available | PARTIAL | execute native cases on a host and add final target intersection/conservation plus no-outside-window evidence |
| A25–A29 error/overhead/phase | tick DWT history and lock budget | PARTIAL | RuntimeEvent microbench, error budget, perturbation A/B, q0-to-TIM2 bound |
| A30–A37 TickService and suspension | T15 q0/release; tick history in T12 | PARTIAL | explicit tick-gap fault-injection and phase evidence |
| A38–A44 T12/T17 cutoff and formal/live separation | T12/T17 sealed attempts | PARTIAL | nested/cutoff interleaving and machine-readable formal-vs-live result split |
| A45–A51 response/utilization/critical composition | full lock bound twice | PARTIAL | known response-time test, utilization schema, RuntimeEvent masking bound, pending-IRQ-at-lock case |
| A52–A58 non-regression/provenance/error preservation | R4 H0–H5 identity/manifests; failed attempts retained | PARTIAL | R2 normal/drop and R3 lifecycle representative target anchors plus acceptance classifier |

## Evidence already sealed

* T12: `docs/evidence/r4/t12-soak-a/attempt-0001` and
  `docs/evidence/r4/t12-soak-b/attempt-0001`.
* T15: `docs/evidence/r4/t15-q0/attempt-0001` and
  `docs/evidence/r4/t15-release/attempt-0002`.
* T17: `docs/evidence/r4/t17-atomic/attempt-0002`.
* Full `t_lock` to `t_unlock` bound: Release/LTO
  `t04-commit-budget-a/attempt-0002` and
  `t04-commit-budget-b/attempt-0001`.

Historical failed attempts are intentionally retained alongside their PASS
successors.  They establish that the harness rejects over-broad release
stimuli, missing tick observation, and Debug-profile timing overrun.

* Production S0/S1 DMA window: `docs/evidence/r4/dma-window/attempt-0001`.
  Its H4 oracle checks the source-bound window configuration, S0/S1 open/close
  statuses, absence of a boundary error, and a completion after the sealed
  cutoff.  It is bound to `cf78c83235c56515a453084ecc6f07d6c8872015`.

## Evidence-reader ABI correction

The target result record contains naturally aligned 64-bit fields. During W6
work, a compile-time offset assertion exposed that the former evidence reader
had assumed packed offsets for some post-prefix 64-bit fields. The direct
firmware invariant checks and the raw `task-synthetic/attempt-0003` values are
consistent when read at their actual offsets, but the old H4 parser hash is
not final-closure quality. `task-synthetic/attempt-0004` is the fresh sealed
replacement from the corrected reader; history was not mutated.

## Principal-model implementation, not closure evidence

The runtime ledger separates sealed per-window task, IRQ, idle and
unclassified counters (including fixed-capacity task/IRQ owner buckets) from
whole-run live counters.  New directed native source cases cover OPEN/CLOSE
clipping, execution wholly outside a window, per-owner IRQ interruption, and
idle interrupted by an IRQ.  The complete STM32F446/FreeRTOS Release target
composition was cross-compiled after this change.  These are implementation
and target-build facts only: they are **not** a replacement for required host
execution, production DMA-boundary wiring, or sealed hardware directed
evidence.

## Rule for closure

R4 changes to PASS only after every Principal matrix row is backed by a
sealed native/source/target/non-regression artifact as appropriate, all
manifests verify from the final committed tree, and the complete set is pushed
to GitHub.
