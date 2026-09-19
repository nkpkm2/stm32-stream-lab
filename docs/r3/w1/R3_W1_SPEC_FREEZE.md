# R3-W1 Lifecycle Specification Freeze

**Architecture baseline:** v3.2.2
**Reviewed repository HEAD:** `6c768f0376a241d866db76889a9d080843906c5e`
**Historical R2 firmware anchor:** `48da792e382e911aad1b7bb43765284aa8b58ea2`
**R3-W1 target-firmware modification:** **FORBIDDEN**

## 1. W1 Principal decision

R2 remains PASS/CLOSED and is the immutable known-good baseline.

The current repository has strong R2→R3 foundations but does **not** yet contain a production R3 lifecycle implementation. In particular:

- `StreamRunAuthority` owns run identity / generation / lease provenance, but deliberately owns no START/STOP state machine.
- `StreamQueueAdapter` owns queue transport and exact READY/PROCESSING lease commits, but deliberately requires external quiescence for reset.
- `AdcDbmDriver` owns ADC1 / DMA2 Stream0 / TIM2 hardware, with `Arm()`, `CommitStart()` and `Stop()`.
- Historical R2 `Start()/Stop()` functions are experiment-local oracles, not lifecycle authorities.
- Historical W4/W5/W6 Processing tasks use WORK notification + zero-wait queue drain; they have no STOP/START worker contract.
- `r2_ct_control.c::CommunicationTask` is a control-traffic experiment task, not the architecture's production lifecycle coordinator.
- No production `InterferenceTask` lifecycle participation / QUIESCED ACK contract exists yet.
- No production `RunContext`, START ticket, STOP transaction, worker-ACK collection, duplicate START/STOP protocol state, or immediate-restart isolation exists yet.

Therefore W1 freezes the lifecycle model and host/evidence model before any target implementation.

## 2. Authority model — frozen

```text
Communication / R3Lifecycle
    sole lifecycle coordination, START/STOP transaction, phase/gates
        |
        +--> StreamRunAuthority
        |      current boot/run/generation + exact READY/PROCESSING lease authority
        |
        +--> StreamQueueAdapter
        |      Free/Ready queue transport + protected commit
        |
        +--> StreamOwnership
        |      logical DMA/BufferPool completion transaction
        |
        +--> AdcDbmDriver
               sole ADC1 / DMA2 Stream0 / TIM2 hardware lifecycle owner
```

Existing lower authorities remain unchanged:

```text
R2_BufferPool      -> semantic buffer owner
R2_DmaSlots        -> logical M0/M1 binding
StreamOwnership    -> physical/logical completion composition
StreamTokenLedger  -> token-validity mirror
StreamQueueAdapter -> queue transport + protected release commit
StreamRunAuthority -> run/generation/lease provenance
AdcDbmDriver       -> physical ADC/DMA/TIM2 lifecycle
```

### Hard prohibition

R3 Communication code must **not** directly write ADC/DMA/TIM2 registers or bypass the above authorities.

## 3. Lifecycle states — frozen

R3 lifecycle state is deliberately separate from later R5 measurement phases.

```c
typedef enum
{
    R3_LIFECYCLE_IDLE = 0,
    R3_LIFECYCLE_STARTING,
    R3_LIFECYCLE_RUNNING,
    R3_LIFECYCLE_QUIESCING,
    R3_LIFECYCLE_RESET_REQUIRED
} R3LifecycleState;
```

`RESET_REQUIRED` means ownership/quiescence can no longer be proven and the architecture requires full MCU reset rather than local repair.

## 4. RunContext — frozen minimum

The production `RunContext` is immutable for one accepted run. Minimum R3 fields:

```c
typedef struct
{
    uint32_t boot_id;
    uint32_t run_id;
    uint32_t generation;

    uint32_t request_id;
    uint32_t resolved_config_hash;

    uint32_t k;
    uint32_t block_samples;
    uint32_t sample_rate_hz;

    uint32_t acquisition_publish_allowed;
    uint32_t processing_claim_allowed;
    uint32_t interference_release_allowed;
} R3RunContext;
```

Later R5/R6 fields may be added without changing R3 lifecycle semantics. R3 must not implement Clock64, cohort accounting or prediction logic.

