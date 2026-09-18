# CT-W4 Committed-State Regression

Checkpoint:

`fd5ddcab2b4c2b831b2105a97ff94d83619d303f`

## Result

- Native W1-W6 + CT protocol: **124 / 124 PASS**
- CT protocol tests: **8 / 8 PASS**
- default CT-OFF sealed W6 K8 NORMAL programmed bytes: **byte-identical PASS**
- default CT-W2 programmed bytes: **hardware-tested byte identity PASS**
- CT-W4 target build: **PASS**
- CT-W4 target programmed bytes equal CT-W3 hardware-tested programmed bytes: **PASS**

## CT-W4 target identity

ELF SHA256:

`64BA47D3A9EA93BB9980D2412932223131CD17F3A6296111FDA1F5CE259CFC8D`

Programmed SHA256:

`D83E2B181F33EB1A5D994D61DA02083D096C23181015A8795E378152DC9C44F7`

CT-W3 hardware-tested programmed SHA256:

`D83E2B181F33EB1A5D994D61DA02083D096C23181015A8795E378152DC9C44F7`

Programmed-byte identity relative to CT-W3: **PASS**.

The target firmware did not change for CT-W4. The only new committed
stimulus at checkpoint `fd5ddcab2b4c2b831b2105a97ff94d83619d303f` is the host-side adverse-burst tool.

## Resource gate

- RAM used: `97264 B / 131072 B` (`74.21%`)
- RAM free: `33808 B`
- FLASH used: `50380 B / 524288 B` (`9.61%`)

## Scope

This establishes committed-state provenance for CT-W4 adverse-burst
traffic only.

It does not constitute final R2 control-traffic acceptance and does not
authorize creation of `r2-pass`.

Hardware operations during this regression: **NONE**.
