# P0-A1 — Computer-side Reproducible Environment

**Status:** PASS
**Date:** 2026-09-11
**Milestone commit:** `6337c44`

## 1. Goal

Establish a reproducible computer-side STM32 development environment and a repository-local smoke check without claiming any physical-board validation.

## 2. Test / verification performed

The repository-local environment smoke check verified:

- Git invocation
- `uv` and project Python 3.12.13
- locked/offline Python environment synchronization
- CMake and Ninja
- ARM GCC / G++ / GDB / readelf
- `arm-none-eabi` compiler target
- STM32CubeMX command-line startup
- STM32CubeProgrammer CLI startup
- real Cortex-M4F host-side cross-compilation
- expected ARM object attributes
- `.venv/` and `build/` ignore behavior
- absence of tracked generated junk
- absence of machine-specific Windows absolute paths in tracked text

## 3. Configuration / environment

Primary environment record:

- [`docs/environment.md`](../../environment.md)

Reusable verification entry point:

- [`tools/environment-smoke.ps1`](../../../tools/environment-smoke.ps1)

Relevant chronology:

- [`docs/bringup-log.md`](../../bringup-log.md)
- [`project-journal/2026-09-11.md`](../../../project-journal/2026-09-11.md)

## 4. Pass criteria

P0-A1 passes when the required host tools can be invoked through the intended command-line workflow, the project-local Python environment can be reproduced from its lock definition, the repository-local smoke check completes successfully, and generated/local-only files remain outside Git.

## 5. Actual result

PASS.

The final environment smoke check reported successful CubeMX CLI startup, STM32CubeProgrammer CLI startup, Cortex-M4F compilation, ARM attribute checks, and repository hygiene checks.

## 6. Evidence locations

- [`docs/environment.md`](../../environment.md)
- [`tools/environment-smoke.ps1`](../../../tools/environment-smoke.ps1)
- [`docs/bringup-log.md`](../../bringup-log.md)
- Git milestone commit `6337c44`

## 7. Milestone commit

```text
6337c44
```

## 8. Known limitations / not tested

No physical target validation was performed.

The following remained untested at P0-A1:

- ST-LINK detection
- flashing
- target execution
- on-board debugging
- VCP/UART
- physical clock behavior
- ADC / TIM / DMA / DBM
- FreeRTOS runtime behavior
- board-level timing
- R0-R7 hardware gates
