# R2-W6 Failures and Deviations

This file records engineering-significant host/tooling deviations observed while executing W6.
They are preserved for provenance and are not rewritten as firmware failures.

## MSVC environment bootstrap

- Initial Native verification failed before tests because `verify_w6.ps1` invoked `VsDevCmd.bat` inside an already initialized Visual Studio developer environment, producing `input line too long` / command syntax failure.
- Direct `cl.exe` compilation proved the existing MSVC environment was valid.
- The verifier was minimally changed to reuse an existing x64 developer environment when `cl.exe` and VSCMD x64 markers are already present; it falls back to VsDevCmd only when needed.
- Final verifier SHA256: `BF632BB6C59998C5DBF00D36B734154AA261205847329728B13DDCC838C1AA42`.

## Diagnostic-script mistakes

- Early `cl.exe /Bv` probes were written incorrectly and produced host-side errors; they did not change firmware or MCU state.
- The first K=1 DROP trace validation used C-style integer suffixes such as `1U` in Windows PowerShell 5.1. PowerShell rejected the script at parse time. The run was not repeated; trace export was retried from the existing RAM result using corrected tooling.
- The first generic run-summary tool read per-buffer arrays beyond their valid K+2 length at K=2, causing a false host-side accounting failure. The underlying K=2 hardware summary was internally consistent for buffers 0..3. `r2_w6_run_summary_v2.ps1` fixed the boundary and later cells used it.
- Several pre-commit wrapper revisions failed due host-side PowerShell argument/output parsing (`array splatting`, `Write-Host` marker capture, and `$Label:` parser syntax). These failures occurred before MCU operations and are preserved as superseded tools.

## Source provenance deviation

- The handoff snapshot copy of `r2_w6_matrix.c` had SHA256 `2BD42D4B477D99030EAACF892354EDCBB13EADC9B4A667A963945329D705795D`.
- The final hardware-tested/current source has SHA256 `7C0F7DEFFCC885965A926B7A3737F684E1D7D951C4D44A96FF33B60CF8930B4F`.
- Because the file was untracked during development, Git could not identify the exact edit moment.
- This uncertainty was closed by pre-commit reconstruction: current source reproduced the programmed bytes of all eight hardware-tested W6 images (8/8 PASS).
- The committed firmware milestone then passed committed-state programmed-byte regression against all eight W6 cells plus W5/W4/W3/R1 (12/12 PASS).

## Evidence integrity rule

No failed host wrapper output is used as positive hardware evidence. Formal claims are tied to the final passing hardware summaries/traces, exact ELF/programmed-image identities, and the committed-state regression.
