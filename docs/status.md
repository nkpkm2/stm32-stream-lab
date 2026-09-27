# Project Status

Last updated: 2026-09-18
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
| R4 | PASS | pending commit | Clock64/RuntimeEvent/TickService/CompletionAdapter implemented; T12/T15/T17 and 10 µs gate have native + board evidence. See [`R4 evidence`](evidence/r4/README.md). |
| R5 | NOT STARTED | — | — |
| R6 | NOT STARTED | — | — |
| R7 | NOT STARTED | — | — |

Primary R0 evidence: [`docs/evidence/r0/README.md`](evidence/r0/README.md)

Primary R1 evidence: [`docs/evidence/r1/README.md`](evidence/r1/README.md)

`r0-pass` points to firmware milestone `9ade715`.

`r1-pass` points to hardware-tested firmware milestone `5ebf62e`.

R2 is IN PROGRESS. W1-W6 are implemented; R2-W6 mandatory K matrix is 8/8 hardware PASS at firmware milestone `bbc3bf6821c4f6e4dc73beabd5685b8b0828205c`. Final control-traffic/service-margin acceptance remains pending; `r2-pass` is not created.
