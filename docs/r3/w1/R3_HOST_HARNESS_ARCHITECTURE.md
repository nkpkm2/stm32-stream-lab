# R3 Host Harness Architecture — W1 Freeze

Formal entrypoint:

```text
python tools/r3/r3_harness.py ...
```

No formal `fix_v2.py`, `recovery_final.py`, or Downloads-based acceptance scripts.

## Internal modules

```text
tools/r3/
  r3_harness.py
  r3lib/
    git_state.py
    evidence.py
    lifecycle.py
    cases.py
```

Later work packages may add `build.py` and `hardware.py` behind the same entrypoint.

## Initial commands

```text
status
selftest
list-cases
```

Later commands are added behind the same CLI:

```text
test
attempt-status
hardware-plan
hardware-phase
evidence-verify
accept
```

## Required startup identity

Every formal action reports:
- harness version/hash;
- evidence schema;
- Git HEAD;
- worktree state;
- current work package;
- case / attempt / completed phase where applicable;
- NEXT_ALLOWED.

## Quality gate

Before W1 closes:
- Python compile;
- hardware-free import;
- CLI parse / help;
- positive fixture;
- negative fixture;
- illegal-state fixture;
- phase-resume fixture;
- Git porcelain fixture;
- evidence path/manifest fixture;
- fail-closed fixture.

Optional hardware dependencies are lazy imports.

## Operator interface

PowerShell remains a thin launcher. Repository/evidence state is authoritative; the shell session is not.
