# R4 Principal acceptance audit

Status: **IN PROGRESS — this document supersedes any narrower R4 closure
claim.**

Source: the Principal acceptance objective dated 2026-09-27.  A row is PASS
only when its specified evidence exists; implemented code or an adjacent
hardware test is not a substitute.

| Domain | Current evidence | Status | Closure required |
|---|---|---|---|
| A01–A03 Clock64 authority, >60 s wrap, service bound | `t12-soak-a/attempt-0002` and `t12-soak-b/attempt-0002` are independent current target PASSes: >=65 s, observed DWT high-word advance, and >=60 bounded monitor services | PARTIAL | add source-uniqueness and invalid-run audit |
| A04–A06 RuntimeEvent atomicity/mask/error ordering | `t17-atomic/attempt-0005` sealed target PASS checks pending IRQ ordering, duplicate-exit rejection, and post-close immutability; `mask-restore/attempt-0001` witnesses PRIMASK preservation; `time-regression/attempt-0001` proves target fail-closed rejection | PARTIAL | combine these directed proofs in the final R2/R3 regression anchor |
| A07–A13 IRQ pairing, no-yield/yield, SysTick | `t17-atomic/attempt-0005` sealed real TIM7→TIM6 nesting; fresh T12 semantic attempts exercise DMA `pdTRUE` and no-event `pdFALSE` tails | PARTIAL | exact task-switch attribution and one-pair SysTick evidence |
| A14–A15 task/idle known workload | `task-synthetic/attempt-0004` sealed target PASS on the corrected ABI reader; H4 checks fixed A/B iterations, owner ratio, Idle residency, and formal conservation | PARTIAL | add known IRQ contribution and retain this workload in final integrated regression |
| A16–A24 CPU window clipping, immutability, conservation | `dma-window/attempt-0001` sealed target PASS at `cf78c83`: real DMA completion sequence opens at S0=1, seals at S1=4, and observes a later sequence >=5 without a boundary fault; per-window buckets and directed native source cases remain available | PARTIAL | execute native cases on a host and add final target intersection/conservation plus no-outside-window evidence |
| A25–A29 error/overhead/phase | `R4_MEASUREMENT_ERROR_BUDGET.md` binds 33-sample RuntimeEvent costs, DWT resolution, phase and completion bounds to sealed target evidence; T15 q0/release independently bracket physical TIM2 CEN | PARTIAL | same-configuration perturbation A/B and complete combined ISR/DBM service budget |
| A30–A37 TickService and suspension | Current independent T15 q0/release target PASSes prove `SysTick enter = exit = real-hook service`, registration, DWT history, exact suspension callback/start/release/skip semantics, and q0→TIM2 CEN; `tick-gap/attempt-0001` proves a late real SysTick latches `TICK_SERVICE_GAP` fail-closed | PARTIAL | integrate these directed proofs into the final R2/R3 regression anchor |
| A38–A44 T12/T17 cutoff and formal/live separation | T12/T17 sealed attempts | PARTIAL | nested/cutoff interleaving and machine-readable formal-vs-live result split |
| A45–A51 response/utilization/critical composition | full lock bound twice | PARTIAL | known response-time test, utilization schema, RuntimeEvent masking bound, pending-IRQ-at-lock case |
| A52–A58 non-regression/provenance/error preservation | R4 H0–H5 identity/manifests; failed attempts retained | PARTIAL | R2 normal/drop and R3 lifecycle representative target anchors plus acceptance classifier |

## Evidence already sealed

* T12: `docs/evidence/r4/t12-soak-a/attempt-0002` and
  `docs/evidence/r4/t12-soak-b/attempt-0002`. Both current H4 records verify
  wrap/duration, lifecycle, DMA `pdTRUE`, no-event `pdFALSE`, and Clock64
  health service.
* T15: current target passes are `docs/evidence/r4/t15-q0/attempt-0004` and
  `docs/evidence/r4/t15-release/attempt-0005`. Each H4 oracle proves one
  physical SysTick enter/exit per real hook service and directly brackets the
  planned-q0 physical TIM2 CEN write, in addition to its suspension contract.
  Earlier attempts remain immutable history.
* Tick DWT integrity: `docs/evidence/r4/tick-gap/attempt-0001` deliberately
  withholds the physical SysTick IRQ past its configured interval limit. Its
  H4 oracle verifies the next real hook latches `TICK_SERVICE_GAP` and requests
  fail-closed; it does not manufacture a hook call or repair `service_seq`.
* T17: `docs/evidence/r4/t17-atomic/attempt-0005` (current semantic H4
  verifies pending-IRQ ordering, real nesting, duplicate-exit rejection, and
  sealed-window immutability). Earlier attempts remain immutable history.
* RuntimeEvent mask restoration: `docs/evidence/r4/mask-restore/attempt-0001`
  is a sealed NUCLEO-F446RE target PASS from commit
  `06d742c7553e0069799c01686daf9bd57a18695f`.  Its H4 oracle requires one
  production Apply transaction entered at PRIMASK=0 to return at PRIMASK=0,
  another entered at PRIMASK=1 to return at PRIMASK=1, and BASEPRI to remain
  unchanged.  The existing T17 pending-high-IRQ case supplies the third
  interleaving witness; this row does not claim to test a time reversal.
* Time-regression rejection: `docs/evidence/r4/time-regression/attempt-0001`
  is a sealed NUCLEO-F446RE target PASS from commit
  `7d5884c547caddd8cdf720f1f81eceaeb450e66b`.  A diagnostic-only corruption
  advances the ledger boundary while leaving Clock64 as the sole authority;
  the next normal target Apply returns `R4_RUNTIME_TIME_REGRESSION`, a second
  Apply returns the same latched status, and the event serial remains frozen.
  It therefore proves invalid formal timing results are fail-closed rather
  than becoming an unsigned underflow interval.
* Full `t_lock` to `t_unlock` bound: Release/LTO
  `t04-commit-budget-a/attempt-0002` and
  `t04-commit-budget-b/attempt-0001`.
* RuntimeEvent microbenchmark: `docs/evidence/r4/microbenchmark/attempt-0001`
  is a sealed target PASS from commit `3ed3df7084d3c0b553151be2e569c8e3ddb08366`
  (ELF SHA-256 `ABAF380D73F9F16ECCFB35D8A0DF3F1BDFD0521A244632B53EAFC020B3DE2FD4`).
  Its raw target words contain 33 samples per row: task checkpoint
  **308/308/379 cycles**, balanced IRQ enter/exit **778/784/906 cycles**,
  window open/close **691/691/822 cycles**, and balanced nested IRQ
  enter/enter/exit/exit **1478/1484/1600 cycles** (min/median/max).  These
  bracket the complete guarded `RuntimeEvent_Apply` ledger transaction.  The
  IRQ rows intentionally measure ledger-path cost only; they do not claim to
  include Cortex-M exception-entry or exception-return machine cycles.

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
