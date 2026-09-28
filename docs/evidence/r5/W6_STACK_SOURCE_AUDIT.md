# R5 W6 shared-stack source audit (host-only)

Date: 2026-09-29  
Purpose: prove source-level compatibility of the current R3/R4/R5 stack; this
is not a hardware run or a substitute for real R5 integration.

## Combined profile

The repository-pinned Arm GNU 14.2.1/CMake 3.31.12/Ninja 1.13.1 build enabled:

```text
STREAM_LAB_FOUNDATION_ADC_DBM_DRIVER=ON
STREAM_LAB_FOUNDATION_OWNERSHIP_CORE=ON
STREAM_LAB_FOUNDATION_TOKEN_LEDGER=ON
STREAM_LAB_FOUNDATION_QUEUE_ADAPTER=ON
STREAM_LAB_R3_WORKER_CONTRACT=ON
STREAM_LAB_R3_WORKER_TASKS=ON
STREAM_LAB_R3_LIFECYCLE=ON
STREAM_LAB_R4_RUNTIME=ON
STREAM_LAB_R5_METRICS=ON
```

Result: **PASS — 56 objects compiled and linked**.

```text
RAM:   14424 B / 128 KiB (11.00%)
FLASH: 18424 B / 512 KiB (3.51%)
```

## Integration finding

The profile is deliberately only a source-stack audit.  It does not select an
R5 start harness and `main.c` still follows the baseline R1 bringup path.
More importantly, the current production callback surface does not yet expose
the three R5 wiring points as one approved transaction:

1. RuntimeEvent receipt (`t_irq`, event serial) for every DMA input;
2. pre-admission FREE/Q observation after S0/S1 but before a token take;
3. `t_commit` plus the same ordering serial at the real COMPLETE hook.

Therefore the audit is evidence of **link compatibility only**.  It proves
neither H01–H08 nor T02/T07/T14/T18.  The next source change must introduce a
small, explicit R5 bridge across these points; it may not imitate this with a
synthetic harness or bypass existing R3/R4 serialization.

No flash, reset, debugger attachment, target execution, or retry occurred.
