# R0 — Platform Freeze

Status: PASS
Date: 2026-09-13
Technical acceptance: GRANTED
Final Principal closeout: GRANTED
Firmware milestone: 9ade715d6f3035cd60512bf2ec4dd1c226436af8

## Goal

Freeze and validate the common STM32F446 + FreeRTOS platform before beginning the acquisition path.

## Result

R0 technical implementation is accepted.

Final committed-state ELF SHA256: 3E078CC76AB82A424B5E0141A1C9686821B776D6D959E44EB9F349B4BB202DEC

The committed firmware milestone was rebuilt from clean source and passed:
- Flash / verify;
- SystemCoreClock = 180000000;
- AIRCR.PRIGROUP = 3;
- FreeRTOS scheduler startup;
- repeated execution of Task A and Task B;
- kernel SysTick @ 1 kHz;
- TIM7 HAL tick;
- advancing HAL uwTick;
- USART2 / ST-LINK VCP;
- two independent physical RESET events.

## Frozen platform

- SYSCLK/HCLK: 180 MHz
- DWT/CYCCNT: validated
- FreeRTOS-Kernel: V11.1.0
- FreeRTOS upstream commit: dbf70559b27d39c1fdb68dfb9a32140b6a6777a0
- HAL tick: TIM7 @ 1 kHz
- Kernel tick: SysTick @ 1 kHz
- SVC / PendSV: FreeRTOS
- NVIC_PRIORITYGROUP_4 / AIRCR.PRIGROUP=3
- max-syscall logical priority: 5
- lowest/kernel logical priority: 15
- TIM7 logical priority: 0

## Primary evidence

- r0-clock-runtime-summary-01.txt
- r0-dwt-basic-summary-03.txt
- r0-tim7-runtime-summary-01.txt
- freertos-v11.1.0-source-preflight.txt
- investigation-001-nvic-priority-group-hardfault.md
- r0-freertos-prigroup4-root-cause-closure-01.txt
- r0-priority-baseline-summary-02.txt
- r0-final-committed-flash-candidate-01.txt
- r0-final-committed-regression-summary-01.txt

## Significant investigation

The initial CubeMX NVIC_PRIORITYGROUP_0 configuration produced runtime PRIGROUP=7 and a genuine scheduler-start HardFault.

The source of truth was corrected to NVIC_PRIORITYGROUP_4, producing runtime PRIGROUP=3.

The repaired platform then passed clean rebuild and real-board scheduling regression.

See:
investigation-001-nvic-priority-group-hardfault.md

## Limitations

R0 does not implement:
- TIM2-triggered ADC1 acquisition;
- DMA DBM;
- BufferPool;
- CompletionAdapter;
- full TickServiceAdapter;
- Clock64;
- RuntimeEvent;
- final four application tasks.

R1 remains NOT STARTED.

r0-pass has been created at the tested firmware milestone.
Final Principal administrative acceptance has been granted.
