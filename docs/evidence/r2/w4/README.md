# R2-W4 READY / PROCESSING / FREE Round-Trip Evidence

## Status

- Work package: R2-W4
- Classification: PASS
- R2 overall: IN PROGRESS
- `r2-pass` tag: NOT CREATED

## Scope

This work package validates the minimal real-hardware ownership round trip:

`DMA_OWNED -> READY -> PROCESSING -> FREE -> DMA_OWNED`

It intentionally does not force FreeBufferQueue exhaustion or validate the controlled capacity-drop path. That remains R2-W5.

## Tested configuration

- Platform: STM32 NUCLEO-F446RE
- Sample rate: 200 kS/s
- Block size: 256 samples
- K: 4
- Active physical buffers P: 6
- FreeBufferQueue capacity: 4
- ReadyQueue capacity: 4
- Test events: 64
- Processing operation: descriptor validation plus 256-sample raw scan and immediate controlled release
- Input condition: PA0 / ADC1_IN0 connected to GND

## Real-hardware result

- DMA -> READY admissions: 64 / 64 PASS
- READY -> PROCESSING claims: 64 / 64 PASS
- PROCESSING -> FREE controlled release commits: 64 / 64 PASS
- Reuse from FREE back into DMA ownership: PASS
- Initialization FreeBufferQueue commit hooks: 4
- Completion release commit hooks: 64
- Illegal FreeBufferQueue sends: 0
- FreeBufferQueue empty events: 0
- ReadyQueue send failures: 0
- Processing notification failures: 0
- Token ledger errors: 0
- DMA TE/DME/FE: 0
- ADC OVR: 0
- BufferPool violations: 0
- DMA-slot mapping violations: 0
- Completed samples validated: 16384
- Sample errors: 0
- Canary errors: 0
- Maximum nominal-to-commit latency: 4978 cycles
- Maximum nominal-to-IRQ-exit marker: 9982 cycles
- Maximum protected write window: 1084 cycles
- Maximum ReadyQueue depth: 1
- Minimum FreeBufferQueue depth after take: 3
- Final ReadyQueue depth: 0
- Final FreeBufferQueue depth: 4
- Final ownership: 4 FREE + 2 DMA_OWNED
- Post-stop quiet window: PASS

The mandatory nominal completion-to-commit budget is 0.25 TB = 57600 cycles at N=256 and 200 kS/s.

## Lower-layer regression

- W1/W2/W3/W4 native suite: 72 / 72 PASS
- W4Target ARM/CubeF4 build: PASS
- W3 programmed firmware bytes with W4 disabled: BYTE-IDENTICAL to the W3 hardware-tested image
- R1 programmed firmware bytes with W3/W4 disabled: BYTE-IDENTICAL to the R1 hardware-tested image

## Build identity

- Source baseline before W4 checkpoint: `f5d0f086dae94d2f244f8366a65aa32c511fd5be`
- Hardware-tested W4 ELF SHA256: `D2018A7B03F65E66E66CCCE1045234DB346AED684BB7B6895D5F2A2097EA4A88`
- Build profile: `STREAM_LAB_R2_W3=OFF`, `STREAM_LAB_R2_W4=ON`

## Evidence files

- `PLAN.md`
- `README.md`
- `prehardware-regression-01.txt`
- `hardware-classification-01.txt`
- `hardware-trace-01.csv`
- `hardware-inspection-01.txt`
- `w4-evidence-manifest.sha256`

## Boundary

R2-W4 closes the normal ownership round-trip path only.
R2-W5 must deliberately exhaust the FreeBufferQueue and prove controlled capacity drops without duplicate ownership, stale publication, active-slot writes, or DMA corruption.