## 5. START state table — frozen

| Current | Event / condition | Atomic/serialized effect | Hardware effect | Next | Result |
|---|---|---|---|---|---|
| IDLE | valid new START | allocate new run_id/generation; bind request/config; phase STARTING; all three gates closed | none | STARTING | PREPARING |
| IDLE | duplicate accepted START, same request+binding | no new run | none | unchanged | return same transaction/run |
| IDLE | same request_id, different binding | none | none | IDLE | REQUEST_CONFLICT |
| STARTING | offline ownership init succeeds | initialize `StreamOwnership`, `StreamRunAuthority`, bind Processing task | none | STARTING | continue |
| STARTING | worker readiness preconditions hold | publish immutable prepared RunContext; clear stale pre-ticket notifications before ticket publication | none | STARTING | continue |
| STARTING | `AdcDbmDriver_Arm()` succeeds | no phase change | ADC/DMA armed; TIM2 remains stopped | STARTING | continue |
| STARTING | final START commit | publish current generation + RUNNING gates before first legal completion; consume exactly one current start ticket | `AdcDbmDriver_CommitStart()` starts TIM2 last | RUNNING | STARTED |
| STARTING | STOP before final commit | revoke ticket, keep gates closed | quiesce any armed hardware | IDLE or RESET_REQUIRED | CANCELLED/RESET |
| STARTING | any preparation failure | revoke ticket, keep gates closed, invalidate pending run | inverse rollback of owned resources only | IDLE if proven quiescent, else RESET_REQUIRED | START_FAILED |
| RUNNING | START | no new run | none | RUNNING | duplicate status or BUSY/CONFLICT |
| QUIESCING | START | none | none | QUIESCING | BUSY |
| RESET_REQUIRED | START | none | none | RESET_REQUIRED | RESET_REQUIRED |

### START invariants

1. TIM2 is the final hardware start action.
2. A valid current run identity/generation and ISR gates exist before TIM2 can produce a legal completion.
3. Expensive initialization occurs before final START commit.
4. A failed or cancelled START cannot leave a live start ticket.
5. Half-initialized hardware never returns as reconfigurable IDLE unless quiescence and resource ownership are proven.

## 6. STOP state table — frozen

| Current | Event / condition | Short commit | Long/task-context work | Next |
|---|---|---|---|---|
| RUNNING | first STOP | phase→QUIESCING; close publish/new-claim/interference-release; bind stop transaction | begin physical stop immediately | QUIESCING |
| QUIESCING | duplicate STOP same request | no second stop | return same stop progress/result | QUIESCING |
| STARTING | STOP | revoke uncommitted START ticket; keep gates closed | rollback preparation | IDLE or RESET_REQUIRED |
| QUIESCING | DMA stop complete | no normal completion publication allowed | park/reconcile two former DMA-owned buffers | QUIESCING |
| QUIESCING | Processing held READY but unclaimed | no new claim | Processing CANCELs exact held READY | QUIESCING |
| QUIESCING | Processing owns current PROCESSING lease | no new claim | current legal lease may finish and COMPLETE/release | QUIESCING |
| QUIESCING | ReadyQueue backlog | no new claim | Processing alone TAKE+CANCELs remaining READY | QUIESCING |
| QUIESCING | Interference pending | release gate closed | cancel pending job | QUIESCING |
| QUIESCING | Interference running | release gate closed | bounded segment stop/check; relinquish run state | QUIESCING |
| QUIESCING | both valid QUIESCED ACKs + DMA stopped + K+2 reconciled | seal stop transaction | reset queue/run authority offline | IDLE |
| any active state | ownership/lease/DMA uncertainty or stop grace expiry | close all gates | no local repair | RESET_REQUIRED |

### STOP invariants

1. Closing `processing_claim_allowed` does **not** forbid return of the already-claimed current PROCESSING lease.
2. Processing is the only owner allowed to cancel READY descriptors.
3. Communication never drains ReadyQueue concurrently.
4. Stop-generated partial/pending TC cannot increment normal sequence, publish READY, or advance steady-state mapping.
5. `STOPPED` protocol result is illegal before DMA is quiescent, both worker ACKs are accepted, and resource reconciliation passes.
6. Worker ACK means resource relinquishment, not "task is Blocked".

