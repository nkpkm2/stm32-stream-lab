# R3-W2B FreeRTOS Worker Task Glue — Principal Design Freeze Candidate

**Reviewed baseline:** `3d5cbdafdff916da2225ef0ef7305dc2b9d77629`
**Historical R2 anchor:** `48da792e382e911aad1b7bb43765284aa8b58ea2`
**Architecture baseline:** v3.2.2
**Scope:** W2 worker STOP / quiescence only

## 1. Principal findings from the real repository

The repository still has no production R3 task implementation. The preflight's
`INTERFERENCE_FILES=2` are the W2-A contract source/header mentioning the
Interference worker identity; they are **not** a production `InterferenceTask`.

Historical R2 W4/W5/W6 Processing tasks are useful oracles only. They wait on
one WORK bit and zero-wait drain their experiment-local ReadyQueue. They do not
have START/STOP lifecycle semantics and must not be copied into R3.

`main.c` currently selects historical R1/R2 bring-up profiles and then starts
the scheduler through `R0_FreeRTOS_StartSmoke()`. W2-B deliberately does not
modify `main.c`: production R3 lifecycle/Communication does not exist yet, so
creating a fake production coordinator here would violate the W1 authority
freeze. W2-C hardware validation may use a dedicated committed test harness;
W3 will supply the production lifecycle coordinator.

## 2. New module

W2-B introduces:

```text
firmware/runtime/r3_worker_tasks.h
firmware/runtime/r3_worker_tasks.c
```

This module owns only:

- the persistent FreeRTOS Processing and Interference tasks;
- their static TCB/stack storage;
- unified worker notification waits;
- task-local run-ticket caching needed for Processing authority calls;
- worker-local interference activity;
- exact one-slot ACK transport from each worker to Communication;
- the FreeRTOS wake functions used by Communication and acquisition ISR;
- deterministic fault latching for glue/infrastructure failures.

It does **not** own:

- R3 lifecycle state, current run, gates, START/STOP transactions;
- lifecycle duplicate/stale command policy;
- BufferPool/ReadyQueue semantics;
- READY/PROCESSING ownership;
- ADC/DMA/TIM2 hardware;
- result lifetime;
- final DSP or interference algorithm.

## 3. Control source rule

Notification bits are wake reasons only.

Before any lifecycle mutation the worker calls the configured `read_control`
hook and obtains one coherent view containing:

- current run validity and `boot_id/run_id/generation`;
- current `StreamRunTicket`;
- `processing_claim_allowed`;
- `interference_release_allowed`;
- current exact STOP identity, if any.

The hook is supplied by the lifecycle coordinator in W3 and by a committed
test coordinator in W2-C. Task glue never invents a run/stop identity from a
notification bit.

## 4. Mandatory serialized READY-claim boundary

A plain sequence of:

```text
read processing_claim_allowed == true
StreamRunAuthority_ClaimHeldReady()
```

is forbidden because STOP may commit in the gap.

After `StreamRunAuthority_TakeReady()` has bound the exact HELD_READY
descriptor, Processing calls `try_claim_held`.

`try_claim_held` is the mandatory lifecycle linearization boundary. In one
serialized decision it must:

1. verify current boot/run/generation;
2. inspect the current processing-claim gate;
3. if the gate is open, call `StreamRunAuthority_ClaimHeldReady()` in the
   Processing task context before releasing the serialization boundary;
4. if STOP is already current, return that exact `R3WorkerStopContext` and do
   not claim.

Therefore:

- claim linearizes first -> the block is legally PROCESSING and may finish;
- STOP linearizes first -> the exact HELD_READY descriptor is CANCELled;
- there is no gate-read / claim race.

This is the W1 `TryClaimNextBlock(...)` requirement made concrete without
moving lifecycle authority into the worker module.

## 5. Processing algorithm

Persistent Processing task:

```text
xTaskNotifyWait(WORK | STOP | START)
  -> re-read coherent lifecycle control
  -> bind/reconcile worker-local run contract
  -> if STOP current:
       COMPLETE current legal PROCESSING lease if one exists
       CANCEL exact HELD_READY if one exists
       repeated TakeReady + exact CANCEL until EMPTY
       clear run-local Processing ticket/pointers
       emit exactly one QUIESCED ACK
  -> else if WORK:
       TakeReady
       serialized try_claim_held
         GRANTED:
           run bounded processing callback
           COMPLETE exact current lease
           re-read control before another block
         STOP_CURRENT:
           latch exact STOP
           CANCEL exact held descriptor
           enter STOP drain
         CLOSED:
           CANCEL held descriptor; do not claim
```

The bounded processing callback does not own/release the buffer. Glue performs
the unique `CompleteAndReleaseBlock()` immediately after the callback returns.

If STOP commits during the bounded callback, the claim already linearized
first, so that one current block completes. No later block may be claimed.

## 6. Interference algorithm

Interference is a persistent worker with task-local activity:

```text
NONE -> PENDING -> RUNNING -> (PENDING or NONE)
```

