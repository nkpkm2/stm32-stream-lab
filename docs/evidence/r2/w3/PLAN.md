# R2-W3: bounded real-DBM rebinding experiment

Status: IMPLEMENTED, TARGET BUILD AND HARDWARE VERIFICATION PENDING.
This is a diagnostic sub-package, NOT R2 acceptance or a benchmark result.

## Source identity

- Input repository HEAD: `a966cd3e78b4f190ca731daea8388463d39f7486`.
- R1 gate: `5ebf62e90b31e262f44013afb594a430061f139a`.
- Input ZIP SHA256: `6D4AC684F9D209B38418E27F5910DB40281E5C974AE87C6B43099363E8648281`.
- All 14 snapshot files were checked against W3_SNAPSHOT_MANIFEST.txt.
- Architecture: v3.2.2, sections 3 and 10. No architecture revision.

## Build selection and R1 protection

`STREAM_LAB_R2_W3` defaults to OFF. OFF retains the R1 driver/soak harness.
ON selects the W3-only driver/harness and the existing W1/W2 modules.
The two acquisition paths are mutually exclusive at build time. The four
r1_acquisition/r1_bringup source/header files, W1/W2 production modules,
FreeRTOS configuration, HAL sources and cubemx.ioc are not edited.
Only project-owned firmware CMake and main/IRQ USER CODE sections select
between the profiles. The generated HAL DMA IRQ dispatcher remains present.

W3 copies the already-reviewed R1 HAL startup sequence into an isolated
one-shot experiment: DMA callbacks -> MultiBufferStart_IT -> no HT ->
HAL_ADC_Start(external trigger) -> ADC DMA enable -> TIM2 starts last.
It is not a new permanent acquisition architecture or a driver-recovery API.

## Bounded experiment

TIM2/ADC1/DMA2 Stream0 Channel0 stay at 200000 samples/s, N=256.
K=8, P=10. B0/B1 are initially bound. The scripted replacement list is
B2..B9, used once each with no recycling and no competing consumer.
This list is test-only; it does not replace the future FreeBufferQueue.

| Event (1-based diagnostic) | CT | Rebound slot | Completed ID | Replacement ID |
|---|---|---|---|---|
| 1 | 1 | M0 | B0 | B2 |
| 2 | 0 | M1 | B1 | B3 |
| 3 | 1 | M0 | B2 | B4 |
| 4 | 0 | M1 | B3 | B5 |
| 5 | 1 | M0 | B4 | B6 |
| 6 | 0 | M1 | B5 | B7 |
| 7 | 1 | M0 | B6 | B8 |
| 8 | 0 | M1 | B7 | B9 |

After event 8, the ISR gates acquisition and stops TIM2. The task waits for
an in-flight conversion to settle, disables ADC/DMA, verifies EN=0, and
captures results. It never asks for a ninth replacement. This does NOT test
capacity-drop behavior. That remains W5.

At the last stable model observation: M0=B8, M1=B9, DMA_OWNED=2,
READY=8, FREE=0, PROCESSING=0. READY means completed/detached experiment
storage here, not descriptors published into a ReadyQueue. DMA-owned IDs
remain parked with the stopped driver; R3 reclaim/lease/reset behavior is
not implemented. B9 has been successfully armed but need not receive samples
before the bounded stop. Only B0..B7 are required to be complete blocks.

## Write transaction and failure behavior

1. Snapshot the IRQ entry cycle, CT and flags before HAL dispatch clears TC.
2. Cross-check the callback slot, expected event phase, W2 mapping, both
   hardware addresses and W1 completed/replacement owners.
3. Prepare a W2 plan without mutation.
4. In a short interrupt-masked section: reread CT, NDTR, hardware health,
   run gate and addresses; check the plan and the independent nominal window.
5. Write ONLY the selected inactive address register; DSB; read back the
   written address, confirm the active address and CT are unchanged.
6. Only on success commit W2 mapping, then W1 ownership. Validate the stable
   cross-module conservation and record a bounded trace.
7. Any unexpected error latches first-fault registers, gates the experiment,
   stops new TIM2 triggers and requests task-side quiescence. No retry,
   silent repair, active-slot write, or software rollback after a hardware
   commit. Failed runs cannot be reported as passing.

