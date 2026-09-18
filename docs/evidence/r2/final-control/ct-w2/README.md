# R2 Final Control — CT-W2 Evidence Seal

## Status

CT-W2 is **CLOSED / KNOWN-GOOD SMOKE**.

This milestone proves one real 64-byte `PING` request during an existing
R2-W6 `K=8 / NORMAL` acquisition run, with the request processed while W6
was in `RUNNING`, and with the complete UART TX-DMA final-completion path
working correctly.

This milestone does **not** claim the final worst-permitted control-traffic
acceptance and does **not** close R2.

## Baseline

- Repository HEAD/origin at test time: `45a5b0b3983d01b59893aea8cfb1b42c4afb2420`
- CT source SHA256: `630FC34A41DA02CFDB0EC00A12EA576CE8915EF7C87A6F477CB63733311FEAD1`
- CT-ON ELF SHA256: `7C2CCA4982D39930BC4FFE31328F985880D4828F0F209C8B150F3A44D7042CFB`
- CT-ON programmed-image SHA256: `C16309F614A5E2B918FFD2B053E45DDAD70BE36B0E3A42E9316DD8C9996696B3`
- CT-OFF sealed W6 programmed-image regression: byte-identical PASS
- Native W1-W6 + CT protocol: 124 / 124 PASS
- CT protocol tests: 8 / 8 PASS

## Attempt 01 — FAIL

The first real hardware PING was physically received by the host, but the
firmware did not service the USART2 final transmit-complete (`TC`) path
through `HAL_UART_IRQHandler()`.

Observed evidence included:

- real 64-byte PING processed during W6 RUNNING;
- host received a valid reply;
- `reply_attempt_count = 1`;
- `reply_success_count = 0`;
- `reply_complete_cycle = 0`;
- W6 acquisition IRQ/input accounting still reached 96;
- task-side processing stalled, FREE capacity exhausted, and NORMAL traffic
  degraded into controlled capacity drops;
- no DMA, ADC, sample, canary, token-ledger, BufferPool, or DMA-slot violation.

Root cause: incomplete HAL UART normal-DMA TX completion integration.

The failed attempt is preserved under `attempt-01-fail/`.

## Fix

The CT USART IRQ path was changed so that when `USART_SR_TC` is asserted and
`UART_IT_TC` is enabled, the TX-completion event is handed to
`HAL_UART_IRQHandler(&huart2)` while preserving the custom bounded RXNE
ownership.

Static inspection of the project HAL driver confirmed the real call chain:

`HAL_UART_Transmit_DMA`
→ `UART_DMATransmitCplt`
→ enable `TCIE`
→ USART2 TC IRQ
→ `HAL_UART_IRQHandler`
→ `UART_EndTransmit_IT`
→ disable `TCIE`
→ `gState = READY`
→ `HAL_UART_TxCpltCallback`.

## Attempt 02 — PASS

H2:
- fixed CT-ON image flashed and verified successfully.

H3:
- one 64-byte PING;
- request id `0x0201`;
- target phase `RUNNING`;
- `K=8`;
- `NORMAL`;
- processed at W6 input `43`;
- host round trip `15.0 ms`;
- host return code `0`;
- host stderr empty;
- no debugger attached during runtime.

H4:
- post-run attach/read only;
- no flash;
- no reset;
- no second PING;
- no continue/restart.

H5:
- `reply_attempt / success / error = 1 / 1 / 0`;
- reply completion timestamp nonzero;
- W6 `96 / 96` admitted;
- W6 `0` capacity drops;
- W6 `96 / 96` processed/released;
- W6 `test_pass = 1`;
- W6 `fault_bits = 0`;
- max nominal→decision `6945 / 57600` cycles;
- max nominal→IRQ-exit `12150 / 80640` cycles;
- max final window `1076 / 3600` cycles;
- DMA/ADC/ownership/sample/canary errors `0`;
- final pool `8 FREE + 2 DMA`, `0 READY`, `0 PROCESSING`.

The passing attempt is preserved under `attempt-02-pass/`.

## Scope boundary

CT-W2 is only the one-request smoke milestone.

Still outstanding before R2 final acceptance:

- worst-permitted uniform control traffic;
- adverse burst traffic;
- DROP/recovery interaction under real control traffic;
- required K/mode expansion and final evidence seal;
- Principal acceptance and `r2-pass`.

No claim is made here that R2 final control-traffic acceptance is complete.
