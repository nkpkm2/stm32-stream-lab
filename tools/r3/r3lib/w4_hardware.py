"""Formal single-execution R3-W4 target evidence workflow (initial cases)."""
from __future__ import annotations

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
from .w3_hardware import digest, manifest, validate_manifest, write_new

CASES = {"W4-T04-A": ("T04_A", 1), "W4-T04-B": ("T04_B", 5), "W4-STOP-A": ("STOP_A", 2),
         "W4-STOP-B": ("STOP_B", 3), "W4-STOP-C": ("STOP_C", 6), "W4-STOP-D": ("STOP_D", 4)}
MAGIC, COMPLETE, WORDS = 0x52335734, 0xA44C0DE4, 29
TARGET_BOUND_S, HOST_TIMEOUT_S = 0.5, 3.0


class WorkflowError(RuntimeError):
    pass


def definitions(selector: str) -> dict[str, str]:
    return {
        "STREAM_LAB_FOUNDATION_ADC_DBM_DRIVER": "ON",
        "STREAM_LAB_FOUNDATION_OWNERSHIP_CORE": "ON",
        "STREAM_LAB_FOUNDATION_TOKEN_LEDGER": "ON",
        "STREAM_LAB_FOUNDATION_QUEUE_ADAPTER": "ON",
        "STREAM_LAB_R3_WORKER_CONTRACT": "ON",
        "STREAM_LAB_R3_WORKER_TASKS": "ON",
        "STREAM_LAB_R3_LIFECYCLE": "ON",
        "STREAM_LAB_R3_W2_HW": "OFF", "STREAM_LAB_R3_W2_HW_CASE": "",
        "STREAM_LAB_R3_W3_HW": "OFF", "STREAM_LAB_R3_W3_HW_CASE": "",
        "STREAM_LAB_R3_W4_HW": "ON", "STREAM_LAB_R3_W4_HW_CASE": selector,
    }


def parse_words(text: str) -> list[int]:
    words: list[int] = []
    for line in text.splitlines():
        if re.match(r"^0x[0-9A-Fa-f]{8}\s*:", line):
            for value in line.split(":", 1)[1].split():
                if not re.fullmatch(r"[0-9A-Fa-f]{8}", value):
                    raise WorkflowError(f"malformed target word: {value!r}")
                words.append(int(value, 16))
    if len(words) != WORDS:
        raise WorkflowError(f"target result has {len(words)} words, expected {WORDS}")
    return words


def result_address(elf: Path, nm: Path, env: dict, log: CommandLog) -> int:
    _, listing, _ = log.run([nm, "-n", elf], name="result-symbol", env=env, timeout=30)
    rows = re.findall(r"^\s*([0-9a-fA-F]+)\s+[A-Za-z]\s+g_r3_w4_hw_result\s*$", listing, re.M)
    if len(rows) != 1:
        raise WorkflowError("expected one g_r3_w4_hw_result symbol")
    return int(rows[0], 16)


def evaluate(case: str, words: list[int]) -> dict:
    selector, case_id = CASES[case]
    checks = {
        "result_magic": words[0] == MAGIC, "schema": words[1] == 1,
        "case_identity": words[2] == case_id, "terminal_pass": words[3] == 1,
        "no_invariant": words[4] == 0, "idle": words[5] == 0,
        "gates_closed": words[6:9] == [0, 0, 0],
        "hardware_released": words[10] == 0,
        "begin_report": words[12] == 1, "begin_timer_closed": (words[15] & 1) == 0,
        "finish_report": words[16] == 1, "dma_disabled": (words[17] & 1) == 0,
        "worker_acks": words[18] == 0x300, "worker_clean": words[22:25] == [0, 0, 0],
        "completion_magic": words[28] == COMPLETE,
    }
    if selector == "T04_A":
        checks.update({"partial_only": 0 < words[13] < 256,
                       "no_completion": words[11] == 0,
                       "no_ready_cancel": words[21] == 0})
    elif selector == "T04_B":
        checks.update({"pending_tc_at_stop": (words[14] & 0x20) != 0,
                       "no_old_completion": words[11] == 0,
                       "no_ready_cancel": words[21] == 0})
    elif selector == "STOP_A":
        checks["ready_cancelled_by_processing"] = words[21] > 0
    elif selector == "STOP_C":
        checks.update({"rebind_before_drop": words[27] > 0,
                       "controlled_drop": words[26] > 0,
                       "processing_reconciled_ready": words[21] > 0})
    else:
        if selector == "STOP_B":
            checks.update({"processing_entered": words[19] == 1,
                           "current_completed": words[20] > 0})
        else:
            checks["duplicate_stop_same_transaction"] = words[25] == 1
    return {"result": "PASS" if all(checks.values()) else "FAIL", "checks": checks,
            "words": [f"0x{word:08X}" for word in words]}


def audit_elf(build: Path, selector: str, tools: dict, env: dict, log: CommandLog) -> dict:
    elf = build / "cubemx.elf"
    _, header, _ = log.run([tools["readelf"], "-h", elf], name="elf-header", env=env, timeout=30)
    _, symbols, _ = log.run([tools["nm"], "-P", "--defined-only", elf], name="elf-symbols", env=env, timeout=30)
    if "ELF32" not in header or "Machine:" not in header:
        raise WorkflowError("artifact is not a 32-bit ARM ELF")
    for symbol in ("R3_W4_HW_Start", "g_r3_w4_hw_result", "AdcDbmDriver_BeginStop", "AdcDbmDriver_FinishStop"):
        if not re.search(r"^" + re.escape(symbol) + r"\s", symbols, re.M):
            raise WorkflowError(f"required W4 target symbol missing: {symbol}")
    binary = build / "programmed.bin"
    log.run([tools["objcopy"], "-O", "binary", elf, binary], name="programmed-binary", env=env, timeout=30)
    return {"elf_sha256": digest(elf), "programmed_sha256": digest(binary)}


