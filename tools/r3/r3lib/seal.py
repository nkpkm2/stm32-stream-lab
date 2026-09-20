from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from pathlib import Path

from .git_state import head, run_git, status


class SealRelation(str, Enum):
    CLEAN_SYNCED = "CLEAN_SYNCED"
    CANDIDATE_DIRTY = "CANDIDATE_DIRTY"
    CANDIDATE_STAGED = "CANDIDATE_STAGED"
    CANDIDATE_MIXED = "CANDIDATE_MIXED"
    LOCAL_COMMIT_REMOTE_PARENT = "LOCAL_COMMIT_REMOTE_PARENT"
    REMOTE_UNKNOWN = "REMOTE_UNKNOWN"
    REMOTE_DIVERGED = "REMOTE_DIVERGED"


@dataclass(frozen=True)
class SealStatus:
    relation: SealRelation
    local_head: str
    local_parent: str | None
    remote_head: str | None
    detail: str


def classify_heads(
    local_head: str,
    local_parent: str | None,
    remote_head: str | None,
) -> SealRelation:
    if remote_head is None:
        return SealRelation.REMOTE_UNKNOWN
    if remote_head == local_head:
        return SealRelation.CLEAN_SYNCED
    if local_parent is not None and remote_head == local_parent:
        return SealRelation.LOCAL_COMMIT_REMOTE_PARENT
    return SealRelation.REMOTE_DIVERGED


def classify_worktree(rows: list[dict[str, str]]) -> SealRelation | None:
    if not rows:
        return None

    staged = False
    unstaged = False
    for row in rows:
        xy = row["xy"]
        if xy == "??":
            unstaged = True
            continue
        if xy[0] != " ":
            staged = True
        if xy[1] != " ":
            unstaged = True

    if staged and unstaged:
        return SealRelation.CANDIDATE_MIXED
    if staged:
        return SealRelation.CANDIDATE_STAGED
    return SealRelation.CANDIDATE_DIRTY


def _local_parent(repo: Path) -> str | None:
    cp = run_git(repo, "rev-parse", "HEAD^", check=False)
    if cp.returncode != 0:
        return None
    return cp.stdout.decode("utf-8", "replace").strip()


def remote_main(repo: Path) -> tuple[str | None, str]:
    cp = run_git(repo, "ls-remote", "origin", "refs/heads/main", check=False)
    if cp.returncode != 0:
        detail = cp.stderr.decode("utf-8", "replace").strip()
        return None, detail or "ls-remote failed"
    rows = [
        line
        for line in cp.stdout.decode("utf-8", "replace").splitlines()
        if line.strip()
    ]
    if len(rows) != 1:
        return None, f"unexpected ls-remote row count: {len(rows)}"
    return rows[0].split()[0], "OK"


def inspect_seal(repo: Path) -> SealStatus:
    local = head(repo)
    parent = _local_parent(repo)

    work_relation = classify_worktree(status(repo))
    if work_relation is not None:
        return SealStatus(
            work_relation,
            local,
            parent,
            None,
            "remote not inspected while working tree/index is dirty",
        )

    remote, detail = remote_main(repo)
    relation = classify_heads(local, parent, remote)
    return SealStatus(relation, local, parent, remote, detail)
