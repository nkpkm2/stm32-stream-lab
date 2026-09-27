# R4 paired instrumentation-perturbation protocol

Status: **frozen acceptance protocol; target instrumentation and an
immutable-pair verifier are implemented, sealed evidence is pending.** This
protocol closes neither R4 nor its perturbation claim until all six required
external attempts and the aggregate verifier record exist.

## Question and scope

The comparison asks whether enabling the R4 accounting hooks materially
changes the representative acquisition/worker system being measured.  It does
not claim that the reference firmware has zero measurement cost or that it
can report CPU residency without R4 accounting.

Each profile is built from the same committed source, NUCLEO-F446RE, 180 MHz,
Release/LTO configuration, ADC DBM setup (200 kSamples/s, 256-sample blocks),
R3 lifecycle/worker configuration, priorities, and 65,000 ms real-DMA soak.
The sole intentional axis is `STREAM_LAB_R4_ACCOUNTING`:

| Profile | Setting | Meaning |
|---|---:|---|
| `MINIMAL` | `0` | task/IRQ/tick/queue RuntimeEvent hooks are compile-time no-ops; a common raw-DWT DMA-envelope probe remains. |
| `R4` | `1` | production R4 accounting routes are enabled, with the same common raw-DWT probe. |

The common observer is deliberately outside `RuntimeEvent`: it measures the
same DMA vector envelope in both profiles and is the only shared observer
used for the direct service comparison.  Its own bounded footprint must be
documented with the implementation.

## Required metrics

Every individual attempt must expose, with explicit units and nonzero sample
counts where applicable:

1. raw-DWT DMA-vector service count and min/median/max;
2. processed/admitted/completed counts and processing wake/complete/cancel
   counts after a quiescent STOP snapshot;
3. KEEP/REBIND/admission-drop identities and fault counts under a declared
   controlled backpressure stimulus;
4. at least 33 fixed raw-DWT response samples (release, worker-start and
   completion) from the real DMA-publication to R3 Processing worker path;
5. for `R4` only, a sealed CPU-window partition.  `MINIMAL` must encode CPU
   accounting as **not available**, never as zero.

Both profiles must individually have a valid lifecycle, no data-plane or
ownership fault, at least 50,000 DMA observations, DMA maximum <=23,040
cycles, and the declared conservation identities.  An attempt that fails an
individual gate may be retained as history but cannot enter a pair.

## Frozen pair acceptance gates

The pair verifier accepts only a `MINIMAL` and an `R4` PASS attempt with the
same source commit, tool hash, board profile, selector, 65,000-ms soak and all
common CMake definitions.  It rejects missing, unmatched, or manually copied
records.

For a valid pair:

| Comparison | Gate |
|---|---|
| DMA maximum service | both <=23,040 cycles; R4-minus-MINIMAL relative increase <=25% |
| DMA service observation rate | normalized count/duration delta <=10% |
| completion throughput | delta <=5%; a one-completion boundary difference is tolerated only when it is <=1 and documented in raw counts |
| controlled admission/drop rates | absolute ratio delta <=5 percentage points; no new fault class |
| processing completion/admission | >=99% in both profiles |
| response | R4 median and maximum each no more than 25% worse than MINIMAL |
| R4 CPU result | sealed task+IRQ+Idle+residual equals its formal window; MINIMAL is explicitly unavailable |

Thresholds are deliberately bounded rather than zero.  Any divide-by-zero,
missing response population, or unavailable common metric is a verifier
failure, not a zero-overhead result.

## Anti-cherry-pick run order

One closure set contains three independent paired runs in alternating order:

```text
MINIMAL → R4 → R4 → MINIMAL → MINIMAL → R4
```

All six attempts are immutable external attempts with H0–H5, raw SRAM record,
binary/ELF identity, manifest and individual acceptance.  The aggregate
verifier records all six inputs and calculated deltas.  Every individual run
must pass; all three paired comparisons must meet the table above.  A pair
above twice any declared threshold is an investigation/FAIL, never an omitted
outlier.

## Non-claims

This protocol quantifies the perturbation of the representative R3 data plane.
It does not replace final R2 semantic regression, R3 lifecycle anchors, or
the separate R4 correctness matrix.  The `MINIMAL` profile cannot be used for
formal CPU-utilization reporting.
