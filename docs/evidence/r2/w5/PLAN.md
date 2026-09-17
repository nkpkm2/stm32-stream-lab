# R2-W5 Directed Capacity-Exhaustion Plan

## Purpose

R2-W5 validates the controlled capacity-drop branch on real STM32 hardware.
It does not change the R1 acquisition baseline, the W1 ownership model, or the
W2 M0/M1 slot semantics.

## Fixed diagnostic configuration

- sample rate: 200 kS/s
- block size: 256 samples
- K: 1
- active physical buffers P: 3
- FreeBufferQueue capacity: 1
- ReadyQueue capacity: 1
- bounded hardware input events: 64
- Processing hold: 6 block periods before each normal completion release
- input: PA0 / ADC1_IN0 connected to GND

## Directed mechanism

The first successful admission consumes the single FREE token and moves the
completed buffer to Processing. Processing deliberately holds that buffer while
remaining preemptible by DMA and kernel interrupts. During the hold interval,
FreeBufferQueue is empty for several consecutive DMA completions.

For each completion with no FREE token, the ISR must:

1. classify one capacity drop;
2. leave the inactive hardware MxAR unchanged;
3. leave the software M0/M1 mapping unchanged;
4. leave the completed physical buffer DMA_OWNED;
5. publish no READY descriptor;
6. issue no Processing notification for the dropped block;
7. continue DMA operation without TE/DME/FE or ADC overrun;
8. meet the same nominal-to-drop-decision and ISR-exit service budgets.

When Processing later releases its current buffer, a later completion must be
admitted again and normal DMA -> READY -> PROCESSING -> FREE reuse must resume.

## Required evidence

The bounded run must establish all of the following:

- exactly 64 input DMA completion events are classified;
- admitted + capacity_drop = input;
- at least 4 admissions;
- at least 16 capacity drops;
- at least one streak of 3 or more consecutive drops;
- at least 3 successful admissions after a prior drop streak;
- drop events never write M0AR/M1AR;
- mapping epoch does not advance on drop events;
- dropped buffers remain DMA_OWNED;
- successful admissions still update only the inactive MxAR;
- no duplicate ownership or token-ledger error;
- no ReadyQueue send failure;
- no DMA TE/DME/FE and no ADC OVR;
- nominal completion to rebind/drop-decision <= 0.25 TB;
- nominal completion to ISR end <= 0.35 TB;
- all admitted blocks complete the normal controlled release path;
- final stable state is one FREE buffer plus two DMA_OWNED buffers;
- canaries and all samples from admitted blocks remain valid.

## Boundary

R2-W5 proves a directed K=1 capacity-exhaustion/drop path. It is not the final
mandatory K matrix. R2-W6 must still exercise K = 1, 2, 4, 8 under the required
success/drop/control-load matrix before R2 can be accepted.
