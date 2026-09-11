# P0-A2 — Reproducible Firmware Build Baseline

**Status:** PASS
**Date:** 2026-09-11
**Milestone commit:** `337e6c8`

## 1. Goal

Prove that the tracked minimal STM32CubeMX project can perform a real clean configure, compile, link, and artifact-packaging workflow using the selected ARM GCC toolchain.

## 2. Test / verification performed

Starting from a clean build directory, the generated STM32F446RE project was configured and built with CMake + Ninja + Arm GNU Toolchain.

The linked ELF was inspected for target architecture and ABI, and was then converted to HEX and BIN images using `arm-none-eabi-objcopy`.

## 3. Configuration / environment

Tracked CubeMX configuration:

- [`firmware/cubemx/cubemx.ioc`](../../../firmware/cubemx/cubemx.ioc)

Build documentation:

- [`docs/firmware-build.md`](../../firmware-build.md)

Relevant generated build inputs:

- [`firmware/cubemx/CMakeLists.txt`](../../../firmware/cubemx/CMakeLists.txt)
- [`firmware/cubemx/cmake/gcc-arm-none-eabi.cmake`](../../../firmware/cubemx/cmake/gcc-arm-none-eabi.cmake)
- [`firmware/cubemx/STM32F446xx_FLASH.ld`](../../../firmware/cubemx/STM32F446xx_FLASH.ld)
- [`firmware/cubemx/startup_stm32f446xx.s`](../../../firmware/cubemx/startup_stm32f446xx.s)

Chronology:

- [`docs/bringup-log.md`](../../bringup-log.md)
- [`project-journal/2026-09-11.md`](../../../project-journal/2026-09-11.md)

## 4. Pass criteria

P0-A2 passes when a clean build:

- configures successfully from repository inputs;
- compiles and links the complete generated STM32 target;
- produces a valid target ELF and linker map;
- can produce HEX and BIN images from the verified ELF;
- uses Cortex-M4 / ARMv7E-M with FPv4-SP-D16 and hard-float ABI;
- leaves build output outside version control;
- keeps the CubeMX `.ioc` tracked;
- introduces no machine-specific absolute paths into the generated source tree.

## 5. Actual result

PASS.

Verified target artifacts:

```text
cubemx.elf
cubemx.map
cubemx.hex
cubemx.bin
```

Verified compile/ABI settings:

```text
-mcpu=cortex-m4
-mfpu=fpv4-sp-d16
-mfloat-abi=hard

Tag_CPU_arch: v7E-M
Tag_FP_arch: VFPv4-D16
Tag_ABI_VFP_args: VFP registers
```

Verified footprint baseline:

```text
text = 7220 B
data = 20 B
bss  = 1572 B
```

The build artifacts were intentionally kept under ignored `build/` output and were not committed.

## 6. Evidence locations

- [`docs/firmware-build.md`](../../firmware-build.md)
- [`firmware/cubemx/cubemx.ioc`](../../../firmware/cubemx/cubemx.ioc)
- generated CMake/startup/linker configuration under `firmware/cubemx/`
- [`docs/bringup-log.md`](../../bringup-log.md)
- Git milestone commit `337e6c8`

## 7. Milestone commit

```text
337e6c8
```

## 8. Known limitations / not tested

No physical target validation was performed.

The following remain untested:

- ST-LINK detection
- flashing
- target execution / boot
- on-board GDB debugging
- VCP/UART
- physical clock behavior
- ADC / TIM / DMA / DBM
- FreeRTOS runtime behavior
- board-level timing
- R0-R7 hardware gates

P0-A2 proves a reproducible host-side firmware build baseline only.
