# Project Status

Last updated: 2026-09-27
Architecture baseline: v3.2.2

| Gate / Package | Status | Milestone Commit | Notes |
|---|---|---|---|
| P0-A1 | PASS | `6337c44` | Computer-side reproducible environment |
| P0-A2 | PASS | `337e6c8` | Reproducible firmware build baseline |
| P0-B | PASS | `7b62082` | Real-board flash, execution, debug, and VCP loop |
| R0 | PASS | `9ade715` | Final Principal acceptance granted; `r0-pass` created |
| R1 | PASS | `5ebf62e` | Final Principal acceptance granted; `r1-pass` created |
| R2 | IN PROGRESS | `bbc3bf6` | W1-W6 implemented; W6 mandatory K matrix 8/8 hardware PASS; final control-traffic/service-margin acceptance pending |
| R3 | PASS | `a932e2f` | W2--W6 sealed. Real board evidence includes all directed W3--W5 cells plus exactly 500 K8/NORMAL and 500 K1/DROP lifecycle records. See [`final acceptance`](r3/R3_FINAL_ACCEPTANCE.md). |
| R4 | PRINCIPAL REVIEW READY | `828eff5` | T04 has a confirmed valid 1919-cycle inner-interval failure against the 1800-cycle initial target. The corrected same-event diagnostic and a proposed conservative engineering-deviation package are sealed; Principal acceptance is required before R4 closure. See [`R4 evidence`](evidence/r4/README.md). |
| R5 | PASS | `d869ac1` | Synthetic cohort/cutoff/sealed-result core implemented; T18 two-arm target evidence and directed native tests pass. See [`R5 evidence`](evidence/r5/README.md). |
| R6 | PASS | `aa4a73c` | Cost-keyed delayed-admission prediction, immutable validation binding and prediction-before-run evidence complete; directed native and real-board harness evidence pass. See [`R6 evidence`](evidence/r6/README.md). |
| R7 | NOT STARTED | — | — |

Primary R0 evidence: [`docs/evidence/r0/README.md`](evidence/r0/README.md)

Primary R1 evidence: [`docs/evidence/r1/README.md`](evidence/r1/README.md)

`r0-pass` points to firmware milestone `9ade715`.

`r1-pass` points to hardware-tested firmware milestone `5ebf62e`.

R2 is IN PROGRESS. W1-W6 are implemented; R2-W6 mandatory K matrix is 8/8 hardware PASS at firmware milestone `bbc3bf6821c4f6e4dc73beabd5685b8b0828205c`. Final control-traffic/service-margin acceptance remains pending; `r2-pass` is not created.
