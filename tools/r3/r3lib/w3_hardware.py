"""Formal, single-execution R3-W3 target evidence workflow."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import shutil
import tempfile
import time

from . import SCHEMA_VERSION, __version__
from .evidence import PHASES, validate_host_timeout
from .git_state import head, status
from .seal import SealRelation, inspect_seal
from .w2_build_validation import CommandLog, configure, select_tools, verify_cache

CASE_TO_SELECTOR = {
    "W3-START-A": "START_A", "W3-T06-A": "T06_A",
    "W3-T06-B": "T06_B", "W3-START-C": "START_C",
}
CASE_IDS = {"START_A": 1, "T06_A": 2, "T06_B": 3, "START_C": 4}
RESULT_MAGIC = 0x52335733
COMPLETED_MAGIC = 0xA33C0DE3
RESULT_WORDS = 21
TARGET_BOUND_S = 0.5
HOST_TIMEOUT_S = 3.0


class WorkflowError(RuntimeError):
    pass


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for part in iter(lambda: f.read(1024 * 1024), b""):
            h.update(part)
    return h.hexdigest().upper()


def write_new(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as f:
        json.dump(value, f, ensure_ascii=True, indent=2, allow_nan=False)
        f.write("\n")


def definitions(selector: str) -> dict[str, str]:
    return {
        "STREAM_LAB_FOUNDATION_ADC_DBM_DRIVER": "ON",
        "STREAM_LAB_FOUNDATION_OWNERSHIP_CORE": "ON",
        "STREAM_LAB_FOUNDATION_TOKEN_LEDGER": "ON",
        "STREAM_LAB_FOUNDATION_QUEUE_ADAPTER": "ON",
        "STREAM_LAB_R3_WORKER_CONTRACT": "ON",
        "STREAM_LAB_R3_WORKER_TASKS": "ON",
        "STREAM_LAB_R3_LIFECYCLE": "ON",
        "STREAM_LAB_R3_W2_HW": "OFF",
        "STREAM_LAB_R3_W2_HW_CASE": "",
        "STREAM_LAB_R3_W3_HW": "ON",
        "STREAM_LAB_R3_W3_HW_CASE": selector,
    }


def parse_words(text: str) -> list[int]:
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
    _, listing, _ = log.run([nm, "-n", elf], name="result-symbol", env=env, timeout=30)
    rows = re.findall(r"^\s*([0-9a-fA-F]+)\s+[A-Za-z]\s+g_r3_w3_hw_result\s*$", listing, re.M)
    if len(rows) != 1:
        raise WorkflowError("expected one g_r3_w3_hw_result symbol")
    return int(rows[0], 16)


def evaluate(case: str, words: list[int]) -> dict:
    selector = CASE_TO_SELECTOR[case]
    common = {
        "result_magic": words[0] == RESULT_MAGIC,
        "schema": words[1] == 1,
        "case_identity": words[2] == CASE_IDS[selector],
        "terminal_pass": words[3] == 1,
        "no_invariant": words[4] == 0,
        "no_runtime_fault": words[17] == 0,
        "completion_magic": words[20] == COMPLETED_MAGIC,
    }
    if selector == "START_A":
        specific = {
            "running": words[5] == 2,
            "ticket_revoked": words[6] == 0,
            "all_gates_open": words[7:10] == [1, 1, 1],
            "driver_running_owned": words[10:12] == [3, 1],
            "tim2_started": (words[12] & 1) != 0,
        }
    else:
        specific = {
            "idle": words[5] == 0,
            "all_gates_closed": words[7:10] == [0, 0, 0],
            "hardware_released": words[11] == 0,
            "tim2_never_started": (words[12] & 1) == 0,
            "rollback_acks": words[16] == 0x300,
            "rollback_recorded": words[19] >= 1,
        }
    checks = {**common, **specific}
    return {"result": "PASS" if all(checks.values()) else "FAIL", "checks": checks,
            "words": [f"0x{word:08X}" for word in words]}


def manifest(root: Path) -> None:
    rows = []
    for path in sorted(root.rglob("*")):
        if path.is_file() and path.name != "MANIFEST.sha256":
            rows.append(f"{digest(path)}  {path.relative_to(root).as_posix()}\n")
    with (root / "MANIFEST.sha256").open("x", encoding="utf-8", newline="\n") as f:
        f.writelines(rows)


def validate_manifest(root: Path) -> None:
    expected: dict[str, str] = {}
    for raw in (root / "MANIFEST.sha256").read_text(encoding="utf-8").splitlines():
        matched = re.fullmatch(r"([0-9A-F]{64})  ([^\\/]+(?:/[^\\/]+)*)", raw)
        if matched is None or matched.group(2) == "MANIFEST.sha256":
            raise WorkflowError("invalid evidence manifest row")
        expected[matched.group(2)] = matched.group(1)
    actual = {path.relative_to(root).as_posix(): digest(path) for path in root.rglob("*")
              if path.is_file() and path.name != "MANIFEST.sha256"}
    if expected != actual:
        raise WorkflowError("evidence manifest does not match attempt files")


def audit_elf(repo: Path, build: Path, selector: str, tools: dict, env: dict, log: CommandLog) -> dict:
    elf = build / "cubemx.elf"
    if not elf.is_file():
        raise WorkflowError("target ELF missing")
    _, header, _ = log.run([tools["readelf"], "-h", elf], name="elf-header", env=env, timeout=30)
    if "ELF32" not in header or not re.search(r"Machine:\s+ARM(?:\s|$)", header):
        raise WorkflowError("artifact is not a 32-bit ARM ELF")
    _, symbols, _ = log.run([tools["nm"], "-P", "--defined-only", elf], name="elf-symbols", env=env, timeout=30)
    required = ["R3_W3_HW_Start", "g_r3_w3_hw_result", "AdcDbmDriver_Arm", "R3Lifecycle_PrepareStart"]
    if selector in ("START_A", "START_C"):
        required.append("R3Lifecycle_CommitStart")
    if selector == "T06_B":
        required.append("R3Lifecycle_RequestStopBeforeCommit")
    for symbol in required:
        if not re.search(r"^" + re.escape(symbol) + r"\s", symbols, re.M):
            raise WorkflowError(f"required W3 target symbol missing: {symbol}")
    binary = build / "programmed.bin"
    log.run([tools["objcopy"], "-O", "binary", elf, binary], name="programmed-binary", env=env, timeout=30)
    return {"elf_sha256": digest(elf), "programmed_sha256": digest(binary)}


def attempt(repo: Path, case: str, output_parent: Path) -> int:
    if case not in CASE_TO_SELECTOR:
        raise WorkflowError(f"not a W3 hardware case: {case}")
    if status(repo):
        raise WorkflowError("formal attempt requires a clean worktree")
    if inspect_seal(repo).relation != SealRelation.CLEAN_SYNCED:
        raise WorkflowError("formal attempt requires HEAD pushed to origin/main")
    validate_host_timeout(TARGET_BOUND_S, HOST_TIMEOUT_S)
    output_parent = output_parent.expanduser().resolve()
    if output_parent.is_relative_to(repo.resolve()):
        raise WorkflowError("formal attempt output must be outside repository")
    output_parent.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix=f"{case.lower()}-", dir=output_parent))
    raw, log, completed = root / "raw", CommandLog(root), []
    try:
        tools, env, tool_identity = select_tools(log)
        programmer = Path(r"E:\DevTools\STM32CubeCLT-1.22.0\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe")
        if not programmer.is_file():
            raise WorkflowError("STM32CubeProgrammer CLI unavailable")
        log.run([programmer, "-l"], name="probe-list", timeout=30)
        write_new(root / "H0.json", {"phase": "H0", "git_head": head(repo), "case": case,
            "toolchain": tool_identity, "target_bound_s": TARGET_BOUND_S,
            "host_timeout_s": HOST_TIMEOUT_S, "single_execution": True})
        completed.append("H0")
        build = root / "build"
        chosen = definitions(CASE_TO_SELECTOR[case])
        configure(log, repo, build, chosen, tools, env)
        verify_cache(build, chosen)
        log.run([tools["cmake"], "--build", build, "--parallel", "4"], name="build", env=env, timeout=600)
        audit = audit_elf(repo, build, CASE_TO_SELECTOR[case], tools, env, log)
        elf = build / "cubemx.elf"
        write_new(root / "H1.json", {"phase": "H1", "audit": audit, "elf_sha256": digest(elf)})
        completed.append("H1")
        log.run([programmer, "-c", "port=SWD", "mode=UR", "-w", elf, "-v", "-rst"], name="flash-verify", timeout=120)
        write_new(root / "H2.json", {"phase": "H2", "programmed_image_sha256": audit["programmed_sha256"], "verified": True})
        completed.append("H2")
        time.sleep(TARGET_BOUND_S)
        write_new(root / "H3.json", {"phase": "H3", "single_execution": True, "result": "COMPLETE"})
        completed.append("H3")
        address = result_address(elf, tools["nm"], env, log)
        _, readout, _ = log.run([programmer, "-c", "port=SWD", "mode=UR", "-r32", f"0x{address:08X}", str(RESULT_WORDS * 4)], name="read-result", timeout=60)
        raw.mkdir(parents=True, exist_ok=True)
        (raw / "target-result.txt").write_text(readout, encoding="utf-8", newline="\n")
        checked = evaluate(case, parse_words(readout))
        write_new(root / "H4.json", {"phase": "H4", "result_address": f"0x{address:08X}", **checked})
        completed.append("H4")
        write_new(root / "identity.json", {"schema_version": SCHEMA_VERSION, "git_head": head(repo),
            "firmware_source_commit": head(repo), "elf_sha256": digest(elf), "programmed_image_sha256": audit["programmed_sha256"],
            "harness_version": __version__, "harness_sha256": digest(repo / "tools/r3/r3_harness.py"), "board_profile": "NUCLEO-F446RE"})
        write_new(root / "config.json", {"work_package": "R3-W3", "case_id": case, "selector": CASE_TO_SELECTOR[case]})
        write_new(root / "state.json", {"work_package": "R3-W3", "case_id": case, "attempt": 1,
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
    acceptance = json.loads((attempt_root / "acceptance.json").read_text(encoding="utf-8"))
    config = json.loads((attempt_root / "config.json").read_text(encoding="utf-8"))
    if acceptance.get("result") != "PASS" or config.get("work_package") != "R3-W3":
        raise WorkflowError("only completed PASS R3-W3 attempts may be imported")
    if config.get("case_id") not in CASE_TO_SELECTOR:
        raise WorkflowError("unrecognized R3-W3 evidence case")
    if any(not (attempt_root / f"{phase}.json").is_file() for phase in PHASES):
        raise WorkflowError("completed H0-H5 records are required")
    validate_manifest(attempt_root)
    case_dir = config["case_id"].lower()
    dest = repo / "docs/evidence/r3/w3" / case_dir / "attempt-0001"
    if dest.exists():
        raise WorkflowError(f"immutable evidence destination exists: {dest}")
    shutil.copytree(attempt_root, dest)
    validate_manifest(dest)
    return dest