## 7. Required AdcDbmDriver STOP split — frozen implementation requirement for W4

Current `AdcDbmDriver_Stop()` correctly owns hardware quiescence but combines the short stop edge with the later HAL/settling work.

R3-W4 must preserve `AdcDbmDriver` as the sole hardware owner and expose a two-step contract equivalent to:

```c
AdcDbmDriverStatus AdcDbmDriver_BeginStop(
    AdcDbmDriverStopBeginReport *report);

AdcDbmDriverStatus AdcDbmDriver_FinishStop(
    AdcDbmDriverStopReport *report);
```

Exact names may be retained or adjusted during the W4 source patch, but the semantics are binding:

### BeginStop
- task context;
- RUNNING/ARMED/ERROR with driver-owned hardware only;
- state becomes STOPPING before any pending completion can be treated as RUNNING;
- stop TIM2 new triggers;
- disable relevant DMA completion/error interrupt sources;
- bounded, nonblocking;
- no HAL wait/abort loop;
- after return, `DispatchCompletion()` cannot publish a normal completion.

### FinishStop
- task context;
- only after BeginStop or equivalent fail-stop;
- wait/request DMA/ADC quiescence with bounded target policy;
- capture NDTR/CT/pending/error diagnostics;
- require DMA EN=0;
- clear flags/NVIC pending only after hardware no longer accesses sample memory;
- leave hardware STOPPED on success, ERROR on failure.

Communication may call these APIs but may not replace them with direct register writes.

## 8. Worker resource / ACK contract — frozen

Persistent workers are not recreated on each run.

Minimum worker identity:

```c
typedef enum
{
    R3_WORKER_PROCESSING = 1,
    R3_WORKER_INTERFERENCE = 2
} R3WorkerId;

typedef struct
{
    uint32_t boot_id;
    uint32_t run_id;
    uint32_t generation;
    uint32_t stop_id;
    R3WorkerId worker_id;
} R3WorkerQuiescedAck;
```

### Processing

Unified wait source carries `WORK | STOP | START` wake reasons.

When STOP is current:
- do not claim a new READY block;
- if an exact READY descriptor has been dequeued but not claimed, CANCEL it;
- if a PROCESSING lease is already owned, finish/release that one current block;
- then drain remaining ReadyQueue only through exact TAKE + CANCEL;
- release all run-local pointers/stat references;
- emit exactly one current-stop ACK;
- after ACK, old-run code may only enter the generation-aware wait/recheck loop.

### Interference

On STOP:
- prevent new releases;
- cancel pending-but-not-running job;
- if running, observe stop at bounded segmentation point;
- relinquish run-local job/context pointers;
- emit exactly one current-stop ACK;
- after ACK, do not write old run state.

### ACK acceptance

Communication accepts an ACK only when all of these match the current stop transaction:

`boot_id, run_id, generation, stop_id, worker_id`.

Duplicate ACK is idempotent. Stale/mismatched ACK cannot advance stop completion.

`eTaskGetState()` is diagnostic only and is not an acceptance condition.

## 9. Failure / rollback table — frozen

| Failure | Class | Required action | May return IDLE? |
|---|---|---|---|
| invalid START config before ownership/hardware | command/config | reject, no mutation | yes |
| worker precondition failure before hardware arm | lifecycle | revoke ticket, gates closed | yes if no old resource |
| `StreamOwnership_Initialize` / `StreamRunAuthority_Initialize` failure | infrastructure | gates closed; no start | only if offline reset proves clean |
| ADC/DMA arm failure | target hardware | inverse stop of driver-owned hardware | only if quiescence proven |
| final TIM2 start commit failure | target hardware | driver quiesce; invalidate run | only if quiescence proven |
| STOP during STARTING | lifecycle | revoke ticket + rollback | only if quiescence proven |
| queue/lease/provenance fault | infrastructure | close gates; run INVALID | no local authority reset; full reset policy |
| DMA/ADC unexplained error | target hardware | fail-stop + controlled/full reset policy | generally no without proof |
| stop-generated partial/pending TC | normal stop diagnostic | record only; no normal seq/READY | yes after full reconciliation |
| STOP grace timeout with unknown owner/pointer | lifecycle safety | full MCU reset | no |
| stale worker ACK/notification | generation isolation | reject; no current state mutation | unchanged |
| duplicate START same binding | protocol idempotence | return same run/status | unchanged |
| same request ID different binding | protocol conflict | reject `REQUEST_CONFLICT` | unchanged |
| duplicate STOP | protocol idempotence | return same stop transaction/progress | unchanged |
| result/TX storage still referenced | resource lifetime | `RESULT_BUSY`; do not overwrite | unchanged |

