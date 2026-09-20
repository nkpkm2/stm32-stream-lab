#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from r3lib import SCHEMA_VERSION, __version__
from r3lib.cases import CASES
from r3lib.control_state import inspect_operator_gate, inspect_progress
from r3lib.evidence import (
    manifest_path, next_phase, require_phase, validate_host_timeout
)
from r3lib.git_state import head, r2_pass_peeled, status
from r3lib.lifecycle import (
    LifecycleState, complete_current_lease_allowed, new_claim_allowed,
    start_allowed, stop_allowed,
)
from r3lib.seal import SealRelation, classify_heads, classify_worktree, inspect_seal


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for b in iter(lambda: f.read(1024 * 1024), b""):
            h.update(b)
    return h.hexdigest().upper()


def identity(repo: Path) -> None:
    print("TOOL: r3_harness")
    print(f"TOOL_VERSION: {__version__}")
    print(f"TOOL_SHA256: {sha256(Path(__file__).resolve())}")
    print(f"SCHEMA_VERSION: {SCHEMA_VERSION}")
    print(f"GIT_HEAD: {head(repo)}")
    print(f"WORKTREE: {'CLEAN' if not status(repo) else 'DIRTY'}")
    print(f"R2_PASS_PEELED: {r2_pass_peeled(repo)}")


def print_status(repo: Path) -> None:
    identity(repo)
    progress = inspect_progress(repo)
    gate = inspect_operator_gate(repo, progress)
    print(f"W1_SPEC: {progress.w1_spec}")
    print(f"W2A_WORKER_CONTRACT: {progress.w2a_worker_contract}")
    print(f"W2B_TASK_GLUE: {progress.w2b_task_glue}")
    print(f"W2_EVIDENCE: {progress.w2_evidence}")
    print(f"CURRENT_WORK_PACKAGE: {progress.current_work_package}")
    print(f"TECHNICAL_NEXT_ALLOWED: {progress.next_allowed}")
    print(f"OPERATOR_GATE: {gate.state}")
    print(
        "TARGET_FIRMWARE_MODIFICATION_ALLOWED: "
        + ("YES" if gate.target_firmware_modification_allowed else "NO")
    )
    print(f"NEXT_ALLOWED: {gate.next_allowed}")


def selftest() -> int:
    tests = []

    def case(name, fn):
        try:
            fn()
            tests.append((name, True, "PASS"))
        except Exception as exc:
            tests.append((name, False, f"{type(exc).__name__}: {exc}"))

    case(
        "porcelain_protocol_shape",
        lambda: __import__("r3lib.git_state", fromlist=["parse_porcelain_v1_z"])
        .parse_porcelain_v1_z(b" M firmware/a.c\x00?? x.py\x00")
        == [
            {"xy": " M", "path": "firmware/a.c"},
            {"xy": "??", "path": "x.py"},
        ]
        or (_ for _ in ()).throw(AssertionError()),
    )
    case(
        "manifest_positive",
        lambda: manifest_path("raw/a.bin") == "raw/a.bin"
        or (_ for _ in ()).throw(AssertionError()),
    )
    case(
        "manifest_absolute_negative",
        lambda: _raises(lambda: manifest_path(r"C:\a.bin")),
    )
    case(
        "phase_resume",
        lambda: next_phase(["H0", "H1", "H2"]) == "H3"
        or (_ for _ in ()).throw(AssertionError()),
    )
    case(
        "h3_no_rerun",
        lambda: require_phase("H3", ["H0", "H1", "H2", "H3"])
        == "H3 ALREADY COMPLETE — DO NOT RERUN"
        or (_ for _ in ()).throw(AssertionError()),
    )
    case(
        "illegal_phase_negative",
        lambda: _raises(lambda: require_phase("H4", ["H0", "H1"])),
    )
    case("timeout_positive", lambda: validate_host_timeout(0.5, 1.0))
    case(
        "timeout_negative",
        lambda: _raises(lambda: validate_host_timeout(1.024, 1.0)),
    )
    case(
        "start_idle_only",
        lambda: (
            start_allowed(LifecycleState.IDLE)
            and not start_allowed(LifecycleState.RUNNING)
        )
        or (_ for _ in ()).throw(AssertionError()),
    )
    case(
        "stop_states",
        lambda: (
            stop_allowed(LifecycleState.STARTING)
            and stop_allowed(LifecycleState.RUNNING)
            and stop_allowed(LifecycleState.QUIESCING)
            and not stop_allowed(LifecycleState.IDLE)
        )
        or (_ for _ in ()).throw(AssertionError()),
    )
    case(
        "quiescing_current_complete",
        lambda: (
            complete_current_lease_allowed(LifecycleState.QUIESCING, True)
            and not new_claim_allowed(LifecycleState.QUIESCING, True)
        )
        or (_ for _ in ()).throw(AssertionError()),
    )
    case(
        "case_catalog_exact",
        lambda: len(CASES) == 33
        or (_ for _ in ()).throw(AssertionError(f"{len(CASES)} != 33")),
    )
    case(
        "w2_catalog_complete",
        lambda: {
            "W2-T03-A", "W2-T03-B", "W2-T03-C", "W2-T03-D",
            "W2-T05-A", "W2-T05-B", "W2-ACK-A", "W2-ACK-B",
        }.issubset({row[0] for row in CASES})
        or (_ for _ in ()).throw(AssertionError()),
    )
    case(
        "seal_synced",
        lambda: classify_heads("b", "a", "b") == SealRelation.CLEAN_SYNCED
        or (_ for _ in ()).throw(AssertionError()),
    )
    case(
        "seal_resume_push",
        lambda: classify_heads("b", "a", "a")
        == SealRelation.LOCAL_COMMIT_REMOTE_PARENT
        or (_ for _ in ()).throw(AssertionError()),
    )
    case(
        "seal_remote_unknown",
        lambda: classify_heads("b", "a", None) == SealRelation.REMOTE_UNKNOWN
        or (_ for _ in ()).throw(AssertionError()),
    )
    case(
        "operator_gate_clean",
        lambda: inspect_operator_gate
        and __import__("r3lib.control_state", fromlist=["derive_operator_gate"])
        .derive_operator_gate(
            __import__("r3lib.control_state", fromlist=["derive_progress"])
            .derive_progress(
                w1_sealed=True,
                w2a_sealed=True,
                w2b_sealed=True,
                w2_evidence_present=False,
            ),
            worktree_dirty=False,
        ).next_allowed == "W2_DIRECTED_HARDWARE_HARNESS_PREFLIGHT"
        or (_ for _ in ()).throw(AssertionError()),
    )
    case(
        "operator_gate_dirty",
        lambda: __import__("r3lib.control_state", fromlist=["derive_operator_gate"])
        .derive_operator_gate(
            __import__("r3lib.control_state", fromlist=["derive_progress"])
            .derive_progress(
                w1_sealed=True,
                w2a_sealed=True,
                w2b_sealed=True,
                w2_evidence_present=False,
            ),
            worktree_dirty=True,
        ).next_allowed == "REVIEW_AND_SEAL_DIRTY_WORKTREE"
        or (_ for _ in ()).throw(AssertionError()),
    )
    case(
        "seal_worktree_dirty",
        lambda: classify_worktree([{"xy": " M", "path": "a"}])
        == SealRelation.CANDIDATE_DIRTY
        or (_ for _ in ()).throw(AssertionError()),
    )
    case(
        "seal_worktree_staged",
        lambda: classify_worktree([{"xy": "M ", "path": "a"}])
        == SealRelation.CANDIDATE_STAGED
        or (_ for _ in ()).throw(AssertionError()),
    )
    case(
        "seal_worktree_mixed",
        lambda: classify_worktree([{"xy": "MM", "path": "a"}])
        == SealRelation.CANDIDATE_MIXED
        or (_ for _ in ()).throw(AssertionError()),
    )

    for name, ok, detail in tests:
        print(f"{'PASS' if ok else 'FAIL'} {name}: {detail}")
    failed = [x for x in tests if not x[1]]
    print(f"SELFTEST: {len(tests)-len(failed)} / {len(tests)} PASS")
    return 0 if not failed else 2


