# R3-W2 Hardware Harness Design Freeze

**Architecture baseline:** v3.2.2
**Technical baseline:** `87bd6bc5a1f5500d8ef37eb27eee7f37e22faf05`
**Historical R2 anchor:** `48da792e382e911aad1b7bb43765284aa8b58ea2`
**Work package:** R3-W2 — Worker STOP / QUIESCED Contract

## Purpose

This freeze defines the target-only harness required to close the hardware portion of R3-W2.
It does not implement the harness and does not authorize flashing hardware. Implementation is
allowed only after this freeze is committed and the repository-local `w2-hw-preflight` passes.

## Claim boundary

Formal W2 hardware evidence may claim only that the production persistent Processing and
Interference workers satisfy the W2 STOP/quiescence contract on real Cortex-M4F hardware with
the real FreeRTOS scheduler, task notifications, priorities and FromISR wake path.

It must not claim validation of:

- ADC acquisition or sample correctness;
- DMA DBM operation, M0AR/M1AR rebinding or DMA ownership commits;
- TIM2 START/STOP sequencing;
- DMA EN=0 shutdown, partial DMA classification or pending TC handling;
- STOP-generated partial publication;
- W3 transactional START semantics;
- W4 physical STOP/reconciliation semantics.

Those remain later work-package responsibilities.

## Directed case partition

Hardware-required W2 cases are exactly:

1. `W2-T03-A` — Processing blocked on empty READY source, STOP wakes it, worker ACKs.
2. `W2-T03-C` — Processing holds exact READY, STOP wins claim linearization, exact CANCEL, ACK.
3. `W2-T03-D` — PROCESSING claim already won, STOP allows only current bounded completion, ACK.
4. `W2-T05-A` — Interference pending, STOP wins release linearization, segment does not start, ACK.
5. `W2-T05-B` — Interference segment already running, STOP allows one bounded finish, no next segment, ACK.

Native-only W2 cases remain exactly:

- `W2-T03-B` — WORK/STOP race immediately before wait;
- `W2-ACK-A` — duplicate/stale ACK handling;
- `W2-ACK-B` — last ACK wake and old-tail mutation closure.

The five hardware cases do not replace their native oracles; they add real scheduler/interrupt
execution evidence where target execution is materially different from host execution.

## Test seam

The W2 target harness uses the production:

- `r3_worker_contract.c`;
- `r3_worker_tasks.c`;
- FreeRTOS kernel and Cortex-M4F port;
- static worker tasks and real task notifications;
- `R3WorkerTasks_NotifyProcessingWorkFromISR()` path.

The harness must not manufacture fake physical DMA ownership. Instead it uses a test-only
`r3_w2_hw_authority_shim` implementing the narrow StreamRunAuthority-facing symbols consumed
by `r3_worker_tasks.c` to place workers in READY / HELD_READY / PROCESSING scenarios.

`AUTHORITY_LINK_MODE: TEST_SHIM_REPLACES_STREAM_RUN_AUTHORITY`

`REAL_AUTHORITY_SOURCE_TOKEN: ../runtime/stream_run_authority.c`

The current foundation queue-adapter profile also compiles the production
`stream_run_authority.c`. Therefore the W2-HW CMake profile must make the link substitution
explicit: when `STREAM_LAB_R3_W2_HW=ON`, production `stream_run_authority.c` must not be linked
into the target that links `r3_w2_hw_authority_shim.c`. This prevents duplicate authority
symbols and prevents the test harness from silently exercising a mixture of test and
production authority implementations.

This is a build/link substitution only. It does not modify production
`stream_run_authority.c`, `stream_queue_adapter.c`, worker code, or any off-profile build.
Historical profiles with `STREAM_LAB_R3_W2_HW=OFF` must retain their existing source set and
programmed-byte identities.

## Build profile

The dedicated profile name is:

`STREAM_LAB_R3_W2_HW`

It must be mutually exclusive with historical R2 W3/W4/W5/W6 profiles and must require the
production R3 worker contract/task modules. Historical R2 programmed-byte profiles must remain
unchanged when this profile is OFF.

Exactly one hardware case is selected at configure time through:

`CASE_SELECTOR: STREAM_LAB_R3_W2_HW_CASE`

Allowed cache values and their immutable formal case mapping are:

- `T03_A` -> `W2-T03-A`
- `T03_C` -> `W2-T03-C`
- `T03_D` -> `W2-T03-D`
- `T05_A` -> `W2-T05-A`
- `T05_B` -> `W2-T05-B`