Masking CPU interrupts does not stop DMA. CT equality alone does not detect
an even number of missed hardware completions; the epoch-derived window is
independent of the first ISR timestamp and rejects such late observations.

## Timing interpretation

The start epoch is bracketed around HAL_TIM_Base_Start after CNT=0, with no
new UG after ADC is armed. Using the earlier timestamp gives a conservative
software reference: event n has nominal offset n*230400 cycles. This run is
only about 10.24 ms, so unsigned DWT subtraction suffices; no Clock64 is added.

- Prewrite acceptance: nominal age + 3600 cycles <= 57600 cycles (0.25 TB).
- Independent remaining-count check: 192 <= NDTR <= 256.
- Final check/write/readback section is interrupt-masked and contains no
  blocking HAL, FreeRTOS, queue, allocation or sample scan.
- The measured final-section duration must be <=3600 cycles. This 20 us
  allowance is a conservative diagnostic engineering budget, NOT a proven
  WCET. Any measured violation invalidates the run.
- Postwrite nominal-to-readback marker must be <=57600 cycles (320 us).
- The diagnostic IRQ exit hook marker must be <=80640 cycles (448 us).
  It precedes the hook epilogue/exception return, and is NOT full R4 runtime
  accounting or final all-load ISR-end certification.
- TIM7/SysTick and software service delay before the critical section are
  included in the epoch-derived elapsed time. W4/W5/W6 must still measure
  actual queue/consumer/control-load and drop paths in the final R2 profile.

## Memory and evidence checks

Ten static buffers have explicit four-byte alignment and before/after
canaries. Samples are seeded with 0xA55A, outside the 12-bit ADC range.
No payload is scanned while DMA can still write it. After EN=0, all 2048
samples in B0..B7 must be in 0..4095; no sentinel may remain and all canaries
must be intact. A two-tick quiet window checks for additional TC/rebind/IRQ.
PA0->GND remains the test input. No per-block UART output is used.

`g_r2_w3_result` stores phase, fault bits/first-fault registers, start/stop
registers, buffer addresses, eight traces, W1/W2 snapshots, timing markers,
and sample/canary counts. Its `test_pass` begins at zero and is set only after
successful bounded completion, hardware stop and all final checks.

## Synchronization owners

- Task owns setup; DMA is disabled.
- ISR owns run-time W1/W2 mutations and trace/counter updates.
- Task reads only volatile run status while RUNNING; it does not inspect
  partially committed W1/W2 fields.
- Task takes over final inspection after run gating and DMA IRQ disable.
- No other task or higher-urgency ISR owns or edits W3 accounting.

## Build and test entry point

`tools/r2/verify_w3.ps1 -Profile Native` builds/runs W1+W2+W3 guard/model tests.
`-Profile W3Target` builds the W3 ARM ELF with the explicit ON option.
`-Profile R1Baseline` checks that the default OFF profile still builds.
Each invocation uses a new build directory, preserves logs, checks source
byte fingerprints and leaves Git content untouched. These commands DO NOT
flash, attach, reset, commit, push or create a gate tag.

Hardware execution and acceptance are deliberately a separate next step.
No host test, mocked-register test or compile check proves actual STM32
CT timing, safe bus access, DMA integrity or the real rebinding margin.

## Source references consulted

The implementation is based on the supplied repository snapshot and frozen
Architecture v3.2.2, not an unpinned replacement SDK. ST's separately consulted
HAL v1.8.5 source documents inactive-only address updates and shows that
HAL_DMAEx_ChangeMemory writes the chosen register without enforcing CT safety.
No downloaded vendor source is installed by this package.

- https://raw.githubusercontent.com/STMicroelectronics/stm32f4xx-hal-driver/v1.8.5/Src/stm32f4xx_hal_dma_ex.c
- https://raw.githubusercontent.com/STMicroelectronics/stm32f4xx-hal-driver/v1.8.5/Src/stm32f4xx_hal_dma.c

## Remaining exclusions

No ReadyQueue, FreeBufferQueue, Processing consumer, DSP, full lifecycle,
capacity-drop campaign, K matrix, control-load stress, Clock64, Ethernet or
GUI. Successful W3 hardware evidence would close only this bounded subtest.
R2 as a whole remains IN PROGRESS; r2-pass must not be created here.
