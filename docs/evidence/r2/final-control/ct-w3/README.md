# R2 Final Control - CT-W3 Evidence Seal

## Status

CT-W3 is **CLOSED / KNOWN-GOOD UNIFORM**.

This milestone proves the worst-permitted uniform control-traffic case used
for this acceptance stage:

- existing W6 `K=8 / NORMAL` acquisition;
- `R2_W6_EVENTS = 800`;
- approximately `1.024 s` acquisition duration;
- ten real `PING` requests;
- `64 B` request payload each;
- nominal absolute start-to-start spacing `100 ms`;
- all ten requests processed while W6 was `RUNNING`;
- no rate limiting;
- complete reply TX-DMA final-completion path for all ten requests.

This milestone does **not** close R2.

## Hardware-tested baseline

- checkpoint: `48da792e382e911aad1b7bb43765284aa8b58ea2`
- CT-W3 run timeout: `1500 ms`
- host tool SHA256: `C91EE670D0B7B5395F479A6238EA87D31DF4708584E4240551732832C341C29D`
- hardware-tested ELF SHA256: `9803E7A3974C08913C18B8285807971253680B9ACAA64572F2BCECBC4A0E578A`
- hardware-tested programmed SHA256: `D83E2B181F33EB1A5D994D61DA02083D096C23181015A8795E378152DC9C44F7`
- RAM used: `97264 B / 131072 B` (`74.21%`)
- RAM free: `33808 B`
- FLASH used: `50380 B / 524288 B` (`9.61%`)

Committed-state regression also reconfirmed:

- Native W1-W6 + CT protocol: `124 / 124 PASS`;
- CT protocol tests: `8 / 8 PASS`;
- default sealed W6 programmed bytes: byte-identical PASS;
- default CT-W2 programmed bytes: hardware-tested byte identity PASS.

## Attempt 01 - FAIL

Attempt 01 used the first 800-event CT-W3 image before the long-run timeout
was made CT-configurable.

Host-side control traffic itself succeeded:

- ten `64 B` PING requests;
- ten valid replies;
- actual first-to-last request-start span `906 ms`;
- W6 input range reported in replies `51 -> 749`;
- `rate_limited_count = 0`.

The final W6 state was:

- `phase = FAILED`;
- `fault_bits = 0x00010000` (`TIMEOUT`);
- `780 / 780` inputs admitted;
- `0` capacity drops;
- `780 / 780` processed/released;
- `199680` admitted samples validated;
- max decision / IRQ-exit / final-window cycles
  `6954 / 12106 / 1054`;
- DMA/ADC/ownership/sample/canary errors `0`.

Root cause was the **test harness timeout**, not loss of acquisition
integrity.

At `fs=200 kS/s`, `N=256`, each W6 event spans `1.28 ms`.
The required 800 events therefore need approximately `1024 ms`, while the
existing W6 run timeout was fixed at `1000 ms`.

The observed timeout at event `780` corresponds to approximately `998.4 ms`,
which matches that fixed timeout.

The failed attempt is preserved under `attempt-01-fail/`.

## Fix

The W6 run timeout was changed from a hard-coded `1000 ms` constant to
`R2_W6_RUN_TIMEOUT_MS`, whose default remains `1000 ms`.

The CMake CT acceptance seam permits an explicit `1500 ms` timeout only for
the CT long-run build.

No acquisition, DMA rebinding, ownership, controlled-drop, processing, or
timing-limit algorithm was changed by this fix.

Regression proved the default W6 and hardware-tested CT-W2 programmed bytes
remain unchanged.

## Attempt 02 - PASS

Attempt 02 used the clean committed-state build at checkpoint
`48da792e382e911aad1b7bb43765284aa8b58ea2`.

H2:

- programmed image flashed and verified successfully.

H3:

- ten real `64 B` PING requests;
- sequential request IDs `0x0300` through `0x0309`;
- nominal absolute spacing `100 ms`;
- actual first-to-last request-start span `891 ms`;
- all ten requests processed while W6 was `RUNNING`;
- W6 input range `52 -> 744`;
- host return code `0`;
- host stderr empty;
- no debugger attached during runtime.

H4:

- post-run attach/read only;
- no flash;
- no reset;
- no second traffic run;
- no continue/restart.

H5:

- CT RX bytes / frames: `760 / 10`;
- CT processed / while-running: `10 / 10`;
- reply attempt / success / error: `10 / 10 / 0`;
- `rate_limited_count = 0`;
- parser/UART/overflow errors `0`;
- max CT service cycles
  `(RX->process / process->TX / TX / RX->reply-complete)`:
  `511 / 33073 / 377775 / 410030`;
- W6 `800 / 800` admitted;
- W6 `0` capacity drops;
- W6 `800 / 800` processed/released;
- W6 `test_pass = 1`;
- W6 `fault_bits = 0`;
- max nominal-to-decision `6954 / 57600` cycles;
- max nominal-to-IRQ-exit `12106 / 80640` cycles;
- max final window `1054 / 3600` cycles;
- DMA/ADC/ownership/sample/canary errors `0`;
- final pool `8 FREE + 2 DMA`, `0 READY`, `0 PROCESSING`.

The passing attempt is preserved under `attempt-02-pass/`.

## Scope boundary

CT-W3 closes the worst-permitted **uniform** real control-traffic case.

Still outstanding before final R2 acceptance:

- adverse burst control traffic;
- DROP/recovery interaction under real control traffic;
- required remaining K/mode expansion for final control acceptance;
- final evidence review and Principal acceptance;
- creation of `r2-pass`.

No claim is made here that R2 final control-traffic acceptance is complete.
