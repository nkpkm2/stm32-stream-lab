# R4 evidence index

* [`R4 implementation and acceptance`](../../r4/R4_IMPLEMENTATION_AND_ACCEPTANCE.md)
* [`IRQ and queue callsite table`](r4-callsite-table.md)
* [`Final board SRAM capture`](r4-hw-final-sram.txt)

Native regression command (Windows MSVC/Ninja):

```text
cmake --build build/r4-native --target test_r4_runtime_event test_r4_tick_service
ctest --test-dir build/r4-native -R ^r4. --output-on-failure
```

Final result: 10/10 tests passed.  The separate board harness is required for
the actual ARM PRIMASK, FreeRTOS queue/port expansion and DWT timing evidence.