## 10. Current code gap map — frozen

| Architecture requirement | Current source evidence | W1 conclusion / later owner |
|---|---|---|
| Communication sole lifecycle coordinator | only R2 CT `CommunicationTask` exists and processes experimental frames | production coordinator missing; new R3 lifecycle integration required |
| START prepare separate from final commit | `AdcDbmDriver_Arm()` + `CommitStart()` already provide hardware split | reuse; lifecycle/ticket/gates above driver missing |
| run/generation/lease isolation | `StreamRunAuthority` implemented | reuse; connect to coordinator |
| private Free/Ready transport + protected commit | `StreamQueueAdapter` implemented | reuse |
| exact ownership composition | `StreamOwnership` implemented | reuse |
| Processing unified WORK/STOP/START wait | R2 W4/W5/W6 Processing waits WORK and drains queue | missing; W2 |
| worker QUIESCED ACK | no production worker ACK object | missing; W2 |
| Interference STOP participation | no production R3 Interference worker contract | missing; W2 |
| transactional START rollback | historical profile Start() is one-shot | missing; W3 |
| STOP gate closure before DMA shutdown | historical Stop() only changes experiment phase and hardware | missing coordinator gate transaction; W4 |
| short stop edge + long DMA quiescence | current `AdcDbmDriver_Stop()` combines both | split contract required; W4 |
| partial/pending stop TC not normal completion | driver suppresses completions when not RUNNING, Stop report captures NDTR/flags | foundation is promising; directed hardware proof required W4 |
| READY cancellation by Processing only | adapter exact CANCEL exists | worker sequencing missing; W2/W4 |
| duplicate START/STOP + old boot rejection | no production command transaction layer | W5 |
| immediate restart after final ACK | no production lifecycle | W5 |
| 1000 repeated START/STOP | none | W6 |
| immutable R3 evidence / harness state | no repo-local R3 formal harness yet | W1 host/docs baseline |

## 11. R2 non-regression policy — frozen

### Ordinary R3 lifecycle-only change
Run:
- affected R3 native tests;
- all R3 lifecycle model tests;
- strict ARM R3 target build;
- relevant R2 native/protocol regression.

### Shared lower-authority change
If changing BufferPool, DmaSlots, AdcDbmDriver completion semantics, StreamOwnership, TokenLedger, QueueAdapter, or RunAuthority:
- existing Native 191;
- foundation integration suite;
- strict ARM build;
- 13 historical programmed-image regression.

### W7 final R3 closure
At minimum:
- current firmware K8 NORMAL hardware anchor;
- current firmware K1 DROP hardware anchor;
- clean rebuild historical `r2-pass` programmed-image identity;
- all R3 directed + soak evidence.

Do not rerun the complete historical R2 hardware matrix after every ordinary R3 edit.

## 12. Git / milestone semantics — frozen

1. W1 host/docs/tooling baseline is its own commit; no target firmware.
2. W2–W5 candidate implementation must be committed before formal hardware evidence.
3. Formal hardware evidence requires committed firmware + committed harness + clean worktree + fresh build + artifact hashes.
4. Diagnostic dirty-tree hardware work is not acceptance evidence.
5. Firmware candidate/milestone commit, evidence/documentation commit, and Principal Acceptance commit are distinct.
6. `r3-pass` is created only after final Principal Acceptance and points to the firmware milestone that passed final R3 hardware acceptance.
7. `r2-pass` never moves.

## 13. W1 exit gate

W1 may close only when:
- this lifecycle specification is accepted;
- directed test matrix is accepted;
- evidence schema v1 is accepted;
- host harness architecture is installed in repo and passes host-only quality gate;
- current implementation gap map is accepted;
- no target firmware was modified.

Only then may R3-W2 target implementation begin.
