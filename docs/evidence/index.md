# Project Evidence Index

This index points to the evidence used to justify milestone and gate results. It is intentionally concise: detailed chronology remains in the existing engineering log, while raw experiment data will live outside the evidence README files.

Current project status: [`docs/status.md`](../status.md)

## P0-A1 — Computer-side Reproducible Environment

**Status:** PASS
**Date:** 2026-09-11
**Milestone commit:** `6337c44`

**Goal:** Establish a reproducible host-side STM32 development environment.

**Primary evidence:**
- [`docs/environment.md`](../environment.md)
- [`tools/environment-smoke.ps1`](../../tools/environment-smoke.ps1)
- [`docs/bringup-log.md`](../bringup-log.md)
- [`project-journal/2026-09-11.md`](../../project-journal/2026-09-11.md)
- Git history at commit `6337c44`

**Verified:**
- Git / Python / uv / CMake / Ninja
- ARM GCC / G++ / GDB
- STM32CubeMX command-line startup
- STM32CubeProgrammer CLI
- Cortex-M4F host-side cross-compilation
- repository hygiene and path portability

**Result:** PASS

Package summary: [`docs/evidence/p0-a1/README.md`](p0-a1/README.md)

## P0-A2 — Reproducible Firmware Build Baseline

**Status:** PASS
**Date:** 2026-09-11
**Milestone commit:** `337e6c8`

**Goal:** Prove that the tracked minimal STM32CubeMX project can be cleanly configured, compiled, linked, and packaged using the selected ARM GCC workflow.

**Primary evidence:**
- [`docs/firmware-build.md`](../firmware-build.md)
- tracked [`firmware/cubemx/cubemx.ioc`](../../firmware/cubemx/cubemx.ioc)
- generated build configuration under `firmware/cubemx/`
- [`docs/bringup-log.md`](../bringup-log.md)
- [`project-journal/2026-09-11.md`](../../project-journal/2026-09-11.md)
- Git history at commit `337e6c8`

**Verified build result:**
- clean configure / compile / link
- `cubemx.elf`
- `cubemx.map`
- `cubemx.hex`
- `cubemx.bin`
- `-mcpu=cortex-m4`
- `-mfpu=fpv4-sp-d16`
- `-mfloat-abi=hard`
- footprint baseline: `text = 7220 B`, `data = 20 B`, `bss = 1572 B`

Build artifacts are intentionally reproducible outputs and are not committed at this stage.

**Result:** PASS

Package summary: [`docs/evidence/p0-a2/README.md`](p0-a2/README.md)

## P0-B — Board Development Loop

**Status:** PASS
**Date:** 2026-09-13
**Firmware milestone commit:** `7b62082`

**Goal:** Prove the real NUCLEO-F446RE development loop through ST-LINK/SWD, Flash programming, target execution, debugger operation, and USART2/ST-LINK VCP.

**Primary evidence:**
- [`docs/evidence/p0-b/README.md`](p0-b/README.md)
- [`target-identification-post-upgrade.txt`](p0-b/target-identification-post-upgrade.txt)
- [`first-flash-verify.txt`](p0-b/first-flash-verify.txt)
- [`gdb-execution-proof.txt`](p0-b/gdb-execution-proof.txt)
- [`vcp-runtime-verification.txt`](p0-b/vcp-runtime-verification.txt)
- [`docs/bringup-log.md`](../bringup-log.md)
- Git history at commit `7b62082`

**Key result:**
- real STM32F446xx target detected;
- ST-LINK recovered from repeated `DEV_USB_COMM_ERR` after firmware update `V2J28M18 → V2J48M35`;
- Flash programming and verification passed;
- reset → breakpoint at `main()` → continue → breakpoint hit passed;
- deterministic `P0-B VCP READY` output captured across two physical resets.

**Result:** PASS

P0-B does not validate the 180 MHz / DWT / FreeRTOS platform; those remain R0 work.

## R0 — Platform Freeze

**Status:** PASS
**Date:** 2026-09-13
**Firmware milestone:** `9ade715`

**Final Principal acceptance:** granted; `r0-pass` created at the tested firmware milestone.

**Primary evidence:**
- [`docs/evidence/r0/README.md`](r0/README.md)
- [`docs/platform.md`](../platform.md)
- [`NVIC priority-group investigation`](r0/investigation-001-nvic-priority-group-hardfault.md)
- [`Final committed-state regression`](r0/r0-final-committed-regression-summary-01.txt)

**Verified:**
- 180 MHz runtime clock platform
- DWT/CYCCNT
- FreeRTOS-Kernel V11.1.0 / GCC ARM_CM4F
- SysTick kernel tick / TIM7 HAL tick ownership
- SVC / PendSV / SysTick exception ownership
- NVIC_PRIORITYGROUP_4 / AIRCR.PRIGROUP=3
- IRQ / FromISR priority boundary
- static two-task scheduling
- VCP and two-reset committed-state regression

**Result:** PASS

R1 remains NOT STARTED.

## Existing engineering / project logs

The existing logs remain the chronological record and are not duplicated by this evidence system:

- [`docs/bringup-log.md`](../bringup-log.md) — detailed engineering / bring-up chronology, failures, diagnostics, and fixes
- [`project-journal/`](../../project-journal/) — daily project journal

Evidence packages summarize formal verification claims and point back to these logs when chronology matters.

## Evidence package convention for future gates

When a future gate is actually executed, create `docs/evidence/rX/README.md` using this minimum structure:

```text
# RX — <Gate Name>

Status:
Date:
Firmware commit:
Host commit/model version if applicable:

## Goal
## Configuration
## Procedure
## Pass Criteria
## Result
## Evidence
## Failures / Deviations
## Limitations
## Conclusion
```

Do not create empty future gate directories in advance.

## Investigation records

Create an investigation record only for engineering-significant failures, such as ownership/race problems, clock or IRQ conflicts, DMA/OVR errors, START/STOP stale state, measurement-accounting inconsistencies, watchdog/recovery failures, or model-prediction failures.

Use a file such as:

```text
docs/evidence/r2/investigation-001.md
```

with:

```text
Observation
Reproduction condition
Initial hypothesis
Diagnostic evidence
Root cause
Fix
Regression test
Final status
```

Open issues may remain `Status: OPEN`. Do not delete meaningful failure history merely to make the project appear failure-free.

## Provenance rule

For any future result used as formal evidence, retain enough information to recover at least:

```text
date
run_id (when runs exist)
firmware commit
test configuration
hardware configuration
result
PASS / FAIL / INVALID
```

Formal experimental campaigns will additionally bind model version, prediction ID, and calibration/validation membership where applicable.

P0-A1 and P0-A2 predate run-based hardware experiments, so no retrospective run IDs are invented.

## Raw data and screenshots

Evidence README files should point to machine-readable raw data rather than embedding large logs.

Future raw experiment data should use a structure such as:

```text
experiments/
  r2-stress-001/
    manifest.json
    summary.csv
    raw/
```

Save screenshots only when visual information itself is evidence. Prefer text, source, manifests, and machine-readable outputs when they express the same fact more precisely.
