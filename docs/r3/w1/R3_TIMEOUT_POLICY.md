# R3 Timeout Derivation Policy — Frozen

Target timing and host watchdog timing are separate.

For every hardware case the PLAN must state a bounded target duration:

```text
T_target_bound
```

before H2 is legal.

Minimum host watchdog:

```text
T_host >= 2 * T_target_bound
```

A larger transport/orchestration margin may be required; the harness records both the target bound and selected host watchdog.

If no defensible target bound exists, the case is not frozen and H2 is blocked.

## Initial architecture bounds

- STOP grace design target: 500 ms, subject to R3 validation against supported worst-case current work.
- Host watchdog for a test whose only bounded target phase is 500 ms STOP grace must therefore be at least 1.0 s, before adding transport/setup margin.
- Block-count runtime lower bound: `events * N / fs`.
- A host timeout less than nominal target duration is a precheck failure.

## Classification

Host watchdog expiry without target-side timing violation evidence:

`HOST_ORCHESTRATION_TIMEOUT`

It must not be reported as `TARGET_LIFECYCLE` or firmware timing FAIL.

## W6 soak

Use both:
- per-cycle bounded watchdog;
- total-run orchestration bound derived from cycle count.

Do not rely on one huge timeout as the only liveness test.
