# R3 W4 Native STOP Execution Record

- Source commit: `dd52654` (W4 hardware-evidence baseline; this record is
  committed with the W4 acceptance decision)
- Date: 2026-09-27
- Host compiler: MSVC 19.50.35729.0, x64
- CMake generator: Ninja
- Build directory: external temporary directory `stm32-r3-w4-native`
- Configuration: `R3_LIFECYCLE_NATIVE_TESTS=ON`,
  `R3_WORKER_CONTRACT_NATIVE_TESTS=ON`, and
  `R3_WORKER_TASK_GLUE_NATIVE_TESTS=ON`

## Executed result

`ctest -R "^r3\\." --output-on-failure` completed **46 / 46 PASS**.

The native suite links the actual lifecycle, worker-contract, and persistent
worker-task glue C sources against host FreeRTOS stubs.  It does not emulate
the STM32 peripherals; each hardware-required W4 cell is separately proven
on the NUCLEO-F446RE under `docs/evidence/r3/w4/`.

| W4 cell | Native tests providing the required software-interleaving proof |
|---|---|
| STOP-A: all READY work canceled | `r3.worker_contract.processing_held_cancel`, `processing_drain_backlog`; `r3.worker_tasks.t03b_work_stop_race` |
| STOP-B: current Processing completes, no new claim | `r3.worker_contract.processing_current_finish`, `new_work_closes_on_stop`; `r3.worker_tasks.t03d_stop_during_processing` |
| STOP-D: duplicate STOP is one transaction | `r3.lifecycle.duplicate_running_stop`, `r3.worker_contract.duplicate_stop_idempotent`, `ack_exactly_once` |

The complete 46-test run also exercises stale STOP rejection, identity-bound
ACKs, post-ACK old-work closure, and rebind only after ACK consumption.  It
is therefore native evidence for the W4 software semantics, not a replacement
for the W4 board records.
