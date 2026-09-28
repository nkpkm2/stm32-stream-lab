# R5 closure matrix (current, non-final)

Status date: 2026-09-29.  Values are restricted to `PASS`, `PARTIAL`,
`MISSING`, or `BLOCKED`; this document does not declare Principal acceptance.

| ID | Requirement | Status | Evidence / reason |
| --- | --- | --- | --- |
| A01 | schema/versioned config | PASS | `R5_SCHEMA_VERSION`, W1 contract |
| A02 | unique input sequence | PASS | strict `next_input_sequence`, native cases |
| A03 | `[S0,S1)` half-open primary population | PASS | native S0/S1 cases |
| A04 | fixed tail/default/minimum | PASS | contract, SYN13/SYN14 |
| A05 | S0 opens before admission | PASS | ordered `OnInput` implementation |
| A06 | S1 closes before tail admission | PASS | ordered `OnInput` implementation |
| A07 | S2 no ordinary admission/drop | PASS | SYN02/SYN03 |
| A08 | S2 no occupancy sample | PASS | SYN05 |
| A09 | nominal/deadline/t_commit equality | PASS | SYN10 |
| A10 | serial-defined cutoff race | PASS | SYN04/SYN05 |
| A11 | insufficient observation distinct | PASS | SYN06 |
| A12 | exact unique primary outcome | PASS | explicit outcome array, SYN08 |
| A13 | outcome conservation at seal | PASS | core invariant, SYN01/SYN08 |
| A14 | cutoff unresolved is immutable | PASS | SYN04 |
| A15 | 128 latency bins/overflow | PASS | SYN07/SYN08 |
| A16 | completed P99 and censored all-admitted P99 | PASS | SYN11/SYN12 |
| A17 | occupancy histogram | PASS | core + SYN05; production Q source still pending A24 |
| A18 | rates and N/A status | PASS | `R5MetricsRate`, native assertions |
| A19 | OPEN → OUTCOME_CLOSED → SEALED | PASS | core/native suite |
| A20 | sealed result transport copy | PASS | result-store native case |
| A21 | live diagnostics separated from outcomes | PARTIAL | post-cutoff live counters exist; transport schema not integrated |
| A22 | C/host known-truth agreement | PASS | W5 native 13/13 + host manifest SYN01–SYN14 |
| A23 | target profile cross-build | PASS | `W5_TARGET_BUILD.md`, `W6_STACK_SOURCE_AUDIT.md` |
| A24 | real DMA input / pre-admission Q wiring | BLOCKED | approved R3/R4 receipt/observer surface absent; see integration review |
| A25 | real worker COMPLETE/t_commit wiring | BLOCKED | actual protected commit does not expose a per-event receipt |
| A26 | actual S2 stop-gate and quiescence integration | BLOCKED | requires the same approved observer/gate contract |
| A27 | run/boot identity in R5 result schema | MISSING | requires R3 lifecycle bridge |
| A28 | real result transport/readback | MISSING | store is local-only |
| A29 | hardware H01–H08 directed cases | BLOCKED | R4 not formally CLOSED; W6 action packet |
| A30 | R2/R3/R4 representative regression | PARTIAL | shared R3/R4/R5 source-stack build passes; no integrated R5 profile or hardware anchor yet |
| A31 | immutable hardware attempt/evidence manifest | BLOCKED | no authorized hardware run |
| A32 | Principal review readiness | BLOCKED | A24–A31 remain open |

## Verdict

`R5 = BLOCKED / HOST CORE AND TARGET COMPILE READY; PRODUCTION INTEGRATION AND
FORMAL HARDWARE ACCEPTANCE NOT READY.`

The next permitted step after formal R4 closure is the W6 source-integration
review described in `R5_W6_ACTION_PACKET.md`; it must not be replaced with the
existing synthetic target harness.
