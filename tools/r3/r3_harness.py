#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from r3lib import __version__, SCHEMA_VERSION
from r3lib.git_state import head, r2_pass_peeled, status
from r3lib.evidence import (
    manifest_path, next_phase, require_phase, validate_host_timeout
)
from r3lib.lifecycle import (
    LifecycleState, start_allowed, stop_allowed,
    complete_current_lease_allowed, new_claim_allowed,
)
from r3lib.cases import CASES

def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for b in iter(lambda: f.read(1024 * 1024), b""):
            h.update(b)
    return h.hexdigest().upper()

def identity(repo: Path):
    print(f"TOOL: r3_harness")
    print(f"TOOL_VERSION: {__version__}")
    print(f"TOOL_SHA256: {sha256(Path(__file__).resolve())}")
    print(f"SCHEMA_VERSION: {SCHEMA_VERSION}")
    print(f"GIT_HEAD: {head(repo)}")
    print(f"WORKTREE: {'CLEAN' if not status(repo) else 'DIRTY'}")
    print(f"R2_PASS_PEELED: {r2_pass_peeled(repo)}")
    print("CURRENT_WORK_PACKAGE: R3-W1")
    print("TARGET_FIRMWARE_MODIFICATION_ALLOWED: NO")

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
            ] or (_ for _ in ()).throw(AssertionError()),
    )
    case("manifest_positive", lambda: manifest_path("raw/a.bin") == "raw/a.bin"
         or (_ for _ in ()).throw(AssertionError()))
    case("manifest_absolute_negative", lambda: _raises(lambda: manifest_path(r"C:\a.bin")))
    case("phase_resume", lambda: next_phase(["H0", "H1", "H2"]) == "H3"
         or (_ for _ in ()).throw(AssertionError()))
    case("h3_no_rerun", lambda: require_phase("H3", ["H0","H1","H2","H3"])
         == "H3 ALREADY COMPLETE — DO NOT RERUN"
         or (_ for _ in ()).throw(AssertionError()))
    case("illegal_phase_negative", lambda: _raises(
        lambda: require_phase("H4", ["H0", "H1"])
    ))
    case("timeout_positive", lambda: validate_host_timeout(0.5, 1.0))
    case("timeout_negative", lambda: _raises(
        lambda: validate_host_timeout(1.024, 1.0)
    ))
    case("start_idle_only", lambda: (
        start_allowed(LifecycleState.IDLE)
        and not start_allowed(LifecycleState.RUNNING)
    ) or (_ for _ in ()).throw(AssertionError()))
    case("stop_states", lambda: (
        stop_allowed(LifecycleState.STARTING)
        and stop_allowed(LifecycleState.RUNNING)
        and stop_allowed(LifecycleState.QUIESCING)
        and not stop_allowed(LifecycleState.IDLE)
    ) or (_ for _ in ()).throw(AssertionError()))
    case("quiescing_current_complete", lambda: (
        complete_current_lease_allowed(LifecycleState.QUIESCING, True)
        and not new_claim_allowed(LifecycleState.QUIESCING, True)
    ) or (_ for _ in ()).throw(AssertionError()))
    case("case_catalog_nonempty", lambda: len(CASES) >= 20
         or (_ for _ in ()).throw(AssertionError()))

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
    p = argparse.ArgumentParser(description="Formal R3 lifecycle host harness")
    p.add_argument("--repo", default=".")
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status")
    sub.add_parser("selftest")
    sub.add_parser("list-cases")
    ns = p.parse_args(argv)
    repo = Path(ns.repo).resolve()

    if ns.cmd == "status":
        identity(repo)
        print("NEXT_ALLOWED: W1_HOST_QUALITY_GATE")
        return 0
    if ns.cmd == "selftest":
        identity(repo)
        rc = selftest()
        print("NEXT_ALLOWED: W1_REVIEW" if rc == 0 else "FIX_HOST_HARNESS")
        return rc
    if ns.cmd == "list-cases":
        identity(repo)
        for cid, wp, ev, desc in CASES:
            print(f"{cid}\t{wp}\t{ev}\t{desc}")
        print("NEXT_ALLOWED: W1_REVIEW")
        return 0
    raise AssertionError(ns.cmd)

if __name__ == "__main__":
    raise SystemExit(main())
