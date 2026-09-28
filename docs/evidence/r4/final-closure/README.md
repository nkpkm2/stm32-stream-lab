# R4 final-closure classifier

Run the final classifier from the repository root:

```text
python tools/r4/r4_final_classifier.py --repo .
```

The checked-in result is
[`r4_final_classifier.json`](r4_final_classifier.json). It emits only the
three permitted domain states: `PASS`, `PASS_WITH_EXPLICIT_DEVIATION`, and
`BLOCKED`. The sole deviation is completion timing; it preserves the valid
1919-cycle production failure and binds the R6/R7 follow-up obligations.

[`r4_final_source_delta.json`](r4_final_source_delta.json) records why the
final diagnostic-only delta permits reuse of the sealed R4 and representative
R2/R3 evidence. Principal acceptance remains required; this classifier does
not grant an R4 PASS.
