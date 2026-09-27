# R3-W3 Transactional START Acceptance

**Decision:** PASS for W3 only.  This does not approve W4--W6 or overall R3.

| Matrix cell | Required evidence | Result | Immutable record |
|---|---|---|---|
| W3-START-A | native + hardware | PASS | [`w3-start-a`](../../evidence/r3/w3/w3-start-a/attempt-0001/acceptance.json) |
| W3-START-B | native | PASS | [`native lifecycle record`](../../evidence/r3/w3/native/R3_W3_NATIVE_LIFECYCLE_20260927.md) (`invalid`) |
| W3-T06-A | native + hardware | PASS | [`w3-t06-a`](../../evidence/r3/w3/w3-t06-a/attempt-0001/acceptance.json) |
| W3-T06-B | native + hardware | PASS | [`w3-t06-b`](../../evidence/r3/w3/w3-t06-b/attempt-0001/acceptance.json) |
| W3-START-C | native + hardware | PASS | [`w3-start-c`](../../evidence/r3/w3/w3-start-c/attempt-0001/acceptance.json) |
| W3-START-D | native | PASS | [`native lifecycle record`](../../evidence/r3/w3/native/R3_W3_NATIVE_LIFECYCLE_20260927.md) (`stale`) |

The required hardware records prove the target-side pre-commit visibility,
ARM ADC/DBM arm path, final TIM2 commit, and three failure/rollback outcomes.
The W3 hardware execution profiles deliberately retain an explicit test fault
selector; the selector is compile-time target-harness configuration and is not
an ordinary runtime request.

The next permitted technical package is W4 safe STOP: partial DMA and pending
IRQ behavior, backlog/current-processing stop semantics, duplicate STOP, and
error-quiesce recovery remain unproven by this decision.
