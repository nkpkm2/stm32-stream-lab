# R5 W5 host known-truth evidence

Date: 2026-09-28  
Source: `codex/r5-lifecycle` after `ec0622e` (pending this evidence commit).

## Commands

```powershell
cmake -S tests/native -B build/r5-native -DR5_METRICS_NATIVE_TESTS=ON
cmake --build build/r5-native --target test_r5_run_metrics --config Debug
ctest --test-dir build/r5-native -C Debug -R '^r5\.metrics\.' --output-on-failure
python tools/r5/r5_golden_vectors.py --check
```

## Result

- Native C: 20/20 directed cases PASS.
- Host arithmetic/vector manifest: PASS, IDs `SYN01` through `SYN14` present.
- Frozen arithmetic: deadline `D=160`, bin width `D/16=10`, exact overflow
  boundary `8D=1280`.

The 14 vector IDs are documented in
[`R5_SEMANTIC_CONTRACT.md`](../../r5/R5_SEMANTIC_CONTRACT.md) and the host
manifest.  Some IDs intentionally share a native process case where they
exercise the same state transition (for example the mixed outcome and
post-close variants); each expectation is separately asserted in C.

This is host-only evidence.  It is not hardware evidence and it does not
change the R4 dependency: no R5 formal hardware acceptance has occurred.
