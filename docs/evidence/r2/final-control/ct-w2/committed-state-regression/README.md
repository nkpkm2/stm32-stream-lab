# CT-W2 Committed-State Regression

Checkpoint:

297ae193b64f277e9cf5d8038c91396f28a6e6c1

## Result

- Native W1-W6 + CT protocol: 124 / 124 PASS
- CT protocol tests: 8 / 8 PASS
- CT-OFF sealed W6 K8 NORMAL programmed bytes: byte-identical PASS
- CT-ON committed-state build: PASS

## CT-ON identity

Hardware-tested ELF SHA256:

7C2CCA4982D39930BC4FFE31328F985880D4828F0F209C8B150F3A44D7042CFB

Committed-state rebuilt ELF SHA256:

CB507E7EBA8EB5E26083EB2FFB5E957FFB4BA5C1C44A8EA20EB8BF16E7C44B5F

Whole ELF identity: DIFFERENT.

No whole-ELF reproducibility claim is made.

Hardware-tested programmed SHA256:

C16309F614A5E2B918FFD2B053E45DDAD70BE36B0E3A42E9316DD8C9996696B3

Committed-state rebuilt programmed SHA256:

C16309F614A5E2B918FFD2B053E45DDAD70BE36B0E3A42E9316DD8C9996696B3

Programmed-byte identity: PASS.

The committed checkpoint clean-rebuilds to the exact programmed bytes
used in the successful CT-W2 hardware run.

## Scope

This establishes committed-state provenance for CT-W2 only.

It does not constitute final R2 control-traffic acceptance and does not
authorize creation of r2-pass.

Hardware operations during this regression: NONE.
