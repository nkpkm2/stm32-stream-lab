# Platform Baseline

Last updated: 2026-09-13
Architecture baseline: v3.2.2
R0 status: PASS CANDIDATE
Technical acceptance: GRANTED
Final Principal closeout: PENDING

## Hardware

Board: NUCLEO-F446RE
MCU: STM32F446RE / detected STM32F446xx
Device ID: 0x421
Revision: Rev A
CPU: Cortex-M4F
Flash: 512 KB
SRAM: 128 KB
ST-LINK/V2-1 firmware: V2J48M35

## Clock

HSE bypass: 8 MHz
PLLM: 4
PLLN: 180
PLLP: /2
SYSCLK/HCLK: 180 MHz
APB1: 45 MHz
APB1 timer clock: 90 MHz
APB2: 90 MHz
APB2 timer clock: 180 MHz
Voltage scale: Scale 1
OverDrive: enabled
Flash latency: 5 WS

## RTOS and timebase ownership

FreeRTOS-Kernel: V11.1.0
FreeRTOS upstream commit: dbf70559b27d39c1fdb68dfb9a32140b6a6777a0
Port: portable/GCC/ARM_CM4F
Kernel tick: SysTick @ 1 kHz
HAL tick: TIM7 @ 1 kHz
SVC: FreeRTOS
PendSV: FreeRTOS

## Interrupt priority baseline

__NVIC_PRIO_BITS = 4
STM32 HAL / CubeMX priority group: NVIC_PRIORITYGROUP_4
AIRCR.PRIGROUP = 3
4 pre-emption-priority bits
0 subpriority bits

configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5
configLIBRARY_LOWEST_INTERRUPT_PRIORITY = 15

SVC logical priority = 0
PendSV logical priority = 15
SysTick logical priority = 15
TIM7 logical priority = 0

ISR contract:
logical priority 0..4 -> must not call FreeRTOS / FromISR APIs
logical priority 5..15 -> may call approved FreeRTOS FromISR APIs

TIM7 remains a priority-0 non-RTOS HAL timebase ISR.

## USART2 / ST-LINK VCP

PA2 = USART2_TX
PA3 = USART2_RX
115200 8N1
No hardware flow control
USART2 IRQ disabled in R0
USART2 DMA disabled in R0
Startup banner: P0-B VCP READY

## R0 firmware milestone

Firmware commit: 9ade715d6f3035cd60512bf2ec4dd1c226436af8
Committed-state ELF SHA256: 3E078CC76AB82A424B5E0141A1C9686821B776D6D959E44EB9F349B4BB202DEC

The committed firmware milestone was rebuilt in a fresh directory, flashed, verified, and passed the final committed-state R0 regression.

## Forward invariants

Later phases must preserve:
- the 180 MHz clock contract;
- TIM7 ownership of the HAL tick;
- SysTick ownership by the FreeRTOS kernel;
- SVC and PendSV ownership by FreeRTOS;
- NVIC_PRIORITYGROUP_4 / AIRCR.PRIGROUP=3;
- the FreeRTOS max-syscall priority boundary;
- the rule that logical priorities 0..4 do not call FreeRTOS APIs.

R1 acquisition functionality is not part of this baseline.
