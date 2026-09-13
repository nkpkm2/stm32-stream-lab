# R1 Raw Acquisition Integrity Evidence

## Status

- Classification: PASS CANDIDATE
- Principal acceptance: PENDING
- `r1-pass` tag: NOT CREATED
- R2 functionality: NOT INCLUDED

## Tested configuration

- Platform: STM32 NUCLEO-F446RE
- Core clock: 180 MHz
- Acquisition trigger: TIM2 TRGO Update
- TIM2 PSC: 0
- TIM2 ARR: 449
- Effective sample rate: 200 kS/s
- ADC: ADC1 channel 0 on PA0
- ADC sampling time: 28 cycles
- DMA: DMA2 Stream0 Channel 0
- DMA mode: genuine double-buffer mode
- Fixed buffers: M0 and M1
- Samples per block: 256
- Nominal block period: 1.28 ms
- Nominal block interval: 230400 CPU cycles

## Hardware validation summary

### Short-run bringup

- Hardware classification: PASS
- Transfer completions: 195
- M0 completions: 98
- M1 completions: 97
- CT mismatch: 0
- Alternation mismatch: 0
- Suspected event loss: 0
- ADC overrun: 0
- DMA TE/DME/FE: 0
- Mean block interval: 230400 cycles

### Lifecycle regression

- Hardware classification: PASS
- Start/stop/restart cycles: 6 / 6
- NDTR restarted at 256 on every cycle
- CT restarted at M0 on every cycle
- First completion after restart: fresh M0 then M1
- Targeted partial-stop campaign: PASS
- Post-stop stale completion: NONE
- ADC overrun: 0
- DMA errors: 0

### Formal 10-minute soak

- Hardware classification: PASS
- Duration: 600000 ms
- Expected transfer completions: 468750
- Actual transfer completions: 468749
- Transfer-completion count error: -1
- Observed block rate: 781.248 blocks/s
- Expected block rate: 781.250 blocks/s
- M0 completions: 234375
- M1 completions: 234374
- Mean block interval: 230400 cycles
- Minimum block interval: 230072 cycles
- Maximum block interval: 230727 cycles
- CT mismatch: 0
- Alternation mismatch: 0
- Suspected event loss: 0
- ADC overrun: 0
- DMA TE/DME/FE: 0
- Post-stop stale completion: NONE

### Raw ADC snapshot

- Complete samples preserved: 256
- Input condition: PA0 / ADC1_IN0 connected to GND
- Selected completed DMA target: M0
- Observed raw range: 0 .. 8
- CSV SHA256: B01105298CDA8782E74D5E6702B5716293C23978D9AE6A9AF8567AF6B9DE1400

### Committed-state reproducibility

- Committed HEAD: `5ebf62e90b31e262f44013afb594a430061f139a`
- Hardware-tested ELF SHA256: `67040A73D2072C569918FA3E3AB9B3F88B52C0901E211665ED99462BF33E6CBA`
- Clean committed-state rebuild produced the identical ELF SHA256
- Byte-identical reproduction: PASS

### Final focused hardware regression

- Exact committed-state ELF flash/verify: PASS
- R1 live acquisition sanity: PASS
- SystemCoreClock = 180 MHz: PASS
- AIRCR.PRIGROUP = 3: PASS
- TIM7 HAL tick: PASS
- SysTick FreeRTOS tick: PASS
- FreeRTOS scheduler: PASS
- USART2 VCP startup banner: PASS
- IRQ priority contract: PASS
- ADC OVR / DMA FE/DME/TE: 0

## Tested-build hashes

- Short-run ELF: `C0493EC1B31D504F404BB2ABD732B1B82038102EA975099651E4D225932F4DA7`
- Lifecycle ELF: `CDB12A342F507C7660E7C25DB8F4F640A3F0A2652A0AD3A3683A22EB680037FC`
- Formal-soak / committed-state ELF: `67040A73D2072C569918FA3E3AB9B3F88B52C0901E211665ED99462BF33E6CBA`

## Evidence files

- `r1-short-run-result-01.txt`
- `r1-lifecycle-result-01.txt`
- `r1-soak-result-01.txt`
- `r1-final-focused-regression-01.txt`
- `r1-raw-snapshot.csv`
- `r1-raw-snapshot-metadata.txt`
- `r1-tested-builds.txt`
- `r1-evidence-manifest.sha256`

## R0 regression

- `r0-pass` remains at `9ade715d6f3035cd60512bf2ec4dd1c226436af8`.
- R0 clock, NVIC grouping, HAL tick, FreeRTOS tick, scheduler, VCP, and interrupt-priority behavior were revalidated after R1 integration.

## Acceptance boundary

This evidence package establishes R1 fixed-buffer raw acquisition integrity only.
Dynamic buffer retargeting, K+2 ownership, queues, DSP processing, and other R2 functionality are intentionally outside this milestone.

The R1 implementation is ready for Principal acceptance review. The `r1-pass` tag must not be created until that acceptance is granted.
