# R3-W2A Worker Lifecycle Contract — Design Freeze Candidate

Reviewed baseline: `b1a385e5056d97b389079783c9ff4363d8a1f244`

## Why W2-A exists

The real repository contains several historical R2 `ProcessingTask` implementations but no production `InterferenceTask`. Copying those R2 tasks would repeat the R2 architecture mistake: lifecycle semantics would become implicit in task code before the STOP/ACK contract was independently testable.

W2-A therefore introduces one pure, target-compilable worker lifecycle contract before FreeRTOS task glue.

## Authority boundary

`r3_worker_contract` owns only:

- worker-local bound run identity;
- exact stop transaction identity;
- STOP latch / duplicate STOP semantics;
- exactly-once ACK eligibility;
- post-ACK mutation closure;
- pure Processing/Interference STOP decisions;
- notification-bit precedence.

It does **not** own:

- queue contents;
- BufferPool state;
- StreamRunAuthority lease state;
- FreeRTOS task scheduling;
- lifecycle coordinator state;
- ADC/DMA/TIM2 hardware.

The worker task remains the sole mutator of its `R3WorkerContract`. Notification bits wake a worker; the current run/stop identity must be re-read from the later coordinator API before mutating the contract.

## Processing mapping to existing authorities

When W2-B task glue is added:

- `HELD_READY + STOP` → `StreamRunAuthority_CancelHeldReady()`;
- `PROCESSING + STOP` → finish current bounded work, then `StreamRunAuthority_CompleteAndReleaseBlock()`;
- `NONE + ReadyQueue backlog + STOP` → `TakeReady()` then exact `CancelHeldReady()`;
- `NONE + no READY + STOP` → build one QUIESCED ACK.

The STOP latch closes new work even if a stale WORK bit is delivered in the same notification value.

## Interference mapping

Because no production InterferenceTask exists yet:

- PENDING + STOP → cancel pending job;
- RUNNING + STOP → stop at bounded segmentation point;
- NONE + STOP → ACK.

Actual task and segment mechanics are W2-B; W2-A freezes their lifecycle decisions first.

## Native acceptance

20 directed cases cover:

- run bind and duplicate bind;
- stale/conflicting STOP;
- STOP precedence over WORK/START;
- new-work closure;
- Processing empty/held/current/backlog cases;
- Interference pending/running/idle cases;
- ACK requires resource release;
- exact ACK identity;
- exactly-once ACK emission;
- post-ACK old-run mutation closure;
- new-generation rebind after ACK.

## Non-claims

W2-A makes no hardware or FreeRTOS scheduling claim. It is a pure lifecycle contract prerequisite for W2-B.
