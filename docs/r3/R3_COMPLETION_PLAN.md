# R3 Completion Plan — Verified, Evidence-Backed Delivery

## Purpose and status

This plan turns the v3.2.2 R3 requirement into a release gate:

> complete start/stop, empty wait, pending work, failure rollback, and the
> directed interleavings; then repeat start/stop at least 1000 times.

It is a delivery plan, not evidence of a pass.  A package is complete only
when its source, host/native checks, target build record, required real-board
attempts, acceptance record, and GitHub push are all present.  A host-only
test never upgrades a hardware-required matrix cell to PASS.

Current assessed state at this plan's creation:

| Package | State | Basis |
|---|---|---|
| W1 specification and evidence rules | SEALED | R3 directed matrix and evidence schema are committed. |
| W2 worker STOP/quiescence | SEALED | Five required hardware cases have immutable, imported PASS attempts. |
| W3 transactional START | SEALED | Native lifecycle checks and four imported real-board PASS attempts are indexed by the W3 acceptance decision. |
| W4 safe STOP | SEALED | Seven imported real-board PASS attempts plus native STOP contract/task-glue/lifecycle checks are indexed by the W4 acceptance decision. |
| W5 isolation and immediate restart | SEALED | Command/result ledger, native negative/interleaving suite, and six imported board PASS attempts are indexed by the W5 acceptance decision. |
| W6 1000-cycle lifecycle | SEALED | Two imported target attempts contain exactly 500 K8/NORMAL + 500 K1/DROP raw lifecycle records, all host-verified. |

## Non-negotiable acceptance rules

1. The Communication task is the sole lifecycle coordinator.  ISR and worker
   paths cannot independently create, commit, or retire a run.
2. Before TIM2 is enabled, the run identity, generation, ownership authority,
   queues, worker control view, and publication/claim/release gates are
   completely installed and observable.
3. Any start-stage failure reaches a demonstrated inverse rollback state with
   TIM2 stopped, ADC/DMA quiesced, no live sample-memory access, and no
   half-IDLE state.  If that cannot be proven, the result is RESET_REQUIRED,
   not PASS.
4. STOP closes new claims first, permits only the contractually allowed
   completion, obtains identity-bound QUIESCED acknowledgements, and reconciles
   all K+2 buffers before IDLE or restart.
5. Commands, worker notifications, acknowledgements, and result references are
   bound to boot/run/generation.  Duplicates are idempotent; stale input is
   fail-closed and cannot mutate the current generation.
6. Evidence is append-only per execution attempt.  Hardware-affecting failure
   ends that attempt; the operator may create a later attempt, but tooling must
   never overwrite a prior result or silently rerun it.
7. A package is pushed only after its evidence manifest hashes, acceptance
   decision, and source revision agree.  Each meaningful verified checkpoint is
   committed and pushed to `origin/main`.

## Work packages and exit criteria

### W3 — transactional START and rollback

Implementation sequence:

1. Wire `R3Lifecycle` into an R3 Communication runtime profile in `main.c`.
   The profile owns static K+2 sample memory, `StreamRunAuthority`, worker task
   creation/control, and `AdcDbmDriver` setup; it must not use the W2 harness
   shim as its production path.
2. Implement lifecycle hooks with concrete prepare, arm, timer-commit, and
   rollback responsibilities.  Rollback invokes the real ADC/DBM stop path,
   revokes the ticket/authority, closes all gates, and leaves a diagnostic
   snapshot suitable for external readback.
3. Add a W3 target harness with explicit fault injection at ADC/DBM arm and
   timer commit, and a pre-commit STOP interleave.  The harness must report
   lifecycle and driver snapshots, not merely a Boolean result.
4. Extend native tests for normal start, invalid configuration with no mutation,
   arm rollback, pre-commit stop, commit rollback, stale ticket/generation, and
   rollback failure to RESET_REQUIRED.

Required matrix cells: W3-START-A/B/C/D and W3-T06-A/B.  Hardware PASS is
required for START-A, START-C, T06-A, and T06-B.  The target build must use the
real STM32F446 ADC/DMA/TIM2 driver path.  START-B and START-D require native
negative evidence and are not a substitute for the hardware cells.

Exit: all six cells have a signed acceptance decision, required hardware cells
have H0--H5 records, and a clean, pushed commit identifies the exact firmware
ELF used by each attempt.

### W4 — safe STOP, partial DMA, and pending IRQ

Implementation sequence:

1. Add the Communication-owned STOP transaction: close gates, signal workers,
   collect only matching acknowledgements, quiesce ADC/DBM, and reconcile pool,
   queue, lease, and driver state before IDLE.
2. Integrate `AdcDbmDriver_Stop` reports with the lifecycle status.  A partial
   block is diagnostic-only; it cannot gain a sequence number or READY entry.
