# R6 evidence index

* [`R6 implementation and acceptance`](../../r6/R6_IMPLEMENTATION_AND_ACCEPTANCE.md)
* [`Prediction artifact persisted before target run`](r6-prediction-before-run.json)
* [`Final target SRAM capture`](r6-hw-final-sram.txt)

Native regression command:

```text
cmake --build build/r6-native --target test_r6_prediction
ctest --test-dir build/r6-native -R ^r6_ --output-on-failure
```

Final result: 3/3 directed R6 tests passed.  The prediction artifact SHA-256 is
`741E85A4977223F8D41DC69A5F5588BF62D7DEF0591A4FD74AEBFC43FB7F4778`.
The final target ELF SHA-256 is
`7491C754844E62AB2DEB6E6CFEA4476693D4C96D7C77A4DF9A951CB1455DD45C`.
