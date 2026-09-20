from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from .git_state import run_git, status
from .w2_hw import W2_HW_FREEZE_PATHS

W1_PATHS = (
    "docs/r3/w1/R3_W1_SPEC_FREEZE.md",
    "docs/r3/w1/R3_DIRECTED_TEST_MATRIX.md",
    "docs/r3/w1/R3_EVIDENCE_SCHEMA_V1.md",
    "docs/r3/w1/R3_HOST_HARNESS_ARCHITECTURE.md",
    "tools/r3/r3_harness.py",
)

W2A_PATHS = (
    "firmware/runtime/r3_worker_contract.c",
    "firmware/runtime/r3_worker_contract.h",
    "tests/native/test_r3_worker_contract.c",
    "docs/r3/w2/R3_W2A_WORKER_CONTRACT_DESIGN.md",
)

W2B_PATHS = (
    "firmware/runtime/r3_worker_tasks.c",
    "firmware/runtime/r3_worker_tasks.h",
    "tests/native/test_r3_worker_tasks.c",
    "docs/r3/w2/R3_W2B_TASK_GLUE_DESIGN.md",
)


@dataclass(frozen=True)
class Progress:
    w1_spec: str
    w2a_worker_contract: str
    w2b_task_glue: str
    w2_hw_harness_freeze: str
    w2_evidence: str
    current_work_package: str
    next_allowed: str
    target_firmware_modification_allowed: bool


@dataclass(frozen=True)
class OperatorGate:
    state: str
    next_allowed: str
    target_firmware_modification_allowed: bool


def _tree_has(repo: Path, rel: str, rev: str = "HEAD") -> bool:
    return run_git(repo, "cat-file", "-e", f"{rev}:{rel}", check=False).returncode == 0


def _tree_has_all(repo: Path, paths: tuple[str, ...], rev: str = "HEAD") -> bool:
    return all(_tree_has(repo, rel, rev) for rel in paths)


def _tree_has_prefix(repo: Path, prefix: str, rev: str = "HEAD") -> bool:
    cp = run_git(repo, "ls-tree", "-r", "--name-only", rev, "--", prefix, check=False)
    return cp.returncode == 0 and bool(cp.stdout.strip())


def derive_progress(
    *,
    w1_sealed: bool,
    w2a_sealed: bool,
    w2b_sealed: bool,
    w2_evidence_present: bool,
    w2_hw_freeze_sealed: bool = False,
) -> Progress:
    if not w1_sealed:
        return Progress(
            "MISSING", "NOT_STARTED", "NOT_STARTED", "NOT_STARTED", "NOT_STARTED",
            "R3-W1", "W1_HOST_QUALITY_GATE", False,
        )
    if not w2a_sealed:
        return Progress(
            "SEALED", "NOT_STARTED", "NOT_STARTED", "NOT_STARTED", "NOT_STARTED",
            "R3-W2", "W2A_IMPLEMENTATION", True,
        )
    if not w2b_sealed:
        return Progress(
            "SEALED", "SEALED", "NOT_STARTED", "NOT_STARTED", "NOT_STARTED",
            "R3-W2", "W2B_TASK_GLUE_IMPLEMENTATION", True,
        )
    if not w2_evidence_present:
        if not w2_hw_freeze_sealed:
            return Progress(
                "SEALED", "SEALED", "SEALED", "NOT_STARTED", "NOT_STARTED",
                "R3-W2", "W2_DIRECTED_HARDWARE_HARNESS_PREFLIGHT", False,
            )
        return Progress(
            "SEALED", "SEALED", "SEALED", "SEALED", "NOT_STARTED",
            "R3-W2", "W2_HW_HARNESS_IMPLEMENTATION", True,
        )
    return Progress(
        "SEALED", "SEALED", "SEALED",
        "SEALED" if w2_hw_freeze_sealed else "NOT_STARTED",
        "PRESENT_UNASSESSED",
        "R3-W2", "W2_EVIDENCE_REVIEW", False,
    )


def derive_operator_gate(
    progress: Progress,
    *,
    worktree_dirty: bool,
) -> OperatorGate:
    if worktree_dirty:
        return OperatorGate(
            "WORKTREE_DIRTY",
            "REVIEW_AND_SEAL_DIRTY_WORKTREE",
            False,
        )
    return OperatorGate(
        "OPEN",
        progress.next_allowed,
        progress.target_firmware_modification_allowed,
    )


def inspect_progress(repo: Path) -> Progress:
    return derive_progress(
        w1_sealed=_tree_has_all(repo, W1_PATHS),
        w2a_sealed=_tree_has_all(repo, W2A_PATHS),
        w2b_sealed=_tree_has_all(repo, W2B_PATHS),
        w2_evidence_present=_tree_has_prefix(repo, "docs/evidence/r3/w2/"),
        w2_hw_freeze_sealed=_tree_has_all(repo, W2_HW_FREEZE_PATHS),
    )


def inspect_operator_gate(repo: Path, progress: Progress | None = None) -> OperatorGate:
    resolved = inspect_progress(repo) if progress is None else progress
    return derive_operator_gate(
        resolved,
        worktree_dirty=bool(status(repo)),
    )


def working_tree_label(repo: Path) -> str:
    return "CLEAN" if not status(repo) else "DIRTY"
