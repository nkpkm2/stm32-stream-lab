# R2-W3 Bounded Dynamic DMA DBM Rebinding Evidence

## Status

- Work package: R2-W3
- Classification: PASS
- R2 overall: IN PROGRESS
- `r2-pass` tag: NOT CREATED

## Scope

This work package validates bounded real-hardware rebinding of the inactive DMA double-buffer target.
It does not yet implement the READY / PROCESSING / FREE consumer round trip or the continuous capacity-drop path.

## Tested configuration

- Platform: STM32 NUCLEO-F446RE
- Sample rate: 200 kS/s
- Block size: 256 samples
- K: 8
- Active physical buffers P: 10
- Initial mapping: M0 = B0, M1 = B1
- Input condition: PA0 / ADC1_IN0 connected to GND

## Hardware result

- Dynamic inactive-MxAR rebinds: 8 / 8 PASS
- Physical rotation: B0/B1 -> B2..B9
- Active-slot address changed by software: NEVER
- CT stable across every protected write: PASS
- DMA TE/DME/FE: 0
- ADC OVR: 0
- BufferPool violations: 0
- DMA-slot mapping violations: 0
- Completed samples checked: 2048
- Sample errors: 0
- Canary errors: 0
- Maximum nominal-to-commit latency: 5206 cycles
- Maximum nominal-to-IRQ-exit marker: 9252 cycles
- Maximum protected write window: 1340 cycles
- Post-stop quiet window: PASS
- Final mapping: M0 = B8, M1 = B9

The R2 acceptance budget for nominal completion to commit is 0.25 TB = 57600 cycles at N=256 and 200 kS/s.
The observed maximum was 5206 cycles.

## Build identity

- Source baseline before W3 checkpoint: `a966cd3e78b4f190ca731daea8388463d39f7486`
- Hardware-tested W3 ELF SHA256: `1F9CACD7838F3985C89DA7F7AFEE6428CB70BB9C1939890D41C4FBF0490A77E1`
- Build profile: `STREAM_LAB_R2_W3=ON`

## Evidence files

- `PLAN.md`
- `hardware-classification-01.txt`
- `hardware-trace-01.csv`
- `hardware-inspection-01.txt`
- `w3-evidence-manifest.sha256`

## Boundary

R2-W3 establishes that the inactive hardware DMA slot can be safely rebound to distinct physical buffers under a bounded real-hardware experiment.
R2-W4 must add the minimal READY -> PROCESSING -> FREE ownership round trip.
R2-W5 must separately validate capacity exhaustion and the controlled drop path.
