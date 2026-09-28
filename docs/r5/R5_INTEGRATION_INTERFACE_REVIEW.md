# R5 integration interface review

Status: `STOP_LOWER_LAYER_OR_ARCHITECTURE_BLOCKER` for production wiring only.
This does not invalidate the R5 host core or its known-truth evidence.

## Observed source facts

`r3_w3_runtime.c:CompleteCallback` is the real DMA completion callback.  It
can call `StreamRunAuthority_TakeFreeFromISR`, but it has no approved
non-destructive pre-admission Q/FREE observation and receives no RuntimeEvent
receipt for every input event.

`r3_worker_tasks.c:CompleteProcessing` reaches the real
`StreamRunAuthority_CompleteAndReleaseBlock` transaction.  The actual formal
commit is inside `stream_queue_adapter.c`'s protected `traceQUEUE_SEND` hook.
After the worker call returns, any clock read is **not** `t_commit` and cannot
be used for R5 deadline classification.

R4's private ledger does create serial receipts, but the target surface
currently discards them for ordinary DMA inputs and its completion API exports
only timing snapshots, not a per-COMPLETE receipt consumable inside the same
protected transaction.

## Why a local workaround is prohibited

Assigning independent R5 serials after a FREE take, or sampling time after the
worker returns, would create a new ordering mechanism and/or redefine
`t_commit`.  Both conflict with Architecture v3.2.2 §6.2 and the R5 directive
sections 9, 19–22.  A synthetic harness cannot establish the missing real
wiring.

## Minimum approved integration contract needed

The existing R3/R4 owner must expose one bounded R5 observer path that supplies:

1. for each integrity-checked DMA input, its `sequence`, `t_irq` and the
   RuntimeEvent serial, before the ordinary FREE decision;
2. an ISR-safe pre-admission Q/FREE observation at that same decision point;
3. for each successful COMPLETE protected commit, its input sequence,
   `t_commit`, and the serial from the same approved ordering domain;
4. an S2 callback that closes R3 admission/stop gates before a FREE take and
   asks the existing task-context lifecycle owner to complete quiescence.

The observer must be compile-time gated, bounded, allocation-free, and not
change the underlying R2 ownership, R3 lifecycle, R4 Clock64 or RuntimeEvent
semantics.  Its implementation would change shared R3/R4 source and therefore
requires an explicit lower-layer regression plan before R5 hardware evidence.

## Consequence

R5 W6 cannot truthfully proceed from source-stack compile to actual production
integration without this approved interface.  Formal hardware remains blocked
also by R4 not being formally CLOSED.