def _raises(fn):
    try:
        fn()
    except Exception:
        return True
    raise AssertionError("expected exception")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Formal R3 lifecycle host harness")
    parser.add_argument("--repo", default=".")
    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status")
    sub.add_parser("selftest")
    sub.add_parser("list-cases")
    sub.add_parser("seal-status")
    ns = parser.parse_args(argv)
    repo = Path(ns.repo).resolve()

    if ns.cmd == "status":
        print_status(repo)
        return 0
    if ns.cmd == "selftest":
        identity(repo)
        rc = selftest()
        gate = inspect_operator_gate(repo)
        print(
            f"NEXT_ALLOWED: {gate.next_allowed}"
            if rc == 0
            else "NEXT_ALLOWED: FIX_HOST_HARNESS"
        )
        return rc
    if ns.cmd == "list-cases":
        identity(repo)
        for cid, wp, evidence, desc in CASES:
            print(f"{cid}\t{wp}\t{evidence}\t{desc}")
        print(f"CASE_COUNT: {len(CASES)}")
        print(f"NEXT_ALLOWED: {inspect_operator_gate(repo).next_allowed}")
        return 0
    if ns.cmd == "seal-status":
        identity(repo)
        seal = inspect_seal(repo)
        print(f"SEAL_RELATION: {seal.relation.value}")
        print(f"LOCAL_HEAD: {seal.local_head}")
        print(f"LOCAL_PARENT: {seal.local_parent or 'NONE'}")
        print(f"REMOTE_MAIN: {seal.remote_head or 'UNKNOWN'}")
        if seal.relation == SealRelation.CLEAN_SYNCED:
            print("NEXT_ALLOWED: CONTINUE")
        elif seal.relation == SealRelation.CANDIDATE_DIRTY:
            print("NEXT_ALLOWED: VALIDATE_DIRTY_CANDIDATE")
        elif seal.relation == SealRelation.CANDIDATE_STAGED:
            print("NEXT_ALLOWED: PRINCIPAL_STAGED_REVIEW")
        elif seal.relation == SealRelation.CANDIDATE_MIXED:
            print("NEXT_ALLOWED: PRINCIPAL_WORKTREE_REVIEW")
        elif seal.relation == SealRelation.LOCAL_COMMIT_REMOTE_PARENT:
            print("NEXT_ALLOWED: PUSH_LOCAL_COMMIT")
        elif seal.relation == SealRelation.REMOTE_UNKNOWN:
            print("NEXT_ALLOWED: RETRY_REMOTE_INSPECTION_ONLY")
        else:
            print("NEXT_ALLOWED: PRINCIPAL_GIT_REVIEW")
        return 0
    raise AssertionError(ns.cmd)


if __name__ == "__main__":
    raise SystemExit(main())
