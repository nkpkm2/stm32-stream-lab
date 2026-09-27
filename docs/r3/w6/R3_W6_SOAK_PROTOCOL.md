# R3-W6 1000-cycle Hardware Soak Protocol

## Scope

W6 is an R3 lifecycle test, not a reuse of the historical R2 queue-matrix
profile. The target harness calls the production Communication-owned
`R3W3Runtime_Start` and `R3W3Runtime_Stop` path on every cycle.

Two separate, non-resumable board attempts are required:

| Anchor | Cycles | Configuration | Stop placement |
|---|---:|---|---|
| A | exactly 500 | K=8, NORMAL, N=256 | deterministic rotating phase after start |
| B | exactly 500 | K=1, DROP, N=256 | deterministic rotating phase with Processing notification suppressed long enough to exercise the K=1 pressure path |

A failure terminates its whole attempt. A later attempt starts again at cycle
1; it must never append to or relabel a prior attempt.

## Per-cycle record

The DMA-visible target result contains exactly 500 fixed-size records per
anchor. A record is valid only when all of the following are present and
internally consistent:

1. one-based `cycle_index`, `boot_id`, `run_id`, `generation`, and `stop_id`;
2. START and STOP statuses, lifecycle terminal state, and the expected
   transaction counters;
3. DMA hardware ownership/EN state, worker ACK mask and fault state;
4. ownership reconciliation state plus processing cancellation/completion
   counters; and
5. result-store identity/reference state and a zero runtime fault indication.

The host verifier rejects missing, duplicate, non-monotonic, stale-identity,
or post-failure records. Its summary is computed from the raw records, not
from a target-side aggregate alone.

## Acceptance

For each anchor the source commit, target ELF hash, raw result readback,
per-cycle verification output, and H0--H5 manifest must be imported and
pushed. Overall W6 passes only when A has exactly 500 valid records and B has
exactly 500 valid records, with no record fault and no attempt abort.
