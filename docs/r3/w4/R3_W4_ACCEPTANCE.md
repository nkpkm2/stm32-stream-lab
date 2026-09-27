# R3-W4 Safe STOP Acceptance

**Decision:** PASS for W4 only. This does not approve W5--W6 or overall R3.

| Matrix cell | Required evidence | Result | Immutable record |
|---|---|---|---|
| W4-T04-A | hardware | PASS | [`w4-t04-a`](../../evidence/r3/w4/w4-t04-a/attempt-0001/acceptance.json) |
| W4-T04-B | hardware | PASS | [`w4-t04-b`](../../evidence/r3/w4/w4-t04-b/attempt-0001/acceptance.json) |
| W4-STOP-A | native + hardware | PASS | [`native STOP record`](../../evidence/r3/w4/native/R3_W4_NATIVE_STOP_20260927.md); [`w4-stop-a`](../../evidence/r3/w4/w4-stop-a/attempt-0001/acceptance.json) |
| W4-STOP-B | native + hardware | PASS | [`native STOP record`](../../evidence/r3/w4/native/R3_W4_NATIVE_STOP_20260927.md); [`w4-stop-b`](../../evidence/r3/w4/w4-stop-b/attempt-0001/acceptance.json) |
| W4-STOP-C | hardware | PASS | [`w4-stop-c`](../../evidence/r3/w4/w4-stop-c/attempt-0001/acceptance.json) |
| W4-STOP-D | native + hardware | PASS | [`native STOP record`](../../evidence/r3/w4/native/R3_W4_NATIVE_STOP_20260927.md); [`w4-stop-d`](../../evidence/r3/w4/w4-stop-d/attempt-0001/acceptance.json) |
| W4-STOP-E | hardware | PASS | [`w4-stop-e`](../../evidence/r3/w4/w4-stop-e/attempt-0001/acceptance.json) |

The board records contain H0--H5 provenance, target build and flash/readback
identity, raw result words, verifier output, and manifest hashes.  Together
they prove: partial DMA does not publish READY data; a pending TC cannot leak
an old completion; backlog is canceled while only the current Processing
block completes; K+2 recovery has no ownership/mapping corruption; duplicate
STOP has one shutdown; and an ADC/DMA quiesce error reaches explicit
RESET_REQUIRED/invalid state rather than cosmetic repair.

The next permitted technical package is W5: command-ledger idempotence,
old-generation isolation, immutable result references, and immediate restart.
