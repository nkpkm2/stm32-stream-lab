# R3-W5 Isolation and Restart Acceptance

**Decision:** PASS for W5 only. This does not approve W6 or overall R3.

| Cell | Evidence | Result |
|---|---|---|
| T13-A | [native](../../evidence/r3/w5/native/R3_W5_NATIVE_ISOLATION_20260927.md) + [hardware](../../evidence/r3/w5/w5-t13-a/attempt-0001/acceptance.json) | PASS |
| T13-B/E/F | [native isolation record](../../evidence/r3/w5/native/R3_W5_NATIVE_ISOLATION_20260927.md) | PASS |
| T13-C | [native](../../evidence/r3/w5/native/R3_W5_NATIVE_ISOLATION_20260927.md) + [hardware](../../evidence/r3/w5/w5-t13-c/attempt-0001/acceptance.json) | PASS |
| T13-D | [native](../../evidence/r3/w5/native/R3_W5_NATIVE_ISOLATION_20260927.md) + [hardware](../../evidence/r3/w5/w5-t13-d/attempt-0001/acceptance.json) | PASS |
| T13-G | [native](../../evidence/r3/w5/native/R3_W5_NATIVE_ISOLATION_20260927.md) + [hardware](../../evidence/r3/w5/w5-t13-g/attempt-0001/acceptance.json) | PASS |
| T20-A | [native](../../evidence/r3/w5/native/R3_W5_NATIVE_ISOLATION_20260927.md) + [hardware](../../evidence/r3/w5/w5-t20-a/attempt-0001/acceptance.json) | PASS |
| T20-B | [native](../../evidence/r3/w5/native/R3_W5_NATIVE_ISOLATION_20260927.md) + [hardware](../../evidence/r3/w5/w5-t20-b/attempt-0001/acceptance.json) | PASS |

The result records prove complete request binding, old-boot rejection,
one-transaction STOP replay, ACK-to-new-run isolation, immutable referenced
bytes with bounded `RESULT_BUSY`, and genuine restart after release. The next
permitted package is W6: exactly 500 anchor-A plus 500 anchor-B board cycles.
