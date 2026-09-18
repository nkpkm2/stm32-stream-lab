# CT-W3 Committed-State Regression

Checkpoint:

`48da792e382e911aad1b7bb43765284aa8b58ea2`

## Result

- Native W1-W6 + CT protocol: **124 / 124 PASS**
- CT protocol tests: **8 / 8 PASS**
- default CT-OFF sealed W6 K8 NORMAL programmed bytes: **byte-identical PASS**
- default CT-W2 programmed bytes: **hardware-tested byte identity PASS**
- CT-W3 committed-state `K=8 / NORMAL / 800 events / 1500 ms` build: **PASS**

## CT-W3 identity

Pre-commit candidate ELF SHA256:

`247595061992EC9E658CA42A44F386706ED255A664A164B1BD32AC1A13DD6F06`

Hardware-tested clean committed-state ELF SHA256:

`9803E7A3974C08913C18B8285807971253680B9ACAA64572F2BCECBC4A0E578A`

Whole ELF identity between the pre-commit and committed build is **DIFFERENT**.

No whole-ELF reproducibility claim is made.

Pre-commit candidate programmed SHA256:

`D83E2B181F33EB1A5D994D61DA02083D096C23181015A8795E378152DC9C44F7`

Hardware-tested clean committed-state programmed SHA256:

`D83E2B181F33EB1A5D994D61DA02083D096C23181015A8795E378152DC9C44F7`

Programmed-byte identity: **PASS**.

The successful CT-W3 Attempt 02 was flashed from the clean committed-state
build at checkpoint `48da792e382e911aad1b7bb43765284aa8b58ea2`.

## Resource gate

- RAM used: `97264 B / 131072 B` (`74.21%`)
- RAM free: `33808 B`
- FLASH used: `50380 B / 524288 B` (`9.61%`)

## Compatibility regression

The CT-long-run timeout seam preserves the default `1000 ms` timeout unless
a CT build explicitly selects the non-default override.

The committed-state regression reconfirmed that:

- the sealed default W6 programmed image remains byte-identical;
- the default CT-W2 programmed image remains byte-identical to its
  hardware-tested image `C16309F614A5E2B918FFD2B053E45DDAD70BE36B0E3A42E9316DD8C9996696B3`;
- therefore the CT-W3-only `1500 ms` timeout does not rewrite the previously
  closed default W6 or CT-W2 programmed paths.

## Scope

This establishes committed-state provenance for CT-W3 uniform traffic only.

It does not constitute final R2 control-traffic acceptance and does not
authorize creation of `r2-pass`.

Hardware operations during this regression: **NONE**.
