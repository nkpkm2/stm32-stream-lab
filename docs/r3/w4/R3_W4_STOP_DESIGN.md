# R3 W4 safe STOP design

**Status:** implementation baseline; not an acceptance record.

## Hardware boundary

`AdcDbmDriver` remains the only owner of ADC1, DMA2 Stream0 and TIM2. W4
splits its previous monolithic task-context stop into two explicit operations:

1. `AdcDbmDriver_BeginStop()` is the bounded Communication-task stop edge. It
   first changes the driver to `STOPPING`, disables TIM2 triggering and DMA
   completion/error interrupt sources, and records `NDTR`, CT and error flags.
   `DispatchCompletion()` accepts normal work only while the state is
   `RUNNING`; a late or pending IRQ after this point is therefore suppressed.
2. `AdcDbmDriver_FinishStop()` is the later bounded HAL quiescence operation.
   It stops DMA/ADC, verifies DMA EN is clear, then clears DMA/NVIC state and
   records final diagnostics. It leaves `STOPPED` only on complete quiescence;
   otherwise it leaves `ERROR`.

The legacy `AdcDbmDriver_Stop()` remains a compatibility wrapper only. New R3
paths must use the split operations and must not write acquisition registers.

## R3 transaction order

`R3Lifecycle_RequestStop()` closes the R3 admission gates before invoking the
runtime rollback hook. The hook performs this order for a driver-owned run:

1. `BeginStop()` records the STOP edge and suppresses any normal completion.
2. Publish the exact STOP identity to persistent workers and wait for both
   resource-relinquishment ACKs. Processing alone drains/cancels READY work;
   a worker which already owns PROCESSING may finish and return it.
3. `FinishStop()` performs the final hardware quiescence.
4. Reset the run authority and ownership only after the preceding operations
   have succeeded.

Both begin and final driver reports are retained in `R3W3RuntimeSnapshot` for
target-harness evidence. The default runtime keeps all W4-only harness knobs
disabled.

## Evidence still required

This design does not satisfy W4 by itself. Hardware evidence remains required
for T04-A/B and STOP-A/B/C/D/E, with native evidence additionally required for
STOP-A/B/D. The forthcoming W4 harness will expose the reports above and prove
that no partial or pending completion becomes READY in a later run.
