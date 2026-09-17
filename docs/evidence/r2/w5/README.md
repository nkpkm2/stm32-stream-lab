# R2-W5 Controlled Capacity-Drop Evidence

## Status

- Work package: R2-W5
- Classification: PASS
- R2 overall: IN PROGRESS
- `r2-pass` tag: NOT CREATED

## Scope

This work package deliberately exhausts the FreeBufferQueue and validates the controlled drop path.
A DROP does not rebind M0AR/M1AR, does not advance the DMA-slot mapping epoch, does not publish a READY descriptor, and does not enter Processing.

## Tested configuration

- Platform: STM32 NUCLEO-F446RE
- Sample rate: 200 kS/s
- Block size: 256 samples
- K: 1
- Active physical buffers P: 3
- FreeBufferQueue capacity: 1
- ReadyQueue capacity: 1
- Input TC events: 64
- Processing hold: approximately five block periods
- Input condition: PA0 / ADC1_IN0 connected to GND

## Real-hardware result

- Input TC events: 64
- Admitted blocks: 10
- Controlled capacity drops: 54
- Accounting: admitted + drops = 64
- Processed blocks: 10
- Released blocks: 10
- Recovered admissions after drop streaks: 9
- Maximum consecutive drop streak: 6
- Every DROP changed M0AR/M1AR: NEVER
- Every DROP changed software mapping epoch: NEVER
- Dropped block entered Processing: NEVER
- FreeBufferQueue exhaustion observed: PASS
- Illegal FreeBufferQueue sends: 0
- ReadyQueue send failures: 0
- Processing notification failures: 0
- Token ledger errors: 0
- DMA TE/DME/FE: 0
- ADC OVR: 0
- BufferPool violations: 0
- DMA-slot mapping violations: 0
- Validated admitted samples: 2560
- Sample errors: 0
- Canary errors: 0
- Maximum nominal-to-decision latency: 6796 cycles
- Maximum nominal-to-IRQ-exit marker: 11850 cycles
- Maximum protected window: 1089 cycles
- Maximum ReadyQueue depth: 1
- Minimum FreeBufferQueue depth after take: 0
- Final ownership: 1 FREE + 2 DMA_OWNED
- Post-stop quiet window: PASS

The mandatory nominal completion-to-decision budget is 0.25 TB = 57600 cycles at N=256 and 200 kS/s.

## Lower-layer regression

- W1/W2/W3/W4/W5 native suite: 84 / 84 PASS
- W5Target ARM/CubeF4 build: PASS
- W4 programmed firmware bytes with W5 disabled: BYTE-IDENTICAL to the W4 hardware-tested image
- W3 programmed firmware bytes with W4/W5 disabled: BYTE-IDENTICAL to the W3 hardware-tested image
- R1 programmed firmware bytes with W3/W4/W5 disabled: BYTE-IDENTICAL to the R1 hardware-tested image

## Build identity

- Source baseline before W5 checkpoint: `7ae9a562ff0eaeeeda4a80c1bea16a6d22d7203c`
- Hardware-tested W5 ELF SHA256: `301417FD7E57F3B74927286F37359035D8FF932CDEA526775E6FB9F243E4DEAE`
- Build profile: `STREAM_LAB_R2_W3=OFF`, `STREAM_LAB_R2_W4=OFF`, `STREAM_LAB_R2_W5=ON`

## Evidence files

- `PLAN.md`
- `README.md`
- `prehardware-regression-01.txt`
- `hardware-classification-01.txt`
- `hardware-trace-01.csv`
- `hardware-inspection-01.txt`
- `w5-evidence-manifest.sha256`

## Boundary

R2-W5 closes the directed K=1 FreeBufferQueue exhaustion and controlled capacity-drop path.
R2-W6 must execute the mandatory K = 1 / 2 / 4 / 8 matrix before R2 can become a PASS candidate.
