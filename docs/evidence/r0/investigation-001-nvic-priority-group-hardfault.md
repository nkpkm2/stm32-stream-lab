# Investigation 001 — FreeRTOS Startup HardFault Caused by NVIC Priority Grouping

Status: CLOSED
Gate: R0 — Platform Freeze
Date: 2026-09-13

## Observation

The initial FreeRTOS V11.1.0 candidate built, linked, flashed, and verified, but scheduler startup entered HardFault before the first task executed.

Startup localization showed:

vTaskStartScheduler -> reached
xPortStartScheduler -> reached
prvPortStartFirstTask -> reached
SVC_Handler -> not reached

## Reproduction

A free-running endpoint test used only HardFault_Handler and R0_TaskA as endpoints. HardFault_Handler was reached first, confirming a genuine runtime platform failure.

## Diagnostic evidence

The active SVC vector matched the linked FreeRTOS SVC handler after removing the Thumb bit.

Fault-state evidence:

BASEPRI = 0x50
PRIMASK = 0
FAULTMASK = 0
CFSR = 0x00000000
HFSR = 0x40000000

Immediately before svc 0:

AIRCR.PRIGROUP = 7
SVC priority = 0
PendSV priority = 15
SysTick priority = 15

## Root cause

CubeMX source of truth selected NVIC_PRIORITYGROUP_0.

The generated HAL MSP initialization overrode the STM32 HAL default priority grouping.

On STM32F446 this produced:

AIRCR.PRIGROUP = 7
0 pre-emption-priority bits
4 subpriority bits

## Repair

CubeMX source of truth was changed to NVIC_PRIORITYGROUP_4.

The repaired runtime state is:

AIRCR.PRIGROUP = 3
4 pre-emption-priority bits
0 subpriority bits

No runtime workaround was added.

## Regression

After repair, the platform passed clean rebuild, flash/verify, first-task startup, sustained two-task scheduling, SysTick kernel timing, TIM7 HAL timing, VCP regression, and two physical reset trials.

Final tested firmware milestone: 9ade715d6f3035cd60512bf2ec4dd1c226436af8
Final committed-state ELF SHA256: 3E078CC76AB82A424B5E0141A1C9686821B776D6D959E44EB9F349B4BB202DEC

## Test-harness issues

Several intermediate failures were caused by diagnostic scripts or debugger behavior and were kept distinct from the genuine platform defect.

## Final status

CLOSED.

The NVIC_PRIORITYGROUP_0 to PRIGROUP=7 configuration was a genuine R0 platform defect.
The NVIC_PRIORITYGROUP_4 to PRIGROUP=3 correction is the accepted source-of-truth repair.
