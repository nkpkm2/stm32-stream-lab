"""Formal, single-case R3-W2 target evidence workflow.

This module is deliberately lazy: importing it never discovers a probe, builds
firmware, writes flash, or creates repository evidence.  The CLI invokes one
new external attempt directory for one frozen case, and H3 is never repeated
inside that directory.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
from datetime import datetime, timezone

from . import SCHEMA_VERSION, __version__
from .evidence import PHASES, validate_host_timeout
from .git_state import head, status
from .w2_build_validation import (
    CASES as SELECTORS, CommandLog, audit_elf, configure, hardware_definitions,
    select_tools, verify_cache,
)
from .w2_hw import W2_HW_HOST_TIMEOUT_S, W2_HW_TARGET_BOUND_S

CASE_TO_SELECTOR = {
    "W2-T03-A": "T03_A", "W2-T03-C": "T03_C", "W2-T03-D": "T03_D",
    "W2-T05-A": "T05_A", "W2-T05-B": "T05_B",
}
RESULT_MAGIC = 0x52335732
COMPLETED_MAGIC = 0xC04D17ED
RESULT_WORDS = 37


class WorkflowError(RuntimeError):
    pass


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest().upper()


def write_new(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as f:
        json.dump(value, f, ensure_ascii=True, indent=2)
        f.write("\n")


def parse_words(text: str) -> list[int]:
    """Parse CubeProgrammer -r32 output without accepting arbitrary hex text."""
    words: list[int] = []
    for line in text.splitlines():
        if not re.match(r"^0x[0-9A-Fa-f]{8}\s*:", line):
            continue
        _, values = line.split(":", 1)
        for value in values.split():
            if not re.fullmatch(r"[0-9A-Fa-f]{8}", value):
                raise WorkflowError(f"malformed target word: {value!r}")
            words.append(int(value, 16))
    if len(words) != RESULT_WORDS:
        raise WorkflowError(f"target result has {len(words)} words, expected {RESULT_WORDS}")
    return words


def result_address(elf: Path, nm: Path, env: dict, log: CommandLog) -> int:
    _, output, _ = log.run([nm, "-n", elf], name="result-symbol", env=env, timeout=30)
    rows = re.findall(r"^\s*([0-9a-fA-F]+)\s+[A-Za-z]\s+g_r3_w2_hw_result\s*$", output, re.M)
    if len(rows) != 1:
        raise WorkflowError("expected one g_r3_w2_hw_result symbol")
    return int(rows[0], 16)


def evaluate(case: str, words: list[int]) -> dict:
    selector = CASE_TO_SELECTOR[case]
    expected_id = SELECTORS[selector]
    terminal, fault, invariant = words[3], words[4], words[5]
    checks = {
        "result_magic": words[0] == RESULT_MAGIC,
        "schema": words[1] == 1,
        "case_identity": words[2] == expected_id,
        "terminal_pass": terminal == 1,
        "no_fault": fault == 0,
        "no_invariant": invariant == 0,
        "completion_magic": words[-1] == COMPLETED_MAGIC,
    }
    return {"result": "PASS" if all(checks.values()) else "FAIL", "checks": checks,
            "words": [f"0x{x:08X}" for x in words]}


def manifest(root: Path) -> None:
    rows = []
    for path in sorted(root.rglob("*")):
        if path.is_file() and path.name != "MANIFEST.sha256":
            rows.append(f"{digest(path)}  {path.relative_to(root).as_posix()}\n")
    with (root / "MANIFEST.sha256").open("x", encoding="utf-8", newline="\n") as f:
        f.writelines(rows)


def attempt(repo: Path, case: str, output_parent: Path) -> int:
    if case not in CASE_TO_SELECTOR:
        raise WorkflowError(f"not a W2 hardware case: {case}")
    if status(repo):
        raise WorkflowError("formal attempt requires a clean worktree")
    validate_host_timeout(W2_HW_TARGET_BOUND_S, W2_HW_HOST_TIMEOUT_S)
    output_parent = output_parent.expanduser().resolve()
    if output_parent.is_relative_to(repo.resolve()):
        raise WorkflowError("formal attempt output must be outside repository")
    output_parent.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix=f"{case.lower()}-", dir=output_parent))
    raw = root / "raw"
    log = CommandLog(root)
    completed: list[str] = []
    try:
        tools, env, tool_identity = select_tools(log)
        write_new(root / "H0.json", {"phase": "H0", "git_head": head(repo), "case": case,
                                      "toolchain": tool_identity, "target_bound_s": W2_HW_TARGET_BOUND_S,
                                      "host_timeout_s": W2_HW_HOST_TIMEOUT_S})
        completed.append("H0")
        build = root / "build"
        configure(log, repo, build, hardware_definitions(CASE_TO_SELECTOR[case]), tools, env)
        verify_cache(build, hardware_definitions(CASE_TO_SELECTOR[case]))
        log.run([tools["cmake"], "--build", build, "--parallel", "4"], name="build", env=env, timeout=600)
        audit = audit_elf(log, repo, build, tools, env)
        elf = build / "cubemx.elf"
        write_new(root / "H1.json", {"phase": "H1", "elf_sha256": digest(elf), "audit": audit})
        completed.append("H1")
        programmer = Path(r"E:\DevTools\STM32CubeCLT-1.22.0\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe")
        if not programmer.is_file():
            raise WorkflowError("STM32CubeProgrammer CLI unavailable")
        log.run([programmer, "-c", "port=SWD", "mode=UR", "-w", elf, "-v", "-rst"], name="flash-verify", timeout=120)
        write_new(root / "H2.json", {"phase": "H2", "programmed_image_sha256": audit["programmed_sha256"], "verified": True})
        completed.append("H2")
        # Reset releases the target.  One bounded wait is the frozen H3 stimulus.
        time.sleep(W2_HW_TARGET_BOUND_S)
        write_new(root / "H3.json", {"phase": "H3", "single_execution": True, "result": "COMPLETE"})
        completed.append("H3")
        address = result_address(elf, tools["nm"], env, log)
        _, readout, _ = log.run([programmer, "-c", "port=SWD", "mode=UR", "-r32", f"0x{address:08X}", str(RESULT_WORDS * 4)], name="read-result", timeout=60)
        (raw / "target-result.txt").parent.mkdir(parents=True, exist_ok=True)
        (raw / "target-result.txt").write_text(readout, encoding="utf-8", newline="\n")
        checked = evaluate(case, parse_words(readout))
        write_new(root / "H4.json", {"phase": "H4", "result_address": f"0x{address:08X}", **checked})
        completed.append("H4")
        write_new(root / "identity.json", {"schema_version": SCHEMA_VERSION, "git_head": head(repo),
            "firmware_source_commit": head(repo), "elf_sha256": digest(elf),
            "programmed_image_sha256": audit["programmed_sha256"], "harness_version": __version__,
            "harness_sha256": digest(repo / "tools/r3/r3_harness.py"), "board_profile": "NUCLEO-F446RE",
            "toolchain_identity": tool_identity["arm_version"]})
        write_new(root / "config.json", {"work_package": "R3-W2", "case_id": case, "selector": CASE_TO_SELECTOR[case]})
        write_new(root / "state.json", {"work_package": "R3-W2", "case_id": case, "attempt": 1,
            "completed_phases": completed + ["H5"], "next_allowed": "COMPLETE", "hardware_state": "SAFE"})
        write_new(root / "acceptance.json", {"result": checked["result"], "first_failure_class": None if checked["result"] == "PASS" else "TARGET_FIRMWARE", "invariants": checked["checks"]})
        write_new(root / "H5.json", {"phase": "H5", "result": checked["result"], "sealed": True})
        manifest(root)
        print(f"ATTEMPT: {root}")
        print(f"RESULT: {checked['result']}")
        return 0 if checked["result"] == "PASS" else 2
    except Exception as exc:
        write_new(root / "failure.json", {"error": str(exc), "completed_phases": completed})
        manifest(root)
        print(f"ATTEMPT: {root}")
        print(f"RESULT: FAIL: {exc}")
        return 2


def import_attempt(repo: Path, attempt_root: Path) -> Path:
    """Copy a completed external immutable attempt into its versioned evidence location."""
    acceptance = json.loads((attempt_root / "acceptance.json").read_text(encoding="utf-8"))
    config = json.loads((attempt_root / "config.json").read_text(encoding="utf-8"))
    if acceptance.get("result") != "PASS" or config.get("work_package") != "R3-W2":
        raise WorkflowError("only completed PASS R3-W2 attempts may be imported")
    dest = repo / "docs/evidence/r3/w2" / config["case_id"].lower() / "attempt-0001"
    if dest.exists():
        raise WorkflowError(f"immutable evidence destination exists: {dest}")
    shutil.copytree(attempt_root, dest)
    return dest
