# R5 implementation and acceptance

Status: PASS for R5's specified scope: synthetic known outcomes through
WARMUP/MEASURE/TAIL, S0/S1/S2 classification, observation cutoff, conservation
and immutable sealing.  R6 prediction and R7 DSP remain out of scope.

## Result core

`r5_run_metrics` is a static-memory state machine.  Every input and completion
is supplied with a strictly increasing event serial, so equal cycle timestamps
still have a declared order.  Its transitions are:

```text
OPEN -- S2 input --> OUTCOME_CLOSED -- integrity + stop confirmation --> SEALED
```

S0 opens the CPU window before ordinary admission; S1 closes it before the TAIL
admission decision; S2 increments only raw hardware input diagnostics and closes
observation before any FREE decision.  S2 cannot rebind, publish READY, add an
admission observation, or manufacture a capacity drop.

At cutoff, admitted-but-incomplete cohort entries become
`expired_unresolved` only if the S2 time exceeds the latest deadline plus the
configured uncertainty.  Otherwise the result records
`INSUFFICIENT_OBSERVATION` and no formal failure rate is claimed.  A completion
after cutoff is diagnostic-only and cannot revise a closed classification.

`r5_result_store` accepts only a `SEALED` snapshot and gives transports a
reference-counted immutable read.  A new run cannot replace a result while an
old read reference exists.

## Tests and required R5 cases

| Requirement | Evidence | Result |
|---|---|---|
| Known WARMUP/cohort/TAIL sequence and conservation | native `known`, target harness | PASS |
| T18 S0/S1 empty FREE; S2 FREE/non-FREE | native `s2`, `s2_empty`; target two-arm harness | PASS |
| Cutoff vs completion serial order | native `cutoff` | PASS |
| Insufficient observation, histogram overflow and censored P99 | native `insufficient`, `overflow` | PASS |
| Sealed result acquire/release isolation | native `store`, target result-store triplet | PASS |

The R5 harness intentionally uses fixed synthetic cycle values.  It proves the
classification and immutable-result implementation on the target, not ADC/DMA
physical timing; the latter remains governed by the R1/R2/R4 evidence and will
be exercised again by later end-to-end profiles.
