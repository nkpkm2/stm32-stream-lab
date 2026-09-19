# R3 Directed Test Matrix — W1 Freeze

The directed cases close specific lifecycle races. 1000-cycle soak is not allowed to substitute for them.

## W2 — worker STOP / quiescence

| ID | Evidence | Scenario | Required oracle |
|---|---|---|---|
| W2-T03-A | native + hardware | ReadyQueue empty, Processing blocked, STOP | Processing wakes and ACKs; no queue deadlock |
| W2-T03-B | native | WORK/STOP race immediately before wait | no lost wakeup; STOP wins new-claim decision |
| W2-T03-C | native + hardware | Processing dequeued READY but has not claimed | exact CANCEL; no lost buffer |
| W2-T03-D | native + hardware | Processing already owns PROCESSING block | no new claim; current block may COMPLETE/release |
| W2-T05-A | native + hardware | Interference pending, STOP | pending cancelled; ACK |
| W2-T05-B | native + hardware | Interference running, STOP | bounded exit; ACK |
| W2-ACK-A | native | duplicate/stale ACK | no current stop progress mutation |
| W2-ACK-B | native | last ACK immediately wakes Communication | old worker tail cannot touch old/new run state |

## W3 — transactional START / rollback

| ID | Evidence | Scenario | Required oracle |
|---|---|---|---|
| W3-START-A | native + hardware | normal START | RunContext/generation/gates visible before TIM2 start |
| W3-START-B | native | invalid config | no ownership/hardware mutation |
| W3-T06-A | native + hardware | DAC prepared, ADC/DBM arm fails | inverse rollback or RESET_REQUIRED; no half-IDLE |
| W3-T06-B | native + hardware | STOP before final start commit/q0 | ticket revoked; TIM2 never starts |
| W3-START-C | native + hardware | final CommitStart fails | no RUNNING state survives |
| W3-START-D | native | stale start ticket/generation | rejected fail-closed |

## W4 — safe STOP / DMA partial / pending IRQ

| ID | Evidence | Scenario | Required oracle |
|---|---|---|---|
| W4-T04-A | hardware | half-filled DMA block + STOP | partial block diagnostic only; no seq/READY |
| W4-T04-B | hardware | pending TC at STOP edge | no old completion leaks to next run |
| W4-STOP-A | native + hardware | READY backlog + STOP | Processing alone cancels all READY |
| W4-STOP-B | native + hardware | current PROCESSING block + STOP | current completes; new claims closed |
| W4-STOP-C | hardware | K1 DROP/recovery region + STOP | K+2 reconciled; no mapping/owner corruption |
| W4-STOP-D | native + hardware | duplicate STOP during DMA quiesce | same stop transaction; no second shutdown |
| W4-STOP-E | hardware | DMA/ADC error during quiesce | RESET_REQUIRED / invalid, no local cosmetic repair |

## W5 — generation isolation / idempotence / immediate restart

| ID | Evidence | Scenario | Required oracle |
|---|---|---|---|
| W5-T13-A | native + hardware | duplicate START same request/binding | same run; no second start |
| W5-T13-B | native | same request ID different binding | REQUEST_CONFLICT |
| W5-T13-C | native + hardware | duplicate STOP | same stop transaction/result |
| W5-T13-D | native + hardware | old boot/session command | cannot start current boot |
| W5-T13-E | native | stale notification | cannot mutate current generation |
| W5-T13-F | native | stale worker ACK | cannot complete current STOP |
| W5-T13-G | native + hardware | final ACK → immediate new START → old worker returns from ACK API | no old-worker mutation of new run |
| W5-T20-A | native + hardware | result TX/storage still referenced, new START | bounded RESULT_BUSY; bytes immutable |
| W5-T20-B | native + hardware | result released and TX ended | next START genuinely accepted |

## W6 — repeated lifecycle

### Anchors

- A: K8 / NORMAL / N256 / 200 kS/s
- B: K1 / DROP / N256 / 200 kS/s
- C: K4 / NORMAL / N512 / 200 kS/s

### Soak

- 500 cycles A
- 500 cycles B

Each cycle verifies:
- correct boot/run/generation;
- no stale ACK/READY/notification;
- DMA EN=0 at completed STOP;
- workers quiesced;
- queue/lease/pool reconciliation;
- no owner/ledger/driver invariant failure.

STOP placement uses deterministic phase sweep: waiting/empty, post-admission, Processing active, backlog/drop region.

## Execution rule

Directed cases must PASS before W6 soak. Hardware-affecting failure ends the attempt; no automatic retry.
