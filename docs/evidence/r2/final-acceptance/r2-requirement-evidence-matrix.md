# R2 Final Requirement -> Evidence Closure Matrix

Repository checkpoint: `3db4339b64b275e5868cdb288d867a561caf3cba`

Firmware content anchor / proposed r2-pass firmware candidate: `48da792e382e911aad1b7bb43765284aa8b58ea2`

Audit verdict: **R2_PRINCIPAL_REVIEW_READY**

| ID | Requirement | Status | Evidence / disposition |
| --- | --- | --- | --- |
| R2-OWNERSHIP | K+2 pool, ownership accounting, DMA-slot model, dynamic inactive-slot rebinding | CLOSED_R2 | R2-W1/W2/W3/W6 sealed evidence |
| R2-ROUNDTRIP | DMA -> READY -> PROCESSING -> FREE -> DMA round trip and functional completion hook | CLOSED_R2 | R2-W4/W6 sealed evidence |
| R2-DROP | controlled capacity drop preserves DMA mapping/ownership and later admission recovery | CLOSED_R2 | R2-W5/W6 + CT-W5/CT-W6 DROP cells |
| R2-K-MATRIX | K=1/2/4/8 x NORMAL/DROP hardware coverage | CLOSED_R2 | R2-W6 8/8 historical matrix + CT-W3/W4/W5/W6 real-control coverage |
| R2-REAL-CONTROL | real USART2/VCP control request processed during active acquisition with bounded reply path | CLOSED_R2 | CT-W2 smoke; CT-W3 through CT-W6 stress |
| R2-UNIFORM | worst-permitted uniform control traffic within <=10/s, payload <=64 B | CLOSED_R2 | CT-W3 |
| R2-BURST | adverse bounded burst in addition to uniform traffic | CLOSED_R2 | CT-W4; reused in CT-W5/CT-W6 |
| R2-SERVICE-MARGIN | nominal->decision <=57600 cycles and nominal->ISR-exit <=80640 cycles under approved control load | CLOSED_R2 | CT-W3/W4/W5/W6 target traces and H5 validators |
| R2-W6-FINAL-WINDOW | W6-specific final protected window <=3600 cycles under control stress | CLOSED_R2 | CT-W3/W4/W5/W6 target traces |
| R2-INTEGRITY | zero DMA/ADC/ownership/slot/token/queue/notification/sample/canary integrity failures under control stress | CLOSED_R2 | CT-W2 through CT-W6 H5 acceptance |
| R2-T11 | T11 K full coverage, successful admission and continuous drop under worst allowed control load | CLOSED_R2 | W6 + CT-W3/W4/W5/W6 |
| R2-PROVENANCE | native/target regression, exact artifact identity and historical programmed-byte reproducibility | CLOSED_R2_IF_THIS_AUDIT_PASSES | current final committed-state regression |
| ARCH-COMMIT-CRITICAL-1800 | CompleteAndReleaseBlock full [t_lock,t_unlock) <=1800 cycles | DEFERRED_R4_NOT_CLAIMED | Architecture explicitly allows R2/R4 diagnostic proof; current W6 final_window is a different interval and is not used as substitute |
| ARCH-N512-OTHER-RATES | N=512 / broader mandatory parameter functionality and final floating-point integration | NOT_CLAIMED_BY_CURRENT_R2_FINAL_CONTROL | current stress point is N=256, fs=200 kS/s; later risk gates/final integration must retain their own coverage |
| R3-R7-FUTURE | START/STOP lifecycle, RuntimeEvent/Clock64, model/DSP and final Benchmark regressions | OUTSIDE_R2_CURRENT_GATE | Architecture R3-R7 risk-gate mapping |

## Final committed-state regression

- Native W1-W6 + CT protocol: **124 / 124 PASS**
- CT protocol tests: **8 / 8 PASS**
- Historical W6 CT-OFF programmed-byte regression: **8 / 8 PASS**
- Final-control CT-ON programmed-byte regression: **8 / 8 PASS**
- CT-W2..CT-W6 evidence manifests: **PASS**
- Current firmware tree vs final firmware content anchor: **identical**
- Hardware operations during this audit: **NONE**

## Explicit non-claims

- W6 `final_window <= 3600` is not treated as proof of the distinct CompleteAndReleaseBlock `[t_lock,t_unlock) <= 1800` target.
- N=512, broader sample-rate functional coverage, and final floating-point DSP integration are not claimed by this R2 final-control package.
- R3-R7 lifecycle/runtime/model/DSP requirements remain in their Architecture-defined future gates.

## Principal submission identities

- firmware candidate: `48da792e382e911aad1b7bb43765284aa8b58ea2`
- evidence/current repository commit: `3db4339b64b275e5868cdb288d867a561caf3cba`
- `r2-pass`: **NOT CREATED**
