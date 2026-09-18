# R2 Final Control - CT-W6 Matrix Evidence Seal

## Status

CT-W6 is **CLOSED / KNOWN-GOOD SIX-CELL REMAINING K/MODE MATRIX**.

The six K/mode cells that were not already closed by CT-W3/CT-W4/CT-W5
have now each completed the full H2 -> H3 -> H4 -> H5 workflow using the
same sealed adverse-burst real-control stimulus.

No further R2 hardware experiment is planned unless the final architecture
audit identifies a concrete unmet acceptance requirement.

This seal does **not** itself create `r2-pass`.

## Why CT-W6 contained six cells

Real-control evidence already existed for:

- `K=8 / NORMAL`: CT-W3 worst-permitted uniform traffic and CT-W4 adverse burst;
- `K=1 / DROP`: CT-W5 adverse burst with proven in-window DROP -> ADMIT recovery.

CT-W6 therefore filled only the six remaining cells:

- `K=1 / NORMAL`;
- `K=2 / NORMAL`;
- `K=2 / DROP`;
- `K=4 / NORMAL`;
- `K=4 / DROP`;
- `K=8 / DROP`.

This avoids needlessly rerunning already-closed cells while completing
K/mode control-traffic coverage.

## Frozen traffic profile

Each CT-W6 cell used:

- `800` W6 events;
- `1500 ms` W6 run timeout;
- ten real PING requests;
- `64 B` payload per request;
- `76 B` request frame per request;
- one `760 B` host serial write;
- `0 ms` intentional inter-request delay;
- no reply wait before burst submission completed.

The host generator was the already-sealed `r2_ct_burst.py`.

## Matrix result

| Cell | H2 | H3 | H4 | H5 | Admitted | Dropped | Recovery | Timing d/i/f | CT max |
| --- | --- | --- | --- | --- | ---: | ---: | --- | --- | ---: |
| k1-normal | PASS | PASS | PASS | PASS | 800 | 0 | - | 6841 / 11810 / 1095 | 411106 |
| k2-normal | PASS | PASS | PASS | PASS | 800 | 0 | - | 7034 / 12049 / 1076 | 411984 |
| k2-drop | PASS | PASS | PASS | PASS | 114 | 686 | DROP 53 -> ADMIT 58 | 6803 / 11608 / 1053 | 407064 |
| k4-normal | PASS | PASS | PASS | PASS | 800 | 0 | - | 6914 / 11991 / 1076 | 410991 |
| k4-drop | PASS | PASS | PASS | PASS | 91 | 709 | DROP 52 -> ADMIT 56 | 7255 / 12090 / 1053 | 407510 |
| k8-drop | PASS | PASS | PASS | PASS | 69 | 731 | DROP 52 -> ADMIT 54 | 8188 / 13077 / 1054 | 407394 |

All NORMAL cells completed with:

- `800 / 800` admissions;
- zero capacity drops;
- `800 / 800` processed and released;
- zero recovery count;
- zero drop streak;
- per-event one-MxAR DMA rotation and mapping-epoch progression valid.

All DROP cells completed with:

- controlled drops present;
- admitted + dropped = `800`;
- processed = released = admitted;
- recovery and drop-streak gates satisfied;
- per-DROP M0AR/M1AR invariance;
- per-DROP mapping-epoch invariance;
- per-ADMIT one-MxAR rotation;
- a proven `DROP -> later ADMIT` inside the guaranteed real-control interval.

All six cells also passed:

- CT 10/10 real request handling;
- no CT rate limiting;
- no RX overflow;
- no parser/UART/reply error;
- W6 hard timing gates;
- DMA/ADC/ownership/token/sample/canary integrity gates;
- final pool-state checks.

## Historical programmed-byte regression

Before hardware execution, every CT-W6 cell rebuilt its corresponding
CT-OFF historical W6 cell.

Result:

**6 / 6 programmed-byte identical PASS.**

This preserves the chain from the original W6 mandatory hardware matrix
to the later CT-W6 control-traffic candidates.

## Evidence layout

- `cells/<cell>/attempt-01-pass/`: immutable H2/H3/H4/H5 evidence;
- `host-procedures/`: exact adverse-burst host, matrix build gate, and matrix harness;
- `committed-state-regression/`: matrix build gate plus source/artifact provenance;
- `matrix-summary.json`: machine-readable six-cell closure summary;
- `MANIFEST.sha256`: hashes of every sealed file except the manifest itself.

Raw hardware/tool logs are preserved byte-for-byte.

## R2 position after CT-W6

With CT-W6 complete, the R2 hardware/control-traffic campaign consists of:

- R2-W1 through R2-W6: closed / known-good;
- CT-W2: real control-path smoke closed;
- CT-W3: worst-permitted uniform traffic closed;
- CT-W4: adverse-burst traffic closed;
- CT-W5: K1/DROP recovery under real control traffic closed;
- CT-W6: six remaining K/mode cells under adverse burst closed.

The next phase is therefore **not another hardware test**.

Remaining R2 work is:

1. final committed-state regression;
2. unified architecture requirement/evidence matrix;
3. evidence/provenance review;
4. Principal Acceptance Review;
5. only after Principal acceptance, create `r2-pass` with the agreed milestone semantics.

No claim is made here that final R2 Principal acceptance has already occurred.
