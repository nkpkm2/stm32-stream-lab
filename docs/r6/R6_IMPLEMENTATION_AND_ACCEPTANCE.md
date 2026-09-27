# R6 implementation and acceptance

Status: PASS for R6's foundation scope: complete cost-key representation,
delayed-IRQ admission prediction, FREE/CPU separation, immutable start binding,
and prediction-before-run host evidence.  This is not a claim of an ADC/DMA
end-to-end latency calibration; physical costs remain qualified only after the
R4 timing profile and future calibrated end-to-end profiles.

## Model boundary

`r6_prediction` preserves the relevant chain rather than using a nominal
trigger as an admission proxy:

```text
nominal trigger -> DMA completion -> executable IRQ/admission
-> READY/processing start -> t_lock -> logical commit -> t_unlock/FREE
-> processing epilogue availability
```

The cost key has non-null fields for pipeline, coefficient and window versions,
features, datatype, compile flags, library version, DAC profile and
instrumentation profile.  The table separately names DMA/admission offsets,
DSP work, commit prefix/suffix, epilogue, FIR reset gap, admit/drop ISR and
monitor/platform costs.  It deliberately never derives CPU work from quiet
wall-clock time.

Tie ordering is frozen as **FREE visible before admission at an equal cycle**.
The directed T10 vector makes FREE visible at cycle 105 after nominal 100 but
before admission 110; it is admitted.  A second vector returns FREE at unlock
110, admits the next block at 120, but delays its processor start to 160 until
the predecessor epilogue finishes.  Thus `t_unlock` and processor availability
cannot be conflated.

R4's `TickService` remains the release/skip authority: its directed suspension
case proves releases at 2 with skips at 4/6/8 and the next release at 10, with
no catch-up.  R6 consumes that frozen service rule; it does not create a second
periodic scheduler.

## Validation binding

`r6_start_binding` accepts only the architecture's allowed matrix:

| kind / purpose | prediction binding |
|---|---|
| PERFORMANCE / CALIBRATION or DIAGNOSTIC | NOT_APPLICABLE and null artifact |
| PERFORMANCE / VALIDATION | BOUND plus nonzero artifact/config/model/calibration/profile/window/schema identities |
| FAULT_STALL / FAULT_TEST | NOT_APPLICABLE and null artifact |

The first valid binding is copied into a slot and cannot be replaced.  The
MCU only validates the declared identity; it does not falsely claim a host file
was persisted.  `tools/r6/make_prediction_artifact.py` creates that self-hashed
host artifact before the target run.

## Acceptance evidence

| Requirement | Evidence | Result |
|---|---|---|
| T10 delayed admission after nominal trigger | native `r6_t10`, target SRAM | PASS |
| FREE at unlock differs from CPU availability | native `r6_unlock`, target SRAM | PASS |
| purpose/binding matrix and immutable start | native `r6_binding`, target SRAM | PASS |
| prediction persisted before run | self-hashed host JSON, created before flash | PASS |
| T09 periodic skip semantics | existing R4 native directed case | PASS |

The target harness is deliberately a static, synthetic-cycle validation of R6
logic and binding.  It is not a substitute for calibrated physical ADC/DMA or
DSP timing, and its evidence must not be promoted into such a claim.
