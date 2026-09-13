# P0-B — Board Development Loop

**Status:** PASS
**Date:** 2026-09-13
**Firmware milestone commit:** `7b62082`

## Goal

Prove a reliable minimum development loop between the host computer and the physical NUCLEO-F446RE:

`identify → connect → flash → execute → debug → communicate`.

P0-B does not validate the final 180 MHz / FreeRTOS platform. That belongs to R0.

## Configuration

- Board: NUCLEO-F446RE
- Target: STM32F446xx
- Device ID: `0x421`
- Internal Flash: 512 KBytes
- CPU: Cortex-M4
- ST-LINK: onboard ST-LINK/V2-1
- Final ST-LINK firmware: `V2J48M35`
- MCU transport: USART2, PA2 TX / PA3 RX
- USART2: 115200 baud, 8N1, TX/RX, no hardware flow control
- USART2 IRQ: not enabled
- USART2 DMA: not enabled

## Procedure

1. Confirmed board and jumper physical baseline.
2. Enumerated the onboard ST-LINK.
3. Diagnosed repeated `DEV_USB_COMM_ERR` failures.
4. Upgraded ST-LINK/V2-1 firmware from `V2J28M18` to `V2J48M35` with the official native ST-LinkUpgrade utility.
5. Reconnected successfully over SWD and identified the STM32F446xx target.
6. Built, flashed, and verified the known baseline firmware.
7. Proved execution with reset → hardware breakpoint at `main()` → continue → breakpoint hit.
8. Proved debugger reset/breakpoint/continue operation with ST-LINK GDB Server 7.14.0.
9. Enabled the approved USART2/ST-LINK VCP path in CubeMX.
10. Added deterministic startup output `P0-B VCP READY\r\n`.
11. Built, flashed, and verified the VCP candidate.
12. Captured the expected MCU-to-PC output across two independent physical RESET events.

## Pass Criteria

- board identity confirmed;
- ST-LINK detected;
- STM32F446RE-family target detected;
- firmware clean build succeeds;
- target Flash programming succeeds;
- Flash verification succeeds;
- firmware execution is proven;
- debugger reset/halt/breakpoint/continue works;
- ST-LINK VCP appears on the host;
- deterministic MCU-to-PC VCP communication works;
- reset/repeat behavior is reproducible.

## Result

**PASS.**

The complete host-to-board development loop was demonstrated on real hardware.

Final VCP firmware artifact:

```text
build/p0-b-vcp-01/cubemx.elf
SHA256: 62AFAC04597BD590092042257D18C796E4DC9A3C1DDB0C3F88E7597FF6523BF2
```

The deterministic startup message `P0-B VCP READY` was captured after two independent physical reset events.

## Evidence

- `target-identification-post-upgrade.txt` — real target identity after ST-LINK recovery
- `stlink-connection-investigation.txt` — initial failure and recovery history
- `first-flash-verify.txt` — first project firmware programming and verification
- `execution-debug-summary.txt` — execution/debugger acceptance summary
- `gdb-execution-proof.txt` — breakpoint-at-main transcript
- `gdbserver-session-01.log` — ST-LINK GDB server session
- `vcp-cubemx-audit.txt` — USART2 configuration audit
- `vcp-candidate-source.diff` — reviewed USART2/VCP firmware diff
- `vcp-flash-candidate-manifest.txt` — candidate source/artifact identity
- `vcp-flash-verify.txt` — VCP firmware programming and verification
- `vcp-runtime-verification.txt` — two-reset VCP runtime proof

Chronological engineering context remains in `docs/bringup-log.md` and `project-journal/`.

## Failures / Deviations

### ST-LINK USB communication failure

Initial ST-LINK/V2-1 firmware `V2J28M18` repeatedly produced `DEV_USB_COMM_ERR`.

The failure survived a controlled USB reconnect, another physical USB port, and a second known-good data cable. The Java updater also failed to enter update mode with JNI/system error 121.

The official native Windows ST-LinkUpgrade utility successfully updated the probe to `V2J48M35`. SWD target connection then succeeded.

### Vendor-file whitespace exception

`git diff --check` reported a blank line at EOF in three CubeMX-introduced STM32CubeF4 UART vendor files. Those files matched STM32CubeF4 V1.28.3 byte-for-byte by SHA-256 and were therefore not reformatted.

## Limitations

P0-B does not prove:

- final 180 MHz platform clock configuration;
- runtime clock correctness;
- DWT timing;
- FreeRTOS scheduling;
- HAL/kernel timebase ownership;
- final IRQ priority policy;
- ADC / TIM-triggered acquisition;
- DMA / DBM;
- R1-R7 behavior.

## Conclusion

**PASS.**

A repeatable physical development loop now exists from host build through ST-LINK/SWD, Flash, target execution, debugger operation, USART2/ST-LINK VCP, and PC reception.

R0 remains NOT STARTED.
