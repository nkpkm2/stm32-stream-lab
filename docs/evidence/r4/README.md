# R4 evidence index

* [`R4 implementation and acceptance`](../../r4/R4_IMPLEMENTATION_AND_ACCEPTANCE.md)
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