def attempt(repo: Path, case: str, output_parent: Path) -> int:
    if case not in CASES: raise WorkflowError(f"not an implemented W4 hardware case: {case}")
    if status(repo): raise WorkflowError("formal attempt requires a clean worktree")
    if inspect_seal(repo).relation != SealRelation.CLEAN_SYNCED: raise WorkflowError("formal attempt requires HEAD pushed to origin/main")
    validate_host_timeout(TARGET_BOUND_S, HOST_TIMEOUT_S)
    output_parent = output_parent.expanduser().resolve()
    if output_parent.is_relative_to(repo.resolve()): raise WorkflowError("formal attempt output must be outside repository")
    root = Path(tempfile.mkdtemp(prefix=f"{case.lower()}-", dir=output_parent)); completed: list[str] = []
    try:
        tools, env, identity = select_tools(CommandLog(root)); log = CommandLog(root)
        programmer = Path(r"E:\DevTools\STM32CubeCLT-1.22.0\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe")
        if not programmer.is_file(): raise WorkflowError("STM32CubeProgrammer CLI unavailable")
        log.run([programmer, "-l"], name="probe-list", timeout=30)
        write_new(root / "H0.json", {"phase":"H0","git_head":head(repo),"case":case,"toolchain":identity,"target_bound_s":TARGET_BOUND_S,"host_timeout_s":HOST_TIMEOUT_S,"single_execution":True}); completed.append("H0")
        build = root / "build"; selector, _ = CASES[case]; chosen = definitions(selector)
        configure(log, repo, build, chosen, tools, env); verify_cache(build, chosen)
        log.run([tools["cmake"], "--build", build, "--parallel", "4"], name="build", env=env, timeout=600)
        audit = audit_elf(build, selector, tools, env, log); elf = build / "cubemx.elf"
        write_new(root / "H1.json", {"phase":"H1","audit":audit,"elf_sha256":digest(elf)}); completed.append("H1")
        log.run([programmer,"-c","port=SWD","mode=UR","-w",elf,"-v","-rst"], name="flash-verify", timeout=120)
        write_new(root / "H2.json", {"phase":"H2","programmed_image_sha256":audit["programmed_sha256"],"verified":True}); completed.append("H2")
        time.sleep(TARGET_BOUND_S); write_new(root / "H3.json", {"phase":"H3","single_execution":True,"result":"COMPLETE"}); completed.append("H3")
        address = result_address(elf, tools["nm"], env, log)
        _, readout, _ = log.run([programmer,"-c","port=SWD","mode=UR","-r32",f"0x{address:08X}",str(WORDS*4)], name="read-result", timeout=60)
        (root / "raw").mkdir(); (root / "raw" / "target-result.txt").write_text(readout, encoding="utf-8", newline="\n")
        checked = evaluate(case, parse_words(readout)); write_new(root / "H4.json", {"phase":"H4","result_address":f"0x{address:08X}",**checked}); completed.append("H4")
        write_new(root / "identity.json", {"schema_version":SCHEMA_VERSION,"git_head":head(repo),"firmware_source_commit":head(repo),"elf_sha256":digest(elf),"programmed_image_sha256":audit["programmed_sha256"],"harness_version":__version__,"harness_sha256":digest(repo/"tools/r3/r3_harness.py"),"board_profile":"NUCLEO-F446RE"})
        write_new(root / "config.json", {"work_package":"R3-W4","case_id":case,"selector":selector})
        write_new(root / "state.json", {"work_package":"R3-W4","case_id":case,"attempt":1,"completed_phases":completed+["H5"],"next_allowed":"COMPLETE","hardware_state":"SAFE"})
        write_new(root / "acceptance.json", {"result":checked["result"],"first_failure_class":None if checked["result"]=="PASS" else "TARGET_FIRMWARE","invariants":checked["checks"]})
        write_new(root / "H5.json", {"phase":"H5","result":checked["result"],"sealed":True}); manifest(root)
        print(f"ATTEMPT: {root}\nRESULT: {checked['result']}"); return 0 if checked["result"] == "PASS" else 2
    except Exception as exc:
        write_new(root / "failure.json", {"error":str(exc),"completed_phases":completed}); manifest(root); print(f"ATTEMPT: {root}\nRESULT: FAIL: {exc}"); return 2


def import_attempt(repo: Path, attempt_root: Path) -> Path:
    acceptance = json.loads((attempt_root / "acceptance.json").read_text(encoding="utf-8")); config = json.loads((attempt_root / "config.json").read_text(encoding="utf-8"))
    if acceptance.get("result") != "PASS" or config.get("work_package") != "R3-W4" or config.get("case_id") not in CASES: raise WorkflowError("only completed PASS implemented R3-W4 attempts may be imported")
    if any(not (attempt_root / f"{p}.json").is_file() for p in PHASES): raise WorkflowError("completed H0-H5 records are required")
    validate_manifest(attempt_root); dest = repo / "docs/evidence/r3/w4" / config["case_id"].lower() / "attempt-0001"
    if dest.exists(): raise WorkflowError(f"immutable evidence destination exists: {dest}")
    shutil.copytree(attempt_root, dest); validate_manifest(dest); return dest
