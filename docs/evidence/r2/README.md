# R2 - Dynamic K+2 DMA Ownership and Capacity Control

## Status

- Architecture baseline: **v3.2.2**
- R2 overall: **IN PROGRESS**
- Latest firmware milestone: `bbc3bf6821c4f6e4dc73beabd5685b8b0828205c` (R2-W6 mandatory K matrix)
- `r2-pass`: **NOT CREATED**

## Work-package chronology

| Package | Firmware milestone | Purpose | Status |
|---|---|---|---|
| W1 | `7e1ab05ac281312a32092380709c4733fe684492` | BufferPool ownership core and K+2 active topology | CLOSED / KNOWN-GOOD |
| W2 | `a966cd3e78b4f190ca731daea8388463d39f7486` | DMA M0/M1 slot abstraction, CT decoding, planned rebind and mapping epoch | CLOSED / KNOWN-GOOD |
| W3 | `f5d0f086dae94d2f244f8366a65aa32c511fd5be` | Bounded real dynamic inactive-slot DMA rebinding | CLOSED / KNOWN-GOOD |
| W4 | `7ae9a562ff0eaeeeda4a80c1bea16a6d22d7203c` | DMA -> READY -> PROCESSING -> FREE -> DMA ownership round trip | CLOSED / KNOWN-GOOD |
| W5 | `2752c0e915ab4725cc13e400d16fe47b14adfd4e` | Directed FreeBufferQueue exhaustion and controlled capacity drop | CLOSED / KNOWN-GOOD |
| W6 | `bbc3bf6821c4f6e4dc73beabd5685b8b0828205c` | Mandatory K=1/2/4/8 NORMAL/DROP hardware matrix | CLOSED / KNOWN-GOOD |

## What R2 has established

Starting from the R1 fixed double-buffer acquisition baseline, R2 established a static K+2 physical buffer pool with explicit ownership, a CT-aware M0/M1 DMA slot model, real dynamic rebinding of the inactive DMA target, a complete queue/Processing/free-buffer ownership round trip, and a controlled capacity-drop path when no FREE token is available.

W6 then demonstrated the same mechanism across K=1,2,4,8 in both NORMAL and DROP mode on real STM32F446RE hardware. All eight mandatory hardware cells passed their accounting, ownership, DMA/ADC, sample/canary, timing, final-state, and trace invariants.

## Evidence

- W3: `docs/evidence/r2/w3/`
- W4: `docs/evidence/r2/w4/`
- W5: `docs/evidence/r2/w5/`
- W6: `docs/evidence/r2/w6/`
- Provenance/archive: `docs/evidence/r2/archive/`

W1 and W2 do not have standalone hardware evidence packages because they are lower-layer software abstractions; their behavior is exercised by later Native and hardware packages. Their implementation commits remain part of the Git history.

## Final R2 status

R2 is **PASS / CLOSED** following completion of the
R2 Final Control-Traffic / Service-Margin Acceptance work package and
Principal Acceptance.

Final R2 identities:

- firmware milestone / authorized `r2-pass` target:
  `48da792e382e911aad1b7bb43765284aa8b58ea2`;
- pre-acceptance evidence commit:
  `3db4339b64b275e5868cdb288d867a561caf3cba`;
- final closure audit: **PASS**;
- current-R2 blockers: **0**.

Final committed-state regression established:

- Native W1-W6 + CT protocol: `124 / 124 PASS`;
- CT protocol tests: `8 / 8 PASS`;
- historical W6 CT-OFF programmed-byte regression: `8 / 8 PASS`;
- final-control CT-ON programmed-byte regression: `8 / 8 PASS`;
- CT-W2 through CT-W6 evidence manifests: **PASS**;
- current firmware tree equals the firmware milestone content.

The following remain explicit non-claims and are not R2 blockers:

- the distinct CompleteAndReleaseBlock full `[t_lock,t_unlock)` initial
  `1800-cycle` target remains assigned to its later Architecture gate;
- N=512, broader sample-rate functional coverage, and final floating-point
  DSP integration are not certified by the current R2 final-control package;
- later R3-R7 lifecycle/runtime/model/DSP gates retain their own acceptance
  requirements.

Principal Acceptance authorizes creation of `r2-pass` at
`48da792e382e911aad1b7bb43765284aa8b58ea2` as a separate post-acceptance tag operation.
