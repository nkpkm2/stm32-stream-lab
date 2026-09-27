#!/usr/bin/env python3
"""Create and verify immutable R4 H0--H5 evidence attempts.

This tool deliberately does not flash a board or manufacture a PASS.  It
enforces the provenance and sealing rules around those irreversible actions.
Hardware commands and their raw output are imported by the phase owner.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

PHASES = ("H0", "H1", "H2", "H3", "H4", "H5")


def run(*args: str) -> str:
    return subprocess.check_output(args, text=True, encoding="utf-8").strip()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8", newline="\n")


def attempt_path(repo: Path, case: str, attempt: int) -> Path:
    if not case or any(c not in "abcdefghijklmnopqrstuvwxyz0123456789-" for c in case):
        raise ValueError("case must be lowercase ASCII letters, digits, or '-' only")
    if attempt < 1:
        raise ValueError("attempt must be positive")
    return repo / "docs" / "evidence" / "r4" / case / f"attempt-{attempt:04d}"


def preflight(args: argparse.Namespace) -> int:
    repo = args.repo.resolve()
    if run("git", "-C", str(repo), "status", "--porcelain"):
        raise RuntimeError("H0 refuses a dirty worktree")
    head = run("git", "-C", str(repo), "rev-parse", "HEAD")
    remote = run("git", "-C", str(repo), "rev-parse", "origin/main")
    if head != remote:
        raise RuntimeError("H0 requires HEAD to equal pushed origin/main")
    destination = attempt_path(repo, args.case, args.attempt)
    if destination.exists():
        raise RuntimeError(f"attempt already exists and is immutable: {destination}")
    (destination / "raw").mkdir(parents=True)
    (destination / "commands").mkdir()
    (destination / "derived").mkdir()
    identity = {
        "schema_version": "r4-evidence-v1",
        "git_head": head,
        "firmware_source_commit": head,
        "harness_sha256": sha256(repo / "tools" / "r4" / "r4_evidence.py"),
        "board_profile": "NUCLEO-F446RE/STM32F446xx",
    }
    config = {"work_package": "R4", "case_id": args.case,
              "attempt": args.attempt, "selector": args.selector}
    state = {"work_package": "R4", "case_id": args.case,
             "attempt": args.attempt, "completed_phases": ["H0"],
             "next_allowed": "H1", "hardware_state": "NOT_TOUCHED"}
    write_json(destination / "identity.json", identity)
    write_json(destination / "config.json", config)
    write_json(destination / "state.json", state)
    write_json(destination / "H0.json", {"phase": "H0", "result": "PASS",
                                           "git_head": head, "clean_tree": True,
                                           "origin_main": remote, "case": args.case})
    print(destination)
    return 0


def verify(args: argparse.Namespace) -> int:
    root = args.attempt.resolve()
    required = [root / name for name in ("identity.json", "config.json", "state.json",
                                         "acceptance.json", "MANIFEST.sha256", *[f"{p}.json" for p in PHASES])]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise RuntimeError("missing required evidence: " + ", ".join(missing))
    state = json.loads((root / "state.json").read_text(encoding="utf-8"))
    if state.get("completed_phases") != list(PHASES) or state.get("next_allowed") != "COMPLETE":
        raise RuntimeError("state is not terminal H0--H5")
    h0 = json.loads((root / "H0.json").read_text(encoding="utf-8"))
    identity = json.loads((root / "identity.json").read_text(encoding="utf-8"))
    acceptance = json.loads((root / "acceptance.json").read_text(encoding="utf-8"))
    if not h0.get("clean_tree") or h0.get("git_head") != identity.get("git_head"):
        raise RuntimeError("H0 identity/clean-tree check failed")
    if acceptance.get("result") != "PASS":
        raise RuntimeError("acceptance is not PASS")
    expected = {}
    for line in (root / "MANIFEST.sha256").read_text(encoding="utf-8").splitlines():
        digest, relative = line.split("  ", 1)
        expected[relative] = digest
    for relative, digest in expected.items():
        candidate = root / relative
        if not candidate.is_file() or sha256(candidate) != digest:
            raise RuntimeError(f"manifest mismatch: {relative}")
    print("PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("preflight", help="create an immutable H0-only attempt")
    p.add_argument("--case", required=True)
    p.add_argument("--attempt", type=int, required=True)
    p.add_argument("--selector", required=True)
    p.set_defaults(handler=preflight)
    v = sub.add_parser("verify", help="verify a fully sealed attempt")
    v.add_argument("--attempt", type=Path, required=True)
    v.set_defaults(handler=verify)
    args = parser.parse_args()
    try:
        return args.handler(args)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
