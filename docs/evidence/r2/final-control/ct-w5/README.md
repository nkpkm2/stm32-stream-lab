# R2 Final Control - CT-W5 Evidence Seal

## Status

CT-W5 is **CLOSED / KNOWN-GOOD K1/DROP RECOVERY UNDER REAL CONTROL TRAFFIC**.

This milestone closes the most important remaining interaction case in
R2 final-control testing: the smallest backlog capacity, deliberate
capacity exhaustion, controlled drop, recovery to later admission, and
real adverse-burst UART control traffic all occurring in the same
hardware run.

This milestone does **not** by itself close R2.

## Baseline

- source checkpoint: `b3aa0b926a49182e99095241a7939195073aa545`
- target profile: `K=1 / DROP / 800 events / 1500 ms`
- deliberate processing hold: `6` block periods
- CT-W5 ELF SHA256: `7736852DFAE9254055ADA8D1EF8DDE36AF09B478BB658AF9526D7143C51367C3`
- CT-W5 programmed SHA256: `754AB433AE4E36EDA317F0F75AC148CC4C6D01AA867E5EB960D69EB5693F39E2`
- sealed W6 K1/DROP programmed SHA256:
  `0242FCFAAD2994B4547C5F9485CB9C5E308A9A36CB75158E3775E3F5736C2B45`
- sealed K1/DROP CT-OFF programmed-byte regression: **PASS**
- RAM used: `93368 B / 131072 B` (`71.23%`)
- RAM free: `37704 B`

No firmware source changed between the accepted CT long-run target
baseline and the CT-W5 source checkpoint.

## Real control stimulus

The already-sealed adverse-burst stimulus was reused without creating a
new traffic generator:

- ten real `PING` requests;
- `64 B` payload per request;
- `76 B` request frame per request;
- `760 B` total request burst;
- all requests submitted by one serial `write()`;
- `0 ms` intentional inter-request delay;
- host did not wait for replies before completing burst submission;
- sequential request IDs `0x0500` through `0x0509`;
- all ten requests processed while W6 was `RUNNING`;
- target W6 input observations `51 -> 98`;
- `rate_limited_count = 0`;
- `rx_overflow_count = 0`;
- reply success `10 / 10`.

The host-side millisecond timestamps are retained as orchestration
evidence only. They are not used as the target service-latency gate.

## Complete K1/DROP result

The 800-event hardware run completed successfully:

- input events: `800`;
- admitted: `115`;
- controlled capacity drops: `685`;
- processed: `115`;
- released: `115`;
- recovered admissions after drop: `114`;
- maximum consecutive drop streak: `6`;
- W6 `test_pass = 1`;
- W6 `fault_bits = 0`.

Hard real-time maxima:

- nominal-to-decision: `6755 / 57600` cycles;
- nominal-to-IRQ-exit: `11707 / 80640` cycles;
- final window: `1073 / 3600` cycles.

Integrity result:

- DMA error flags: `0`;
- ADC overrun: `0`;
- ownership/token violations: `0`;
- sample errors: `0`;
- canary errors: `0`;
- final pool: `1 FREE + 2 DMA`, no READY/PROCESSING residue.

## Per-event DROP / ADMIT invariants

The complete `800 / 800` W6 trace was collected from the same run and
validated offline.

For every controlled DROP:

- no M0AR write;
- no M1AR write;
- no mapping-epoch advance;
- no processing completion for the dropped event;
- free depth after failed take remained zero.

For every ADMIT:

- exactly one inactive DMA address register changed;
- mapping epoch advanced by exactly one;
- the admitted event was later processed successfully.

Trace-derived admitted/drop/recovery accounting exactly matched the
final W6 summary.

## CT x DROP/recovery temporal intersection

The first control request was processed when W6 `input_count = 51`; the
last was processed when `input_count = 98`.

Therefore W6 completion events `52..98` are guaranteed to have occurred
between those two control-processing observations.

Inside that guaranteed interval:

- admitted events: `6`;
- controlled drops: `41`;
- at least one recovery is directly demonstrated;
- first proven pair: `DROP sequence 52 -> ADMIT sequence 57`.

This establishes more than separate evidence that control traffic works
and that DROP/recovery works. It establishes that capacity exhaustion,
controlled dropping, recovery, and real control servicing coexisted in
the same bounded interval while all architecture invariants remained
valid.

## Control-service evidence

Maximum target-side CT service intervals:

- RX complete -> process: retained in H5 JSON;
- process -> TX start: retained in H5 JSON;
- TX DMA interval: retained in H5 JSON;
- RX complete -> final reply completion: `406337` cycles.

Complete reply transmission is DMA-backed and is supplementary service
evidence. The W6 hard real-time gates above remain the architecture
timing acceptance criteria.

## Evidence layout

- `attempt-01-pass/`: immutable H2/H3/H4/H5 evidence from the passing run;
- `attempt-01-pass/host-procedures/`: exact host tools and validators used;
- `committed-state-regression/`: build-gate and source/artifact provenance;
- `MANIFEST.sha256`: hashes of every sealed file except the manifest itself.

Raw hardware/tool logs are preserved byte-for-byte.

## Scope boundary

CT-W5 closes **K=1 / DROP + recovery under real control traffic**.

The next action is not automatically another hardware experiment.
Architecture v3.2.2 must first be audited against the complete R2
evidence set to determine whether any required K/mode control case
remains uncovered.

Still required before `r2-pass`:

- Architecture-level final-control coverage closure audit;
- any specifically identified missing acceptance case, if one exists;
- final committed-state regression and unified R2 evidence review;
- Principal Acceptance Review.

No claim is made here that R2 final acceptance is complete.
