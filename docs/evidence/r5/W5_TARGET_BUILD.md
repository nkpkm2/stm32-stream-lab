# R5 target compile evidence (host-only)

Date: 2026-09-29  
Profile: `Release`, `STREAM_LAB_R5_METRICS=ON`, `STREAM_LAB_R5_HW=ON`  
Compiler: Arm GNU Toolchain 14.2.1 (`arm-none-eabi`)  
Build system: repository-pinned CMake 3.31.12 and Ninja 1.13.1.

Result: **PASS — cross-compile and link completed.**

```text
RAM:   15968 B / 128 KiB (12.18%)
FLASH: 13936 B / 512 KiB (2.66%)
```

The only source adjustment needed for this profile is in the generated IRQ
translation unit: it now includes the FreeRTOS and baseline DMA acquisition
declarations that its already-present `TIM7_IRQHandler` and
`DMA2_Stream0_IRQHandler` require under `STREAM_LAB_R5_HW`.  This makes the
existing synthetic profile buildable; it does **not** add R5 production
DMA/queue/worker integration and is not target execution evidence.

No flash, reset, probe connection, or hardware execution was performed.
