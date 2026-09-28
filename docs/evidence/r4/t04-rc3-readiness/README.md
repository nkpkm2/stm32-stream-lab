# R4 T04 RC3 diagnostic readiness

This package records host-only readiness for one future, explicitly authorized
T04 diagnostic run. It is not formal T04 acceptance and contains no hardware
result.

The diagnostic profile records one coherent witness for the event that creates
the running maximum full inner interval. The default production profile omits
the witness path; its rebuilt ELF is byte-identical to the confirmed 1919-cycle
attempt ELF.

The future diagnostic run must preserve the historical 1919-cycle failure and
must not be used to declare formal T04 PASS. `EDGE_PRE_BOUND` and
`EDGE_POST_BOUND` remain required before formal closure.

The first diagnostic attempt is preserved separately as a diagnostic wiring
failure. Its snapshot export incorrectly selected case ID 12; the corrected
candidate selects COMMIT_BUDGET case ID 5 and is protected by a source audit.

To avoid the prior Codex observer timeout, the authorized run must execute the
existing harness as a PowerShell job in the user's terminal with a 180-second
allowance. The 75-second target soak/readback requirement therefore retains a
105-second host margin; the workflow performs no automatic retry.
