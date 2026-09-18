# R2 Final Control-Traffic / Service-Margin Acceptance Plan

## Baseline

- Architecture: v3.2.2
- Repository start: `45a5b0b3983d01b59893aea8cfb1b42c4afb2420`
- W6 firmware anchor: `bbc3bf6821c4f6e4dc73beabd5685b8b0828205c`
- R2-W1 through R2-W6: CLOSED / KNOWN-GOOD
- R2 overall: IN PROGRESS
- `r2-pass`: NOT CREATED

This package does not reopen W1-W6 and does not enter R3. It adds only the real USART2 control path needed to finish the Architecture v3.2.2 R2 control-traffic/service-margin acceptance.

## Frozen communication path

```text
PC
  -> ST-LINK VCP
  -> USART2 RXNE IRQ (NVIC priority 6)
  -> bounded static RX ring
  -> CommunicationTask (tskIDLE_PRIORITY + 4)
  -> bounded PING processing
  -> fixed TX staging buffer
  -> USART2 TX DMA1 Stream 6 / Channel 4 (NVIC priority 6)
  -> ST-LINK VCP
  -> PC
```

The ADC DMA2 Stream0 IRQ remains priority 5 and therefore has greater urgency than the control-traffic IRQs. All UART IRQ priorities remain inside the FreeRTOS kernel-aware ISR range (`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`).

## CT-W2 scope

CT-W2 is only a one-request hardware smoke. It intentionally leaves the sealed W6 acquisition length and ownership/drop mechanism unchanged.

Configuration:

- W6 K=8
- W6 mode=NORMAL
- 200 kS/s, N=256 inherited from W6
- one PING request
- 64-byte deterministic payload
- request must be processed while W6 phase is RUNNING
- reply uses USART2 TX DMA
- W6 hardware result must still pass

CT-W2 is not the final worst-permitted-control-traffic acceptance and must not create `r2-pass`.

## Later stages

After CT-W2 is known-good, later work packages may extend only one major dimension at a time:

1. worst-permitted uniform traffic;
2. adverse burst traffic;
3. K=1 DROP plus recovery under traffic;
4. remaining K/mode matrix;
5. committed-state regression, evidence seal and Principal review.

The final stress profile remains bounded by Architecture v3.2.2: no more than 10 small runtime control requests per second, each runtime request payload no more than 64 bytes, with an adverse burst case in addition to uniform traffic.

## W6 preservation rule

`STREAM_LAB_R2_CT` is a modifier of `STREAM_LAB_R2_W6` and is OFF by default. When it is OFF, the ordinary W6 programmed image must remain byte-identical to the sealed W6 hardware image. CT-W2 verification explicitly checks this before any hardware operation.

## Evidence policy

A CT-W2 PASS requires machine-readable evidence for both sides of the path:

- host request/reply validation;
- target `g_r2_ct_result` counters/trace;
- target W6 result and invariants;
- exact ELF/programmed-image identity;
- source fingerprint and Git state.

Host-tooling failures are not firmware failures.