3. Build deterministic hardware injection points for half-filled DMA, pending
   TC at the stop edge, K1 DROP/recovery, duplicate STOP during quiesce, and
   ADC/DMA error during quiesce.
4. Add native state-machine tests for READY backlog, current processing lease,
   and duplicate STOP identity behavior.

Required matrix cells: W4-T04-A/B and W4-STOP-A/B/C/D/E.  Hardware PASS is
required for T04-A/B and STOP-A/B/C/D/E; native evidence is additionally
required for STOP-A/B/D.  Any error-quiesce outcome must prove either complete
safe reconciliation or RESET_REQUIRED/INVALID; cosmetic local repair fails.

Exit: all seven cells PASS with matching stop report, worker acknowledgements,
driver state, and K+2 reconciliation evidence.

### W5 — generation isolation, idempotence, and immediate restart

Implementation sequence:

1. Implement the Communication command ledger for request binding, duplicate
   START/STOP replay, request conflict, old-boot rejection, and generation
   checks.  Make decisions observable in the target result record.
2. Bind worker notifications and ACK acceptance to the active ticket; ensure a
   worker returning after its QUIESCED ACK cannot write either the old or new
   run context.
3. Implement immutable result ownership with bounded RESULT_BUSY while TX or
   storage holds a reference, followed by demonstrable release and real next
   START acceptance.
4. Add exact interleaving harness points, especially final ACK -> new START ->
   old worker returns from ACK API.

Required matrix cells: W5-T13-A through W5-T13-G and W5-T20-A/B.  Hardware PASS
is required for T13-A/C/D/G and T20-A/B; all remaining cells require native
negative/interleaving evidence.  No persistent RESULT_BUSY workaround can pass
T13-G or T20-B.

Exit: all nine cells PASS, with per-cell records proving the relevant identity,
generation, result-reference count, and no stale mutation.

### W6 — repeated lifecycle and 1000-cycle soak

Precondition: every required W3--W5 directed cell is PASS on the exact or a
newer source revision, and no unreviewed target-affecting difference exists.

1. Freeze the W6 harness parameters, deterministic stop phase sweep, external
   timeout bound, reset/recovery policy, and machine-readable per-cycle schema.
2. Run 500 consecutive hardware cycles for anchor A (`K8/NORMAL/N256/200 kS/s`)
   and 500 consecutive hardware cycles for anchor B (`K1/DROP/N256/200 kS/s`).
   A third C (`K4/NORMAL/N512/200 kS/s`) smoke run is retained as an additional
   compatibility check, not a replacement for A or B.
3. For every cycle record boot/run/generation, stop placement, DMA EN state,
   worker-quiesced state, K+2 reconciliation, and driver/owner/ledger fault
   state.  Preserve abort/reset/invalid cycles rather than relabelling them.
4. Produce summary counts directly from the immutable per-cycle data and have
   the host verifier reject missing, duplicate, stale, or out-of-order records.

Exit: exactly 500 valid A + 500 valid B cycles, every required invariant true,
no omitted cycle evidence, hashes verified, and the summary plus raw records
are committed and pushed.  A failure ends that hardware attempt; it is not
hidden by resuming an aggregate count.

## Evidence, review, and GitHub workflow

For each hardware attempt, the harness creates external H0--H5 records before
importing a copy into `docs/evidence/r3/<work-package>/<case>/attempt-NNNN/`:

- H0: immutable test intent, source and tool identity, board/probe identity,
  timeout and fault selector;
- H1: configure/build logs, cache, ELF/map/symbol audit, and firmware hashes;
- H2: flash/connect/readback identity;
- H3: one execution log and raw target result readback;
- H4: machine-verifier result and invariant evaluation;
- H5: acceptance decision, manifest hashes, and import provenance.

Before every hardware run: require clean worktree, current commit pushed, exact
case selector, probe identity, build audit, and no unfinished prior execution
for that attempt directory.  After it: validate manifest, review the acceptance
decision, commit source/evidence/status together where applicable, then push.
Host/native tests, strict ARM compilation, and real target builds run on every
source checkpoint; results are recorded in the associated evidence or commit
message.  Hardware evidence is never manufactured from host simulation.

## Completion definition for R3

R3 may be marked PASS only when W2, W3, W4, W5, and W6 all meet their exits;
the repository is clean; the final evidence verifier passes from a fresh
checkout; the final firmware and evidence commits are on GitHub; and
`docs/status.md` identifies the final commit and evidence index.  R4--R7 remain
separate gates and are not implied by an R3 PASS.

**Final decision:** these conditions are satisfied; see
[`R3_FINAL_ACCEPTANCE.md`](R3_FINAL_ACCEPTANCE.md).
