# R4 formal acceptance plan

Status: **frozen for formal evidence preparation; R4 remains IN PROGRESS.**

This plan applies architecture v3.2.2 §§4, 7, 9 and T12/T15/T17 to R4 using
the repository's frozen R3 evidence discipline.  A diagnostic result is never
retroactively promoted into an attempt.

## Non-negotiable preconditions

Every formal board attempt starts only after the R4 implementation and harness
are committed, pushed, and the worktree is clean.  It uses a fresh build and
records exact tool, probe, commit, ELF and programmed-image identities.  A
failed or incomplete attempt remains immutable and is never overwritten or
rerun in place.

## Formal matrix

| Case | Architecture requirement | Board stimulus and required decision |
|---|---|---|
| `t12-soak-a`, `t12-soak-b` | T12 | Two independent >60 s real DMA/FreeRTOS/SysTick/TIM7 runs.  Each proves Clock64 monotonic wrap observation, real DMA wake tail, no-event tail, exact IRQ pairing, no early task re-attribution, and sealed-window immutability. |
| `t15-q0` | T15 | Suspend scheduler across q0 only.  The real tick hook must advance exactly once per invocation and commit the start exactly once without synthetic recovery. |
| `t15-release` | T15 | Suspend scheduler across an occupied release slot.  It must record exactly one SKIPPED release; resume must not emit catch-up work. |
| `t17-atomic` | T17 | Pend the approved high-priority diagnostic IRQ inside the low-priority RuntimeEvent transaction; verify pairing, attribution and immutable closed window.  A separate duplicate exit must latch the specified fault. |
| `t04-commit-budget-a`, `t04-commit-budget-b` | §4.3 and T12 | Real DMA/SysTick pressure around the complete queue critical section.  Capture sufficient samples in each independent run, prefix/suffix/total maxima, and prove `t_lock <= t_commit <= t_unlock <= 1800` cycles for every sample. |

The diagnostic no-event vector is permitted only after the DMA stream is
stopped and only if DMA status flags are clear.  It establishes the handler
tail, never a fabricated completion.

## Fail-closed requirements before H0

1. Clock64 has exactly one extension state and a Monitor-owned periodic
   service interval safely below the 23.86 s DWT wrap interval at 180 MHz.
2. A detected Tick-service interval violation or Clock64-service deadline
   violation latches a named infrastructure fault and a fail-closed request.
   Its IRQ-safe half immediately closes acquisition publication, Processing
   claim and interference release; the Communication-owned task then consumes
   the request through the ordinary safe-stop transaction to quiesce DMA and
   workers before any later admission can be trusted.
3. The Tick hook only records and requests; it does not call a blocking
   lifecycle API or invent service ticks.
4. The generated callsite table covers DMA, TIM7, SysTick, tick hook, queue
   lock/commit/unlock and every enabled application IRQ.  It is checked from
   the committed source, not maintained by hand alone.

## Evidence contract

Each `docs/evidence/r4/<case>/attempt-NNNN/` contains:

```text
identity.json  config.json  state.json
H0.json H1.json H2.json H3.json H4.json H5.json
raw/ commands/ derived/ acceptance.json MANIFEST.sha256
```

The phases are: H0 preflight, H1 build/hash, H2 flash/verify, H3 exactly one
stimulus, H4 read-only target inspection plus machine verdict, H5 offline
acceptance.  `tools/r4/r4_evidence.py verify` rejects incomplete phase order,
non-clean H0, missing identities, a non-terminal state, or a bad manifest.

R4 may return to PASS only after every matrix cell has a sealed PASS attempt,
the native suite and ARM builds pass from the final commit, the callsite check
passes, GitHub contains source and evidence, and `docs/status.md` names the
final evidence index.
