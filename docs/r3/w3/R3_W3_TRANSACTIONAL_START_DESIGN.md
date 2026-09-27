# R3-W3 Transactional START — Concrete Integration Design

## Scope

This package turns the previously pure `R3Lifecycle` state machine into the
actual R3 Communication-task path.  It does not close W4 STOP, W5 command
isolation, or W6 soak acceptance.

The implementation is `r3_w3_runtime.c`.  Its public operations are accepted
only from the FreeRTOS task which called `R3W3Runtime_Initialize`; that task is
the Communication coordinator.  Workers and DMA ISR callbacks consume a
read-only control view and cannot create/commit/revoke a run ticket.

## Start ordering

```text
Communication task
  -> R3Lifecycle_PrepareStart(ticket)
     -> StreamOwnership initialize K+2
     -> StreamRunAuthority + queue adapter initialize with boot/run/generation
     -> bind Processing task; publish run control with all gates closed
     -> wake and prove both persistent workers RUN_BOUND
     -> AdcDbmDriver arm static SRAM M0/M1; TIM2 remains stopped
  -> R3Lifecycle_CommitStart(ticket)
     -> publish all lifecycle gates
     -> AdcDbmDriver_CommitStart starts TIM2
     -> state RUNNING and ticket is revoked
```

The target harness records the PREPARING view before commit and requires all
three gates closed, ADC/DBM ARMED, hardware ownership present, and TIM2 CEN
clear.  The normal-start record then requires RUNNING, all gates open, driver
RUNNING, ownership present, and TIM2 CEN set.

The DMA completion callback uses the real `AdcDbmDriver` callback chain.  It
receives one FREE token when available, derives a logical REBIND plan, performs
the physical inactive-MxAR commit, commits logical ownership, publishes READY,
and wakes Processing.  With no FREE token it executes the explicit hardware
KEEP/controlled-DROP path and matching `StreamOwnership_CommitKeep`.  Thus
every completed DMA event has one bounded disposition and W4 can exercise a
real READY backlog/current Processing lease; this remains distinct from R7 DSP
throughput acceptance.

## Rollback ordering

Any prepare/arm/commit failure invokes the single lifecycle rollback hook:

```text
close lifecycle gates
  -> stop owned ADC/DBM/TIM2 if armed or running
  -> install exact STOP identity and notify both workers
  -> require matching QUIESCED ACKs
  -> reset RunAuthority/queue adapter offline
  -> reset ownership model and clear current run control
  -> IDLE, otherwise RESET_REQUIRED
```

The arm and final-commit target cases use explicit harness selectors so fault
paths are observable and reproducible.  Those selectors are test-only
configuration and cannot be enabled by an ordinary runtime request.

## IRQ ownership

For `STREAM_LAB_R3_LIFECYCLE`, `DMA2_Stream0_IRQHandler` does not enter the
historical R1 acquisition state machine.  It invokes HAL DMA dispatch only;
the callbacks installed by `AdcDbmDriver_Arm` are the R3 hardware path.

## Required evidence still pending

`START_A`, `T06_A`, `T06_B`, and `START_C` must each receive a clean-source,
single-execution H0--H5 hardware attempt.  Native cases cover invalid config,
prepare/arm/commit rollback, stale tickets, and rollback failure.  A successful
compile or target harness build is not a hardware PASS.
