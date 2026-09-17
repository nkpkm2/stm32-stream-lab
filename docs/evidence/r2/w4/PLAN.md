# R2-W4 READY / PROCESSING / FREE Round-Trip Plan

## Scope

R2-W4 adds the smallest real FreeRTOS consumer path on top of the W3 dynamic inactive-slot rebinding baseline.

It proves the ownership cycle:

`DMA_OWNED -> READY -> PROCESSING -> FREE -> DMA_OWNED`

for repeated physical-buffer reuse under the mandatory 200 kS/s, N=256 baseline.

## Bounded diagnostic configuration

- K = 4
- P = K + 2 = 6 physical buffers
- FreeBufferQueue capacity = 4
- ReadyQueue capacity = 4
- 64 admitted/completed blocks
- Processing work = metadata checks plus a 256-sample integer range scan
- no DSP, FFT, interference load, communication load, watchdog campaign, or model validation

## Queue and ownership rules

- FreeBufferQueue stores only buffer IDs.
- ReadyQueue stores only a compact block descriptor.
- The DMA ISR obtains replacement IDs only from FreeBufferQueue.
- The DMA ISR publishes completed descriptors only after the inactive MxAR hardware write and W1/W2 commits succeed.
- Processing receives ReadyQueue entries with zero wait after a task notification wakes it.
- READY -> PROCESSING is serialized with a short task critical section.
- PROCESSING -> FREE is committed only inside the FreeRTOS V11.1.0 `traceQUEUE_SEND` hook associated with a declared COMPLETE operation.
- The successful FreeBufferQueue send then copies the token before the queue critical section is released.
- INIT and COMPLETE are the only permitted W4 FreeBufferQueue send sources. W4 does not yet implement CANCEL.

## Safety boundary

W4 treats an empty FreeBufferQueue as an unexpected failure. The controlled capacity-drop branch is intentionally deferred to R2-W5.

W4 does not claim complete R2 acceptance. It does not yet prove deliberate FreeBufferQueue exhaustion, continuous drops, or the final mandatory K matrix.
