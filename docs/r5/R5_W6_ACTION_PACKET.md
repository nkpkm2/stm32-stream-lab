# R5 W6 hardware action packet — not authorized

Status: **BLOCKED — do not flash, reset, run, or retry hardware.**

## Entry gates not yet satisfied

1. R4 is Principal-review ready with an accepted engineering deviation, but is
   not formally CLOSED.  The R5 directive prohibits formal R5 hardware
   acceptance before R0–R4 are closed.
2. `r5_run_metrics` has no production connection to the real DMA completion
   callback, `StreamRunAuthority`/`StreamQueueAdapter`, worker logical commit,
   RuntimeEvent serial, or lifecycle stop gate.  The existing R5 hardware
   harness is synthetic only and cannot be used as H01–H08 evidence.

## Required wiring before an authorized physical run

- At each integrity-checked real DMA input, obtain one RuntimeEvent serial and
  pre-admission Q, then call
  `R5RunMetrics_OnInputWithOccupancy(seq, t_irq, serial, free, Q)` before the
  real admission decision is committed.
- At the successful `COMPLETE` logical commit, call `OnCompletion` with that
  block's input sequence, the formal `t_commit`, and a serial ordered with the
  cutoff transaction.
- At S2, invoke the existing metrics cutoff branch before any FREE take,
  rebind, READY publication, normal drop or occupancy observation.  It must
  establish the stop gate and request worker cleanup after bounded outcome
  closure.
- After DMA quiescence, worker acknowledgement and integrity checks, call
  `Seal`, then copy the sealed result to `R5ResultStore` for transport.
- Expose config identity, run/boot identity, counts, histogram/P99 status,
  outcome status, and separately named live diagnostics in the result schema.

## Required preflight for any future H01–H08

- Principal declares R0–R4 CLOSED and authorizes exactly the specified physical
  experiment.
- Correct source commit, clean tree, target profile, ELF/programmed-image
  identity, board/clock and harness versions are captured before flash.
- Host execution allowance exceeds target soak plus readback, with no automatic
  retry.
- An attempt directory is allocated before flash; it records raw readback,
  manifest and classification.  A failure remains immutable.

## Current result

`HARDWARE_OPERATION = NONE`.  This packet authorizes nothing by itself.
