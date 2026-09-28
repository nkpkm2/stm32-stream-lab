# R4 Principal acceptance audit

Status: **IN PROGRESS — this document supersedes any narrower R4 closure
claim.**

Source: the Principal acceptance objective dated 2026-09-27.  A row is PASS
only when its specified evidence exists; implemented code or an adjacent
hardware test is not a substitute.

| Domain | Current evidence | Status | Closure required |
|---|---|---|---|
| A01–A03 Clock64 authority, >60 s wrap, service bound | `t12-soak-a/attempt-0002` and `t12-soak-b/attempt-0002` are independent current target PASSes: >=65 s, observed DWT high-word advance, and >=60 bounded monitor services; `test_r4_source_audit.py` enforces the single production Clock64 authority and boot-only initialization | PARTIAL | add invalid-run audit |
| A04–A06 RuntimeEvent atomicity/mask/error ordering | `t17-atomic/attempt-0005` sealed target PASS checks pending IRQ ordering, duplicate-exit rejection, and post-close immutability; `mask-restore/attempt-0001` witnesses PRIMASK preservation; `time-regression/attempt-0001` proves target fail-closed rejection | PARTIAL | combine these directed proofs in the final R2/R3 regression anchor |
| A07–A13 IRQ pairing, no-yield/yield, SysTick | `t17-atomic/attempt-0005` sealed real TIM7→TIM6 nesting; fresh T12 semantic attempts exercise DMA `pdTRUE` and no-event `pdFALSE` tails; `test_r4_source_audit.py` locks macro wiring, TickHook non-duplication, and one-entry/one-port-tail application handlers | PARTIAL | exact task-switch attribution and one-pair SysTick evidence |
| A14–A15 task/idle known workload | `task-synthetic/attempt-0004` sealed target PASS on the corrected ABI reader; H4 checks fixed A/B iterations, owner ratio, Idle residency, and formal conservation | PARTIAL | add known IRQ contribution and retain this workload in final integrated regression |
| A16–A24 CPU window clipping, immutability, conservation | `dma-window/attempt-0001` sealed target PASS at `cf78c83`: real DMA completion sequence opens at S0=1, seals at S1=4, and observes a later sequence >=5 without a boundary fault; `window-intersection/attempt-0001` adds target partition conservation and a post-CLOSE real SysTick/tick-hook witness with no formal-window mutation; `R4_NATIVE_RUNTIME_EXECUTION.md` records 20/20 host model cases including clipping and outside-window exclusion | PARTIAL | carry these directed proofs through the final R2/R3 regression anchor |
| A25–A29 error/overhead/phase | `R4_MEASUREMENT_ERROR_BUDGET.md` binds 33-sample RuntimeEvent costs, DWT resolution, phase and completion bounds to sealed target evidence; `mask-timing/attempt-0001` independently observes 52 actual PRIMASK-held RuntimeEvent spans, maximum 559 cycles <= 1800; `perturbation-ab/attempt-0001` through `attempt-0006` plus `suite-0001.json` seal the frozen three-pair same-source A/B protocol; T15 q0/release independently bracket physical TIM2 CEN | PARTIAL | retain the perturbation result in the final R2/R3 regression anchor |
| A30–A37 TickService and suspension | Current independent T15 q0/release target PASSes prove `SysTick enter = exit = real-hook service`, registration, DWT history, exact suspension callback/start/release/skip semantics, and q0→TIM2 CEN; `tick-gap/attempt-0001` proves a late real SysTick latches `TICK_SERVICE_GAP` fail-closed | PARTIAL | integrate these directed proofs into the final R2/R3 regression anchor |
| A38–A44 T12/T17 cutoff and formal/live separation | T12/T17 sealed attempts | PARTIAL | nested/cutoff interleaving and machine-readable formal-vs-live result split |
| A45–A51 response/utilization/critical composition | Two independent uninstrumented full-lock bound runs plus `commit-pending-irq/attempt-0001`: a real TIM6 IRQ is pended at the FreeRTOS syscall ceiling during the actual queue lock and is observed only after actual unlock; `response-synthetic/attempt-0001` records independent direct-DWT release/start/completion endpoints around a fixed worker workload and requires >=99.0% sealed owner-bucket coverage; `mask-timing/attempt-0001` separately bounds actual PRIMASK-held RuntimeEvent spans; `combined-service/attempt-0001` measures 50,781 real DMA trace-entry-to-post-`RuntimeEvent`-exit spans, maximum 10,734 <= 23,040 cycles; `R4_UTILIZATION_SCHEMA.md` freezes typed sealed-window denominator/numerator and residual treatment | PARTIAL | final integrated R2/R3 representative regression anchor |
| A52–A58 non-regression/provenance/error preservation | R4 H0–H5 identity/manifests and retained failed attempts; R3 lifecycle is now covered by fresh R4-aware W6 A/B target evidence | PARTIAL | R2 normal/drop representative target anchors plus final acceptance classifier |

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
* RuntimeEvent microbenchmark: `docs/evidence/r4/microbenchmark/attempt-0002`
  is the current sealed target PASS after the diagnostic harness was made
  fail-closed for every sample.  Its raw target words contain 33 successful
  samples per row: task checkpoint **412/417/439 cycles**, balanced IRQ
  enter/exit **897/902/902 cycles**, window open/close **876/881/2049
  cycles**, and balanced nested IRQ enter/enter/exit/exit
  **1595/1597/3358 cycles** (min/median/max).  These bracket guarded
  `RuntimeEvent_Apply` ledger sequences; the multi-event rows do not claim to
  be one PRIMASK-held interval or to include Cortex-M exception entry/return.

* Instrumentation perturbation: `docs/evidence/r4/perturbation-ab/attempt-0001`
  through `attempt-0006` are a single-source `5e333ac` NUCLEO-F446RE cohort in
  the frozen order MINIMAL, R4, R4, MINIMAL, MINIMAL, R4.  Every attempt has
  an H0--H5 manifest, >=50,781 raw DMA observations and 33 direct
  DMA-publication-to-real-Processing-worker response triplets.  The immutable
  `suite-0001.json` verifies all three pairs: 8,837/10,244-cycle DMA maxima,
  55,307/57,736-cycle response medians, 55,542/59,427-cycle response maxima,
  identical completion/admission counts, and the required R4 sealed
  CPU-partition versus MINIMAL-unavailable distinction.  This closes A/B
  perturbation only; it does not substitute for the required R2/R3 anchors.

* R4-aware R3 lifecycle non-regression: the first schema-2 cohort is
  `docs/evidence/r3/w6/w6-a/attempt-0002` and
  `docs/evidence/r3/w6/w6-b/attempt-0002`, both built from the clean,
  pushed commit `17d137b`.  Each H4 record checks 500/500 real production
  START/STOP cycles, a successful R4 checkpoint after every stopped cycle,
  a sealed nonzero R4 window with exact task+IRQ+Idle+residual conservation,
  healthy runtime state, and a nonzero event serial.  A is K=8 normal
  lifecycle traffic (raw aggregate `KEEP=0`, `REBIND=500`); B is K=1 with
  processing notification withheld (raw aggregate `KEEP=2500`,
  `REBIND=500`).  The next schema-2 rerun adds those A normal-path values as
  explicit H4 predicates; neither cohort substitutes for the still-required
  long R2 normal/drop anchors.

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