One WORK wake creates one pending logical job. Before each bounded segment the
worker re-reads lifecycle control and calls `try_begin_interference`.

`try_begin_interference` is the segment-release linearization boundary:

- release gate first -> segment is treated as already RUNNING and may finish;
- STOP first -> pending segment is not started and is cancelled.

The worker intentionally keeps activity at RUNNING until the next control
re-read after the bounded callback returns. This makes STOP-during-segment map
to W2 `FINISH_SEGMENT`, then ACK.

## 7. ACK transport

Each worker owns one fixed ACK mailbox.

Before building its ACK the worker proves its own resources are relinquished.
The ACK is then:

1. built by the sealed W2-A contract;
2. copied into that worker's empty mailbox;
3. followed by one configured `eSetBits` notification to Communication.

Communication is the sole consumer through `R3WorkerTasks_TakeAck()`.

Lifecycle acceptance remains outside task glue. W3 must accept an ACK only
after exact matching of:

```text
boot_id, run_id, generation, stop_id, worker_id
```

A new run cannot be bound while the prior worker ACK mailbox is still pending.
This prevents old ACK storage from being silently overwritten across immediate
restart.

After the W2-A contract enters QUIESCED, stale WORK/START/STOP wakes may only
cause a control re-read; old-run ownership APIs are not called.

## 8. ISR-safe fault-latch rule

`R3WorkerTasks_NotifyProcessingWorkFromISR()` is an ISR API. Any fault path
reachable from it must use the FreeRTOS ISR critical-section API:

```text
taskENTER_CRITICAL_FROM_ISR()
taskEXIT_CRITICAL_FROM_ISR(saved_status)
```

It must never call the task-context `taskENTER_CRITICAL()` /
`taskEXIT_CRITICAL()` pair. The W2-B Native suite injects a failed ISR
notification and proves that only the ISR-safe fault latch executes.

## 9. Task creation policy

The module uses static tasks.

- Processing: `768` stack words, `tskIDLE_PRIORITY + 3`
- Interference: `512` stack words, `tskIDLE_PRIORITY + 1`

These are conservative W2 values consistent with the historical Processing
priority/stack envelope. W3 may create Communication separately at its own
reviewed priority.

Creation is **single-attempt / fail-closed**. If one static task is created and
the second creation fails, the module refuses any local retry. Reusing the same
static TCB/stack after a partial create would be unsafe.

Worker contracts are initialized by the worker tasks themselves after the
scheduler starts, preserving the W2-A rule that the worker task is the normal
mutator of its own contract.

## 10. W2-B native acceptance

The deterministic test build executes the exact production service routines
without blocking in `xTaskNotifyWait()`.

Fifteen cases are frozen:

```text
start_binds
t03a_empty_stop
t03b_work_stop_race
t03c_stop_between_take_claim
t03d_stop_during_processing
t05a_pending_stop
t05b_running_stop
ack_exactly_once
ack_wake_bits
post_ack_old_work_closed
new_run_requires_ack_consumed
new_run_after_ack_consumed
notify_caller_guard
create_partial_failure_no_retry
isr_notify_failure_uses_isr_critical
```

Principal pre-package QA:

```text
GCC strict:   15 / 15 PASS
Clang strict: 15 / 15 PASS
Production-path host strict compile: PASS
```

These tests do not substitute for W2-C real FreeRTOS/hardware directed cases.

## 11. Build integration

A new compile gate:

```text
STREAM_LAB_R3_WORKER_TASKS=ON
```

requires:

```text
STREAM_LAB_FOUNDATION_QUEUE_ADAPTER=ON
STREAM_LAB_R3_WORKER_CONTRACT=ON
```

and remains mutually exclusive with historical R2 W3/W4/W5/W6 profiles.

Historical R2 source files are not modified.

## 12. Exact W2-B patch scope

New:

```text
firmware/runtime/r3_worker_tasks.h
firmware/runtime/r3_worker_tasks.c
tests/native/test_r3_worker_tasks.c
tests/native/r3_worker_task_stubs/FreeRTOS.h
tests/native/r3_worker_task_stubs/task.h
docs/r3/w2/R3_W2B_TASK_GLUE_DESIGN.md
```

Modified:

```text
firmware/cubemx/CMakeLists.txt
tests/native/CMakeLists.txt
```

`main.c`, historical R2 sources, lower ownership authorities, formal case
catalog, and hardware drivers are not modified in W2-B.

## 13. Non-claims / next boundary

W2-B does not claim:

- production Communication/R3Lifecycle implementation;
- transactional START/rollback;
- physical DMA STOP sequencing;
- generation/idempotence protocol closure;
- W2 real-hardware acceptance;
- W6 soak.

After W2-B source validation/seal, W2-C will add the smallest committed
directed-hardware harness needed to exercise W2-T03-A/C/D and W2-T05-A/B
against these persistent worker tasks. W3 production lifecycle integration
remains a separate reviewed boundary.
