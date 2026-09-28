# R4 evidence index

## Formal status

R4 is **PRINCIPAL REVIEW READY WITH A PROPOSED T04 ENGINEERING DEVIATION**.
The factual T04 production result remains a valid 1919-cycle observation over
the 1800-cycle initial engineering target; it is not a PASS. The proposed
deviation package preserves that result, supplies the corrected same-event
diagnostic witness, and binds conservative cost use for R5--R7. Principal
acceptance is still required before R4 can be closed.

The two historical SRAM captures below are useful
diagnostics, but they are not formal acceptance records: they lack a committed
source/harness identity, a clean-tree H0 preflight, immutable phase records,
raw command logs, and a manifest.  They must not be used to claim R4 PASS.

Formal attempts are governed by
[`R4_FORMAL_ACCEPTANCE_PLAN.md`](../../r4/R4_FORMAL_ACCEPTANCE_PLAN.md) and
created/verified by `tools/r4/r4_evidence.py`.  A passing attempt is a
case-specific `docs/evidence/r4/<case>/attempt-0001/` directory containing
H0--H5, `identity.json`, `state.json`, raw command output and
`MANIFEST.sha256`.

The broader Principal acceptance standard is tracked separately in the
[`R4 Principal audit`](../../r4/R4_PRINCIPAL_ACCEPTANCE_AUDIT.md); sealed
T12/T15/T17/timing records below close only their stated rows.

* [`T04 engineering-deviation package`](../../r4/R4_T04_ENGINEERING_DEVIATION.md)
* [`Corrected RC3 same-event diagnostic`](t04-rc3-diagnostic/attempt-0002/README.md)

* [`R4 implementation candidate`](../../r4/R4_IMPLEMENTATION_AND_ACCEPTANCE.md)
* [`IRQ and queue callsite table`](r4-callsite-table.md)
* [`Final board SRAM capture`](r4-hw-final-sram.txt)
* [`65-second real DMA/Tick soak raw SRAM capture`](r4-hw-soak-final-sram.txt)

Long-soak verifier: `python tools/r4/verify_soak.py docs/evidence/r4/r4-hw-soak-final-sram.txt --output <verdict.json>`.
It returns PASS for 65,029 real TickService calls, no over-limit interval,
Clock64 high word 0→2, 50,781 DMA wake tails, and one post-stop no-event DMA
tail that requests no scheduler switch.

Native regression command (Windows MSVC/Ninja):

```text
cmake --build build/r4-native --target test_r4_runtime_event test_r4_tick_service
ctest --test-dir build/r4-native -R ^r4. --output-on-failure
```

Final result: 10/10 tests passed.  The separate board harness is required for
the actual ARM PRIMASK, FreeRTOS queue/port expansion and DWT timing evidence.
