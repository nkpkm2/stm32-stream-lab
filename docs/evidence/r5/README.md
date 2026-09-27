# R5 evidence index

* [`R5 implementation and acceptance`](../../r5/R5_IMPLEMENTATION_AND_ACCEPTANCE.md)
* [`Final target SRAM capture`](r5-hw-final-sram.txt)

Native regression command:

```text
cmake --build build/r5-native --target test_r5_run_metrics
ctest --test-dir build/r5-native -R ^r5. --output-on-failure
```

Final result: 7/7 R5 directed tests passed.  The board harness runs the same
synthetic classification and sealed-store paths on the STM32 target.