CMake must reject any other value. The selected case must be compiled into the artifact
identity; runtime selection over UART, debugger writes, or mutable RAM is not allowed for a
formal W2 attempt.

Planned new target files are exactly:

- `firmware/runtime/r3_w2_hw_harness.c`
- `firmware/runtime/r3_w2_hw_harness.h`
- `firmware/runtime/r3_w2_hw_authority_shim.c`
- `firmware/runtime/r3_w2_hw_authority_shim.h`

Planned modified integration files are exactly:

- `firmware/cubemx/CMakeLists.txt`
- `firmware/cubemx/Core/Src/main.c`

Production `r3_worker_tasks.*`, `r3_worker_contract.*`, QueueAdapter, RunAuthority and ADC/DBM
sources are not modified by the planned harness implementation unless a separately reviewed
production defect is found.

## Synthetic ISR boundary

The W2 target harness reserves `TIM6_DAC_IRQn` as a software-pended synthetic producer IRQ.
Preflight must scan all tracked target `firmware/**/*.c` sources in committed `HEAD` for a
strong `TIM6_DAC_IRQHandler` definition, not only `stm32f4xx_it.c`. The harness may provide a
strong handler only when no competing committed definition exists and only under
`STREAM_LAB_R3_W2_HW`.

The IRQ priority is frozen at library priority `5`, equal to
`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`, so FreeRTOS FromISR APIs are legal while still
exercising the configured interrupt-priority contract.

The IRQ handler may call only the W2 test/harness entry required to reach
`R3WorkerTasks_NotifyProcessingWorkFromISR()` and then `portYIELD_FROM_ISR()` as appropriate.

## Target result boundary

Each boot executes one compile-time frozen case and ends in a stable completed state. A
volatile target result record must expose at least:

- result magic and schema/version;
- compile-time formal case ID;
- terminal PASS/FAIL code and first fault;
- run/generation/stop identity used by the case;
- Processing wake/cancel/complete counts;
- Interference segment count;
- Processing and Interference ACK observed flags;
- invariant/failure bitset;
- `completed_magic`.

`RESULT_COMMIT_RULE: completed_magic_written_last_after_DMB`

The controller writes all result fields first, executes a data-memory barrier, and writes the
expected `completed_magic` last. H4 accepts a terminal result only when both the result magic
and completion magic match the frozen schema. This prevents a debugger read from treating a
partially written result as a completed attempt.

After publishing the completion marker, the harness must disable its synthetic IRQ source and
enter a stable terminal state without starting another case.

H4 reads this record through a read-only debugger operation. W2 does not add a UART protocol.

## Timeout policy

The initial W2 target case bound is `0.5 s`. The formal host orchestration timeout is `2.0 s`,
which satisfies the W1 requirement that host timeout be at least 2x the target bound. Any case
requiring a larger target bound must be reviewed and documented before hardware execution.

## Formal hardware phase mapping

Formal W2 hardware attempts continue to use W1 H0-H5:

- H0 — committed source/harness, clean tree, canonical tools, case identity and evidence path;
- H1 — fresh W2-HW build and artifact identity;
- H2 — flash/program/verify;
- H3 — execute exactly one frozen case; never rerun an already-completed H3 attempt;
- H4 — read-only target result inspection;
- H5 — offline acceptance, manifest and immutable evidence seal.

No formal H2/H3 activity is authorized by this design-freeze commit.

## Evidence layout

Formal evidence begins only after the implementation/harness is committed and reviewed:

`docs/evidence/r3/w2/<case>/attempt-0001/`

Each attempt is immutable and follows evidence schema v1. Failed attempts are preserved and
never overwritten.

## Exit criteria for this freeze

This design-freeze gate closes only when:

1. the repository-local harness recognizes this freeze from committed Git state;
2. the W2 case partition is exactly 5 hardware + 3 native-only;
3. baseline W2 worker implementation is present;
4. `STREAM_LAB_R3_W2_HW` and planned target harness sources are still absent;
5. formal W2 evidence is still absent;
6. `TIM6_DAC_IRQn` is available and FreeRTOS syscall priority is 5;
7. the production worker ISR API is present;
8. the authority link-substitution contract is frozen and the current CMake coupling is
   recognized as requiring replacement of production `stream_run_authority.c` in W2-HW builds;
9. the configure-time case selector and exact five-value mapping are frozen;
10. the target result completion-marker protocol is frozen;
11. host tests/selftests pass and the tree is clean after commit.

Only then is `W2_HW_HARNESS_IMPLEMENTATION` allowed.
