# R2 Final Control - CT-W4 Evidence Seal

## Status

CT-W4 is **CLOSED / KNOWN-GOOD BURST**.

This milestone proves the adverse-burst control-traffic case using the
same target programmed image already proven by CT-W3.

The frozen burst stimulus was:

- W6 `K=8 / NORMAL`;
- `R2_W6_EVENTS = 800`;
- CT long-run timeout `1500 ms`;
- ten real `PING` requests;
- `64 B` payload per request;
- `76 B` request frame per request;
- `760 B` total request burst;
- all ten request frames submitted in **one serial write**;
- no intentional inter-request delay;
- the host did not wait for a reply before the complete burst had been
  submitted.

This milestone does **not** close R2.

## Baseline

- checkpoint: `fd5ddcab2b4c2b831b2105a97ff94d83619d303f`
- host burst tool SHA256: `C70A794F61980DF6DBB0F4E6B61775C22E3AE51D37B2D32EB50BEE38717B143B`
- target ELF SHA256: `64BA47D3A9EA93BB9980D2412932223131CD17F3A6296111FDA1F5CE259CFC8D`
- target programmed SHA256: `D83E2B181F33EB1A5D994D61DA02083D096C23181015A8795E378152DC9C44F7`
- CT-W3 programmed SHA256: `D83E2B181F33EB1A5D994D61DA02083D096C23181015A8795E378152DC9C44F7`
- CT-W4 target vs CT-W3 programmed-byte identity: **PASS**
- RAM used: `97264 B / 131072 B` (`74.21%`)
- RAM free: `33808 B`

Committed-state regression also reconfirmed:

- Native W1-W6 + CT protocol: `124 / 124 PASS`;
- CT protocol tests: `8 / 8 PASS`;
- default sealed W6 programmed bytes: byte-identical PASS;
- default CT-W2 programmed bytes: hardware-tested byte identity PASS.

## Hardware Attempt 01 - PASS

H2:

- committed target image flashed and verified successfully.

H3:

- ten `64 B` PING requests;
- sequential request IDs `0x0400` through `0x0409`;
- one `760 B` host serial write;
- `0 ms` intentional inter-request delay;
- host did not wait for reply before burst submission completed;
- host burst submission completed in `62.000 ms`;
- all ten replies completed by `78.000 ms` after burst start;
- all ten requests processed while W6 was `RUNNING`;
- W6 input range `52 -> 98`;
- host return code `0`;
- host stderr empty;
- no debugger attached during runtime.

H4:

- post-run attach/read only;
- no flash;
- no reset;
- no second burst;
- no continue/restart.

H5:

- CT RX bytes / frames: `760 / 10`;
- CT processed / while-running: `10 / 10`;
- reply attempt / success / error: `10 / 10 / 0`;
- `rate_limited_count = 0`;
- `rx_overflow_count = 0`;
- parser/UART/reply errors `0`;
- max CT service cycles
  `(RX->process / process->TX / TX / RX->reply-complete)`:
  `512 / 22608 / 386752 / 409094`;
- W6 `800 / 800` admitted;
- W6 `0` capacity drops;
- W6 `800 / 800` processed/released;
- W6 `test_pass = 1`;
- W6 `fault_bits = 0`;
- max nominal-to-decision `6972 / 57600` cycles;
- max nominal-to-IRQ-exit `12132 / 80640` cycles;
- max final window `1054 / 3600` cycles;
- DMA/ADC/ownership/sample/canary errors `0`;
- final pool `8 FREE + 2 DMA`, `0 READY`, `0 PROCESSING`.

The passing attempt is preserved under `attempt-01-pass/`.

## Service-margin interpretation

The complete PING reply is transported by USART2 TX DMA. Therefore the
full `RX complete -> reply complete` interval is not itself required to fit
inside one `1.28 ms` W6 acquisition block.

The hard real-time W6 gates remain:

- nominal-to-decision `<= 57600` cycles;
- nominal-to-IRQ-exit `<= 80640` cycles;
- final window `<= 3600` cycles.

CT-W4 passed all three gates while the adverse burst was active.

The CT trace timing is retained as additional control-service evidence, not
as a replacement for the W6 hard real-time gates.

## Scope boundary

CT-W4 closes the adverse-burst real control-traffic case.

Still outstanding before final R2 acceptance:

- `K=1 / DROP` plus recovery under real control traffic;
- required remaining K/mode expansion for final control acceptance;
- final evidence review and Principal acceptance;
- creation of `r2-pass`.

No claim is made here that R2 final control-traffic acceptance is complete.
