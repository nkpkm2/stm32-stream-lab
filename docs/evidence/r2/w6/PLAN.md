# R2-W6 Mandatory K Matrix Plan

## Purpose

R2-W6 closes the mandatory backlog-capacity matrix without introducing a new
runtime subsystem. It reuses the W1 BufferPool, W2 DMA-slot model, W4 normal
ownership round trip, and W5 controlled capacity-drop semantics.

The matrix covers all mandatory capacities:

- K = 1, P = 3
- K = 2, P = 4
- K = 4, P = 6
- K = 8, P = 10

Each K is exercised in two compile-time diagnostic modes:

- NORMAL: no deliberate Processing hold; every input block must be admitted,
  processed, released, and reused with zero capacity drops.
- DROP: Processing deliberately holds one buffer for K+5 block periods so the
  K FREE tokens are exhausted and repeated controlled drops occur before
  admission resumes.

This produces eight independently identified firmware builds/runs.

## Fixed hardware envelope

- STM32 NUCLEO-F446RE
- 180 MHz core clock
- 200 kS/s ADC rate
- N = 256 samples
- nominal block period = 1.28 ms
- nominal block interval = 230400 cycles
- 96 DMA transfer-complete input events per matrix cell
- PA0 / ADC1_IN0 connected to GND for deterministic raw-range validation
- FreeBufferQueue capacity = K
- ReadyQueue capacity = K
- active physical buffers = K+2
- all remaining build-time buffers = INACTIVE

## NORMAL-mode acceptance

For each K = 1, 2, 4, 8:

- exactly 96 input events;
- 96 admissions;
- zero capacity drops;
- 96 Processing claims;
- 96 controlled completion releases;
- every admitted block validates 256 12-bit samples and buffer canaries;
- no FreeBufferQueue receive failure;
- no ReadyQueue send failure;
- no notification failure;
- no token-ledger error;
- no BufferPool or DMA-slot violation;
- no DMA TE/DME/FE or ADC OVR;
- final state contains K FREE buffers and two DMA_OWNED buffers;
- ReadyQueue is empty and FreeBufferQueue depth is K;
- nominal completion to rebind decision <= 0.25 TB;
- nominal completion to ISR end <= 0.35 TB;
- protected final write window remains within its audited bound.

## DROP-mode acceptance

For each K = 1, 2, 4, 8:

- exactly 96 input events;
- admitted + capacity_drop = 96;
- at least K+3 admissions;
- at least 8 controlled capacity drops;
- at least one drop streak of length 3 or greater;
- at least 3 admissions after a prior drop streak;
- every DROP leaves M0AR/M1AR unchanged;
- every DROP leaves the software mapping epoch unchanged;
- every DROP leaves the completed physical buffer DMA_OWNED;
- no dropped block is published to ReadyQueue or enters Processing;
- admissions resume after capacity becomes available again;
- all admitted blocks complete the normal READY -> PROCESSING -> FREE path;
- all owner/token/DMA/error/timing invariants from NORMAL mode remain valid;
- final stable state contains K FREE buffers and two DMA_OWNED buffers.

## Build identity

The W6 firmware is one parameterized implementation. Each matrix cell must have
an independent build directory and ELF identity containing:

- STREAM_LAB_R2_W6=ON
- STREAM_LAB_R2_W6_K in {1,2,4,8}
- STREAM_LAB_R2_W6_MODE in {NORMAL,DROP}

The eight target builds must not be conflated into one artifact identity.

## Required evidence

For every hardware cell preserve at least:

- source commit / pre-checkpoint HEAD;
- K and mode;
- ELF SHA256;
- exact flash/verify result;
- input/admission/drop/process/release totals;
- maximum consecutive drop streak and recovery count;
- M0/M1 final mapping;
- final owner and queue-token conservation;
- DMA/ADC/error counters;
- service-latency maxima;
- sample/canary validation;
- machine-readable event trace or equivalent bounded summary.

A matrix summary must compare all eight cells and verify that each mandatory K
has both a no-drop success case and a directed continuous-drop case.

## Boundary

R2-W6 closes the mandatory K matrix only. Architecture v3.2.2 T11 also requires
service-margin evidence under the worst permitted control traffic, and R2
closeout requires stress/soak evidence in addition to directed edge tests.
Those final R2 acceptance items are not silently claimed by this bounded matrix
and must be addressed before `r2-pass` can be created.
