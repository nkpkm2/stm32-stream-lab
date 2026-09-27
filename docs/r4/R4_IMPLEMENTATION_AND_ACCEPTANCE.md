# R4 implementation and acceptance

Status: PASS for the R4 scope defined by architecture v3.2.2 (§7, §9, T12,
T15 and T17).  This package deliberately does not claim R5 cohort sealing or
R6 prediction work.

## Runtime boundary

`r4_clock64` is the only DWT extension state.  `r4_runtime_event` owns every
ledger mutation: it saves PRIMASK, reads Clock64, settles the prior interval,
updates IRQ/task/window state and restores the original mask in one bounded
transaction.  A caller cannot provide an old timestamp.  Mismatched or
duplicate IRQ exits latch `R4_RUNTIME_IRQ_EXIT_MISMATCH`; they are never
dropped.

The completion adapter is intentionally narrower.  Only a successful
`COMPLETE` route emits the t_commit checkpoint.  INIT and CANCEL still use the
fixed V11.1.0 queue hook for ownership, but cannot create a completion timing
sample.  `t_lock`, `t_commit` and `t_unlock` surround the actual queue critical
section; boundary reads do not add redundant ledger settlement.

## Tick service boundary

`R4_TickServiceTarget_OnTickHook()` is called exactly once from the real
`vApplicationTickHook`.  The 64-bit `service_seq` is independent of FreeRTOS
`xTickCount`.  Ticket registration, q0 selection, cancellation, completion and
snapshots use short task critical sections.  The hook alone advances the
sequence and releases at most one planned job; an occupied job is recorded as
SKIPPED and is never caught up later.

## Acceptance matrix

| Requirement | Code/test evidence | Result |
|---|---|---|
| Clock64 wrap, monotonic atomic RuntimeEvent, nested IRQ, task transitions and sealed windows | `test_r4_runtime_event` (six directed native cases) | PASS |
| T12 unique real IRQ wiring | [`r4-callsite-table.md`](../evidence/r4/r4-callsite-table.md) and fixed V11.1.0 source | PASS |
| T15 q0/period single service domain, occupied skip and no catch-up | `test_r4_tick_service` (three directed native cases), board record | PASS |
| T17 high IRQ pending inside masked RuntimeEvent; close sealing; duplicate exit | board-only TIM6 diagnostic and raw SRAM record | PASS |
| full t_lock→t_commit→t_unlock budget | 15 board samples, max 1735 cycles ≤ 1800 | PASS |

The board-only T17 hooks are compiled only with `STREAM_LAB_R4_HW`; production
R4 profiles cannot arm the diagnostic TIM6 software interrupt.
