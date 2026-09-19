# R3 Code Skeleton — W1 Freeze

No target code is created in W1. This file freezes the intended module boundaries before implementation.

## Planned new target modules

```text
firmware/runtime/
  r3_lifecycle.h
  r3_lifecycle.c
  r3_worker_contract.h
  r3_worker_contract.c

firmware/
  <target task-glue file chosen during W2 patch>
```

`r3_lifecycle` owns lifecycle state/transactions/gates only. It does not own queue transport, buffer ownership or ADC/DMA registers.

`r3_worker_contract` owns generation-aware worker command/ACK records and pure validation helpers. It does not own FreeRTOS scheduling.

Task glue owns FreeRTOS waits/notifications and calls the above contracts.

## Planned build profile

A new R3 lifecycle build profile must be mutually exclusive with historical R2 experiment profiles. It enables the already-sealed foundation modules instead of copying R2 W4/W5/W6 code.

Conceptually:

```text
STREAM_LAB_R3_LIFECYCLE=ON
    requires:
      AdcDbmDriver
      StreamOwnership
      StreamTokenLedger
      StreamQueueAdapter
      StreamRunAuthority
      R3 lifecycle modules
      four persistent application tasks
```

Historical R2 profiles remain immutable oracles.

## Planned coordinator API shape

Exact spelling may change only during reviewed W2/W3 patching; responsibilities may not.

```c
R3Status R3Lifecycle_PrepareStart(
    const R3StartRequest *request,
    R3StartTicket *ticket);

R3Status R3Lifecycle_CommitStart(
    const R3StartTicket *ticket);

R3Status R3Lifecycle_RequestStop(
    const R3StopRequest *request,
    R3StopTicket *ticket);

R3Status R3Lifecycle_RecordWorkerAck(
    const R3WorkerQuiescedAck *ack);

R3Status R3Lifecycle_GetSnapshot(
    R3LifecycleSnapshot *out);
```

## Worker command shape

```c
#define R3_WORK_BIT   (...)
#define R3_STOP_BIT   (...)
#define R3_START_BIT  (...)
```

Notification bits are wake reasons only. Current generation/ticket is the source of truth.

## Processing claim helper shape

Later Processing implementation must have one serialized decision point equivalent to:

```c
TryClaimNextBlock(current_run, stop_gate, ...)
```

STOP wins if its gate committed first. A dequeued-but-unclaimed descriptor is CANCELled. A block already successfully claimed may complete/release.

## Hardware stop shape

W4 must refactor `AdcDbmDriver_Stop()` into an equivalent two-stage boundary:

```c
AdcDbmDriver_BeginStop(...)
AdcDbmDriver_FinishStop(...)
```

Communication never directly performs the driver's TIM2/DMA register operations.

## Explicit non-goals

No Clock64, RuntimeEvent, measurement cohort, model prediction, final DSP, product GUI, Ethernet or unrelated refactor in R3 lifecycle implementation.
