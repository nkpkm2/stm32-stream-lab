# R3 Evidence Schema v1 — Frozen

## Directory

```text
docs/evidence/r3/
  wN/
    <case-id>/
      attempt-0001/
        identity.json
        config.json
        state.json
        H0.json
        H1.json
        H2.json
        H3.json
        H4.json
        H5.json
        raw/
        derived/
        acceptance.json
        MANIFEST.sha256
```

Only files relevant to the case/phases need exist; a phase file is never fabricated for an unexecuted phase.

## Attempt semantics

- attempt directories are immutable;
- failed attempts remain;
- no overwrite;
- hardware failure never auto-creates and runs a retry;
- next attempt number is allocated only after explicit operator/engineer action.

## Manifest

Name: exactly `MANIFEST.sha256`.

Path semantics:
- manifest-directory-relative;
- logical separator `/`;
- no absolute paths;
- no `..`;
- no automatic repo-relative guessing.

## identity.json minimum

```json
{
  "schema_version": "r3-evidence-v1",
  "git_head": "...",
  "firmware_source_commit": "...",
  "elf_sha256": "...",
  "programmed_image_sha256": "...",
  "harness_version": "...",
  "harness_sha256": "...",
  "board_profile": "...",
  "toolchain_identity": "..."
}
```

## state.json minimum

Machine-owned current attempt state:

```json
{
  "work_package": "R3-W4",
  "case_id": "W4-T04-A",
  "attempt": 1,
  "completed_phases": ["H0", "H1", "H2", "H3"],
  "next_allowed": "H4",
  "hardware_state": "HALTED|SAFE|UNKNOWN|NOT_TOUCHED"
}
```

If H3 already completed, requesting H3 returns:

`H3 ALREADY COMPLETE — DO NOT RERUN`

and performs no hardware operation.

## Hardware phases

- H0: repo/tool/preflight
- H1: fresh build + artifact identity
- H2: flash/verify only
- H3: frozen lifecycle stimulus
- H4: post-run read-only inspection
- H5: offline acceptance

## acceptance.json

Contains:
- PASS / FAIL / INCONCLUSIVE / INVALID;
- first failure class;
- machine-readable invariant results;
- exact evidence inputs;
- no result field may be silently rewritten by later phases.

## Failure classes

`TARGET_FIRMWARE, HOST_TOOL, BUILD, EVIDENCE, GIT_STATE, WORKFLOW, OPERATOR_INPUT, INFRASTRUCTURE`.

Host timeout alone is `HOST_ORCHESTRATION_TIMEOUT`, not firmware timing failure.

## Formal evidence source

Formal hardware acceptance requires:
- committed source;
- committed harness;
- clean worktree;
- fresh build;
- hashed artifact.

Dirty-tree hardware work is DIAGNOSTIC only.
