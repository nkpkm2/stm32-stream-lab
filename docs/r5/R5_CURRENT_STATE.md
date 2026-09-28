# R5 current state — W0 inventory

Status date: 2026-09-28  
Authority: Architecture v3.2.2 §6.1–6.2, §7.1, §7.4, §7.5 and R5 autonomous-execution directive.  
Scope: this is an inventory, not an R5 acceptance claim.  R4 remains pending
Principal closure, so R5 formal hardware acceptance is blocked by the
cross-release dependency.

## Existing implementation

The repository contains a useful static-memory foundation:

- `firmware/runtime/r5_run_metrics.[ch]` implements the basic
  `OPEN -> OUTCOME_CLOSED -> SEALED` lifecycle, S0/S1/S2 processing, nominal
  time and deadline arithmetic, a 128-bin completed-latency histogram, and
  conditional P99 statuses.
- `firmware/runtime/r5_result_store.[ch]` copies a sealed snapshot and holds
  result references across transport use.
- `tests/native/test_r5_run_metrics.c` has seven directed native cases.
- `firmware/runtime/r5_hw_harness.[ch]` is a target-side **synthetic** proof;
  it is not connected to the real DMA, FreeBufferQueue, CompletionAdapter or
  RuntimeEvent paths.

The pre-existing `docs/r5/R5_IMPLEMENTATION_AND_ACCEPTANCE.md` and
`docs/status.md` describe an earlier, narrower synthetic scope.  They are
historical context only; they do not satisfy or supersede the current R5
directive.

## Requirement-to-implementation map

| Requirement | Existing implementation | Tests/evidence | Gap status | W package |
| --- | --- | --- | --- | --- |
| Unique input sequence including drops; primary `[S0,S1)` | Strict sequential input and cohort counters | `known`, `s2` | PARTIAL: no explicit per-sequence outcome record | W2/W3 |
| Fixed tail (default 8, minimum 2), no early end | Minimum two enforced; S2 calculated as `S1 + tail` | Basic cases use tail 2 | PARTIAL: default/contract and no-early-stop proof absent | W1/W2 |
| S0 opens before admission; S1 closes before admission | Correct ordering in `OnInput` | `known`, `s2` | PARTIAL: no real IRQ wiring | W2/W6 |
| S2 cutoff only; no normal admission/drop/occupancy | Correct model branch prior to admission | `s2`, `s2_empty`; synthetic target | PARTIAL: occupancy absent; no real queue proof | W2/W6 |
| Cutoff sufficiency and ordered cutoff-vs-completion | Strict serial while open; insufficient status | `cutoff`, `insufficient` | PARTIAL: post-cutoff events do not participate in the same serial audit; completion-wins and equal-timestamp cases absent | W3 |
| Exactly one primary outcome | Counter conservation at seal | `known` | PARTIAL: no explicit outcome state prevents a direct proof/audit | W3 |
| Nominal/deadline/t_commit classification | Implemented, equality is on-time | `known`, `overflow` | PARTIAL: boundary vectors incomplete | W1/W3/W5 |
| 128 bins, overflow and conditional P99 | Implemented | `overflow` | PARTIAL: exact 8D, rank, no-completion and golden vectors incomplete | W1/W4/W5 |
| Arrival-observed occupancy | None | None | MISSING | W1/W4/W6 |
| Configuration/runtime observation sufficiency | Basic config and cutoff-time check | `insufficient` | PARTIAL: no stable schema/diagnostic separation | W1/W4 |
| Immutable sealed result, live diagnostics separated | Snapshot copied into result store | `store` | PARTIAL: no explicit live diagnostic object or immutable-field mutation test | W4 |
| Real DMA / queue / completion / RuntimeEvent integration | None; synthetic harness only | Synthetic target harness | MISSING | W6 |
| Known-truth C/Python vectors and stable host tooling | None | Native C only | MISSING | W5 |
| R2/R3/R4 representative regression linkage | None | None | MISSING | W7 |
| Formal R5 hardware acceptance | Not authorized while R4 is not closed | Historical synthetic evidence only | BLOCKED | W6/W7 |

## W0 decision

Reuse `r5_run_metrics` and `r5_result_store` as the implementation substrate.
The next work begins by freezing the semantic schema and vectors, then extends
the core with explicit outcomes, occupancy, lifecycle integrity and tests.
No formal R5 hardware acceptance, board operation, flash, or retry is allowed
until R4 is formally closed and a separate action packet authorizes W6.
