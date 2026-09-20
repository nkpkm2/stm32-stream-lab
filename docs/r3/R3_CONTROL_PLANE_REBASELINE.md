# R3 Control-Plane Rebaseline

**Architecture baseline:** v3.2.2
**Technical baseline at rebaseline start:** `98dc2d2158e2162ff33ce5819bf4af92e0c65874`
**Historical R2 anchor:** `48da792e382e911aad1b7bb43765284aa8b58ea2`

## Purpose

R3-W2 exposed a tooling/control-plane failure mode rather than a firmware architecture failure.
The W2-A worker contract and W2-B persistent task glue remain valid technical assets.
The control plane is rebaselined so later work packages use one repository-local authority.

## Binding operator model

Formal R3 orchestration has exactly one user-facing entrypoint:

```text
python tools/r3/r3_harness.py ...
```

PowerShell is a thin launcher only.

Files downloaded outside the repository may be inert transport artifacts such as bundles,
patches or logs. They are not formal workflow controllers and are not an authority for
repository state.

## Authority

The harness derives progress from committed Git tree state and committed evidence state.
It does not maintain a separate hand-edited CURRENT_WORK_PACKAGE variable.

Working-tree files do not count as sealed implementation or acceptance evidence.

## Current technical state

At the rebaseline start:

- R2 remains PASS/CLOSED and `r2-pass` is immutable.
- R3-W1 specification/harness baseline is committed.
- R3-W2A worker contract is committed.
- R3-W2B persistent Processing/Interference task glue is committed.
- W2 formal hardware evidence has not started.
- W3 implementation has not started.

Therefore the next technical activity is W2 directed-hardware harness/preflight, not W3.

## Seal recovery model

`seal-status` is read-only and classifies both repository dirtiness and the local/remote
relationship:

- `CLEAN_SYNCED`: clean local HEAD equals remote main.
- `CANDIDATE_DIRTY`: unstaged or untracked candidate state exists; validate before staging.
- `CANDIDATE_STAGED`: index contains candidate state; require staged review before commit.
- `CANDIDATE_MIXED`: staged and unstaged state coexist; require Principal worktree review.
- `LOCAL_COMMIT_REMOTE_PARENT`: a clean local candidate commit exists and remote main is its
  parent; the safe continuation is push/verify, not rebuilding or recommitting.
- `REMOTE_UNKNOWN`: clean local state exists but remote inspection failed; do not mutate local
  Git state.
- `REMOTE_DIVERGED`: clean local/remote history requires Principal Git review.

A network failure must never cause reset/restore/clean or candidate reconstruction.

## Operator command discipline

After this control-plane rebaseline is committed, formal R3 operation returns to the W1 rule:
PowerShell is a single-line thin launcher and complex orchestration lives in the committed
repository-local Python harness.

The multiline PowerShell used to bootstrap this rebaseline is transitional only. It is not a
formal hardware workflow and must not become a recurring operator pattern.

The harness must fail closed when the working tree or index is dirty. Technical progress may
still be reported, but a dirty repository must not emit a formal hardware action as NEXT_ALLOWED.

## Evidence discipline

`docs/evidence/r3/` remains empty until formal hardware attempts occur.
Historical Downloads reports are not retroactively promoted into formal evidence.

## Scope discipline

This rebaseline changes host tooling/tests/docs only. It does not modify:

- R3 worker firmware;
- QueueAdapter/RunAuthority;
- ADC/DMA ownership code;
- historical R2 profiles;
- W2 native firmware tests;
- target hardware state.

## Case catalog

The formal catalog mirrors the frozen W1 directed matrix: 33 cases across W2-W6.
Directed cases must close before W6 soak.
