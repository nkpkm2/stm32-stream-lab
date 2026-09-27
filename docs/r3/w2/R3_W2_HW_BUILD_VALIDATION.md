# W2-HW candidate build/link validation

## Entry and scope

`python tools/r3/r3_harness.py --repo <repo> w2-hw-validate --candidate`

This command validates the exact installed six-file target candidate on parent
`f80f774607383d01c516eff0b34fdb828f5610ba`. It writes no repository source,
Git index, commit, tag or remote. It starts no debugger, probe, flasher or board.
The five host/docs files in this tooling addition form a separate allowed scope.
The temporary review worktree therefore has exactly 11 unstaged paths, not six.
The candidate firmware SHA256 values remain unchanged.

`--candidate` grants only build-review execution for that precise dirty state.
It does not bypass the formal hardware gate, and does not turn a candidate build
into a formal H1 artifact or hardware acceptance. Existing status/selftest gates
are retained. A later formal build must use the reviewed committed source and tools.

## Mechanical gates

1. Exact parent, historical R2 annotated tag, index, 11-path envelope and all six
   candidate SHA256 values. Snapshot all tracked files plus allowed additions.
2. Full R3 Python host suite and existing harness selftest; true exit status and
   machine-readable unittest result, no stderr-as-failure heuristic or fixed PASS echo.
3. Canonical Windows Arm GNU 14.2.Rel1, CubeCLT CMake and Ninja. Record executable
   hashes and version identities. No Visual Studio environment is needed for these gates.
4. Five fresh ARM configurations: T03_A, T03_C, T03_D, T05_A and T05_B.
   Verify actual CMake cache and compile database, all four real runtime source
   paths, case defines, Cortex-M4F flags and strict warnings.
5. Inspect ELF machine, strong final IRQ/result/worker/authority symbols, the actual
   shim object provider, and the vector-table entry for the pinned TIM6_DAC IRQ.
   Real RunAuthority/QueueAdapter and historical acquisition translation units
   must not enter the W2-HW target compilation database.
6. Six negative configurations: invalid/empty selector, mixed authority,
   historical profile mix, selector without profile and missing production worker.
   An environment/tool error is NOT an expected rejection. A positive build runs first.
7. Thirteen historical fresh programmed-image hashes against the existing frozen
   values, with W2-HW/worker source intrusion checked. Expected hashes are never rewritten.
8. Full source/index/HEAD/tag snapshot equality after validation.

`--phase host|arm|negative|history` permits an explicitly selected diagnostic run.
Only `--phase all` (the default) can report all-gate PASS. Selected phases never
produce an aggregate acceptance claim. There are no automatic retries or hidden resumes.

The existing 226 C Native tests are not silently claimed to have rerun by this
command. This candidate does not change those tests or production worker code.
Their previously reported results remain prior evidence only.

## Artifacts and failure behavior

Each invocation creates a unique directory under Downloads/STM32_R3_VALIDATION.
Existing attempts are neither deleted nor overwritten. Each subprocess records
argv, timeout, stdout, stderr, exit code and output hashes before failure propagates.
A host timeout is HOST_ORCHESTRATION_TIMEOUT, not target timing failure.

`report.json`, source identities, exact review sources, logs, ELF/binary identities,
per-case audits, `review_bundle.zip`, and a relative-path MANIFEST.sha256 remain
available after PASS or FAIL. These are build-review/diagnostic records, not formal
W2 target runtime evidence. No records are retroactively described as board tests.
The small review ZIP excludes object/build caches; the complete attempt remains local.

PASS permits Principal implementation review only. It does not authorize stage,
commit, push, H2 flash or H3 stimulus. Failures preserve all existing source and results.

## Tooling quality

Tool tests cover protocol parsing, changed-state rejection, expected-error versus
wrong-error classification, compile/link oracle negatives, stderr on successful
commands, failed-command output preservation, timeouts, unique attempts and
no-overwrite reports. These tests are not simulated proof of actual ARM compilation.

## References

CMake compile database format: https://cmake.org/cmake/help/latest/variable/CMAKE_EXPORT_COMPILE_COMMANDS.html
GNU nm format/symbol types: https://sourceware.org/binutils/docs/binutils/nm.html
