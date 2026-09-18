# R2 Principal Acceptance

## Decision

**R2 = PASS / CLOSED.**

Principal Acceptance is granted for the Architecture v3.2.2 R2 risk gate
as implemented and evidenced in the repository through commit
`3db4339b64b275e5868cdb288d867a561caf3cba` plus the final committed-state closure audit.

Creation of `r2-pass` is authorized after this final acceptance package is
committed and pushed.

## Authorized firmware milestone

`48da792e382e911aad1b7bb43765284aa8b58ea2`

This commit is the final firmware-content anchor for R2.

The final closure audit established that the current firmware tree is
identical to this anchor and that fresh current-state rebuilds reproduce
all eight hardware-tested final-control programmed images byte-for-byte.

The milestone tag therefore follows the same semantic policy used for R1:
the pass tag identifies the accepted firmware state, while later
documentation/evidence commits preserve the corresponding proof package.

## Principal basis

The final closure audit reports:

- current repository and origin aligned and clean;
- Native W1-W6 + CT protocol: `124 / 124 PASS`;
- CT protocol tests: `8 / 8 PASS`;
- historical W6 CT-OFF programmed images: `8 / 8 PASS`;
- final-control CT-ON programmed images: `8 / 8 PASS`;
- CT-W2 through CT-W6 evidence manifest verification: **PASS**;
- current firmware tree equals `48da792e382e911aad1b7bb43765284aa8b58ea2`;
- current R2 blocker count: `0`;
- verdict before Principal review:
  `R2_PRINCIPAL_REVIEW_READY`.

The hardware campaign established the K+2 ownership model, DMA slot and
rebinding semantics, queue/processing/FREE round trip, controlled
capacity-drop invariants, K=1/2/4/8 NORMAL/DROP coverage, real USART2
control servicing, worst-permitted uniform traffic, adverse burst
traffic, DROP/recovery interaction under real control traffic, and the
required service-margin/integrity gates.

## Explicit non-claims

This Principal Acceptance does not broaden the R2 evidence beyond its
approved Architecture scope.

In particular:

1. W6 `final_window <= 3600 cycles` is not substituted for the distinct
   CompleteAndReleaseBlock full `[t_lock,t_unlock)` initial
   `1800-cycle` target. That metric remains assigned to its later
   Architecture gate.
2. The R2 final-control stress campaign is not a claim that N=512, every
   sample rate, or the final floating-point DSP firmware has already
   passed.
3. R3-R7 lifecycle, runtime-accounting, model, DSP, and final-integration
   acceptance remains future work.
4. Later final floating-point integration must rerun the Architecture-
   required R2-R4 regressions.

These are scope boundaries, not unresolved R2 blockers.

## Authorization

After the final acceptance evidence/documentation commit is pushed:

- create annotated tag `r2-pass`;
- tag target: `48da792e382e911aad1b7bb43765284aa8b58ea2`;
- preserve all historical R1/R2/W6/final-control anchors;
- do not move the tag to the documentation commit.

Once the tag is pushed and verified, R2 administrative closure is complete
and R3 may begin.
