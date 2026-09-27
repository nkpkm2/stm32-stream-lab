"""Formal R3-W6 500-record board soak workflow."""
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

ANCHORS = {"W6-A": ("A", 1, 8), "W6-B": ("B", 2, 1)}
MAGIC, COMPLETE, CYCLES, HEADER, CYCLE_WORDS = 0x52335736, 0xA66C0DE6, 500, 8, 12
WORDS = HEADER + CYCLES * CYCLE_WORDS
TARGET_BOUND_S, HOST_TIMEOUT_S = 20.0, 45.0


class WorkflowError(RuntimeError): pass


def definitions(anchor: str) -> dict[str, str]:
    return {"STREAM_LAB_FOUNDATION_ADC_DBM_DRIVER":"ON", "STREAM_LAB_FOUNDATION_OWNERSHIP_CORE":"ON", "STREAM_LAB_FOUNDATION_TOKEN_LEDGER":"ON", "STREAM_LAB_FOUNDATION_QUEUE_ADAPTER":"ON", "STREAM_LAB_R3_WORKER_CONTRACT":"ON", "STREAM_LAB_R3_WORKER_TASKS":"ON", "STREAM_LAB_R3_LIFECYCLE":"ON", "STREAM_LAB_R3_W2_HW":"OFF", "STREAM_LAB_R3_W2_HW_CASE":"", "STREAM_LAB_R3_W3_HW":"OFF", "STREAM_LAB_R3_W3_HW_CASE":"", "STREAM_LAB_R3_W4_HW":"OFF", "STREAM_LAB_R3_W4_HW_CASE":"", "STREAM_LAB_R3_W5_HW":"OFF", "STREAM_LAB_R3_W5_HW_CASE":"", "STREAM_LAB_R3_W6_HW":"ON", "STREAM_LAB_R3_W6_HW_ANCHOR":anchor}


def parse_words(text: str) -> list[int]:
    values = []
    for line in text.splitlines():
        if re.match(r"^0x[0-9A-Fa-f]{8}\s*:", line):
            values += [int(x, 16) for x in line.split(":", 1)[1].split() if re.fullmatch(r"[0-9A-Fa-f]{8}", x)]
    if len(values) != WORDS: raise WorkflowError(f"target result has {len(values)} words, expected {WORDS}")
    return values


def evaluate(case: str, words: list[int]) -> dict:
    anchor, anchor_id, k = ANCHORS[case]
    checks = {"magic":words[0] == MAGIC, "schema":words[1] == 1, "anchor":words[2] == anchor_id, "terminal":words[3] == 1, "all_cycles":words[4] == CYCLES, "no_fault_cycles":words[5] == 0, "complete":words[7] == COMPLETE}
    records = []
    for n in range(CYCLES):
        row = words[HEADER + n*CYCLE_WORDS:HEADER + (n+1)*CYCLE_WORDS]
        valid = row[0] == n+1 and row[1] == n+1 and row[2] == n+1 and row[3] == 0x60000000+n+1 and row[4] == 0 and row[5] == 0 and row[6] == 0 and row[7] == 0 and row[8] == 0x300 and row[9] == 0 and row[10] == 0 and row[11] == 0
        records.append({"cycle":n+1, "valid":valid, "words":[f"0x{x:08X}" for x in row]})
    checks["all_records_valid"] = all(x["valid"] for x in records)
    if anchor == "B": checks["controlled_drop_observed"] = words[6] > 0
    return {"result":"PASS" if all(checks.values()) else "FAIL", "checks":checks, "records":records}


def _address(elf: Path, nm: Path, env: dict, log: CommandLog) -> int:
    _, out, _ = log.run([nm,"-n",elf], name="result-symbol", env=env, timeout=30)
    m = re.findall(r"^\s*([0-9a-fA-F]+)\s+[A-Za-z]\s+g_r3_w6_hw_result\s*$", out, re.M)
    if len(m) != 1: raise WorkflowError("expected one W6 result symbol")
    return int(m[0],16)


def attempt(repo: Path, case: str, output_parent: Path) -> int:
    if case not in ANCHORS: raise WorkflowError("W6 case must be W6-A or W6-B")
    if status(repo) or inspect_seal(repo).relation != SealRelation.CLEAN_SYNCED: raise WorkflowError("formal attempt requires clean, pushed HEAD")
    validate_host_timeout(TARGET_BOUND_S, HOST_TIMEOUT_S); output_parent = output_parent.expanduser().resolve()
    root = Path(tempfile.mkdtemp(prefix=f"{case.lower()}-", dir=output_parent)); completed=[]
    try:
        tools, env, ident = select_tools(CommandLog(root)); log=CommandLog(root); programmer=Path(r"E:\DevTools\STM32CubeCLT-1.22.0\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe")
        log.run([programmer,"-l"], name="probe-list", timeout=30); write_new(root/"H0.json", {"phase":"H0","case":case,"git_head":head(repo),"toolchain":ident,"target_bound_s":TARGET_BOUND_S,"host_timeout_s":HOST_TIMEOUT_S,"single_execution":True}); completed.append("H0")
        build=root/"build"; anchor,_,_=ANCHORS[case]; chosen=definitions(anchor); configure(log,repo,build,chosen,tools,env); verify_cache(build,chosen); log.run([tools["cmake"],"--build",build,"--parallel","4"],name="build",env=env,timeout=600)
        elf=build/"cubemx.elf"; binary=build/"programmed.bin"; log.run([tools["objcopy"],"-O","binary",elf,binary],name="programmed-binary",env=env,timeout=30); write_new(root/"H1.json", {"phase":"H1","elf_sha256":digest(elf),"programmed_sha256":digest(binary)}); completed.append("H1")
        log.run([programmer,"-c","port=SWD","mode=UR","-w",elf,"-v","-rst"],name="flash-verify",timeout=120); write_new(root/"H2.json", {"phase":"H2","verified":True}); completed.append("H2")
        time.sleep(TARGET_BOUND_S); write_new(root/"H3.json", {"phase":"H3","single_execution":True,"result":"COMPLETE"}); completed.append("H3")
        address=_address(elf,tools["nm"],env,log); _,raw,_=log.run([programmer,"-c","port=SWD","mode=UR","-r32",f"0x{address:08X}",str(WORDS*4)],name="read-result",timeout=90); (root/"raw").mkdir(); (root/"raw"/"target-result.txt").write_text(raw,encoding="utf-8",newline="\n")
        checked=evaluate(case,parse_words(raw)); write_new(root/"H4.json", {"phase":"H4","result_address":f"0x{address:08X}",**checked}); completed.append("H4")
        write_new(root/"identity.json", {"schema_version":SCHEMA_VERSION,"git_head":head(repo),"elf_sha256":digest(elf),"harness_version":__version__,"harness_sha256":digest(repo/"tools/r3/r3_harness.py"),"board_profile":"NUCLEO-F446RE"}); write_new(root/"config.json", {"work_package":"R3-W6","case_id":case,"anchor":anchor}); write_new(root/"state.json", {"work_package":"R3-W6","case_id":case,"attempt":1,"completed_phases":completed+["H5"],"next_allowed":"COMPLETE"}); write_new(root/"acceptance.json", {"result":checked["result"],"invariants":checked["checks"]}); write_new(root/"H5.json", {"phase":"H5","result":checked["result"],"sealed":True}); manifest(root)
        print(f"ATTEMPT: {root}\nRESULT: {checked['result']}"); return 0 if checked["result"] == "PASS" else 2
    except Exception as exc:
        write_new(root/"failure.json", {"error":str(exc),"completed_phases":completed}); manifest(root); print(f"ATTEMPT: {root}\nRESULT: FAIL: {exc}"); return 2


def import_attempt(repo: Path, root: Path) -> Path:
    a=json.loads((root/"acceptance.json").read_text()); c=json.loads((root/"config.json").read_text())
    if a.get("result") != "PASS" or c.get("case_id") not in ANCHORS: raise WorkflowError("only completed PASS W6 attempts may be imported")
    validate_manifest(root); dest=repo/"docs/evidence/r3/w6"/c["case_id"].lower()/"attempt-0001"
    if dest.exists(): raise WorkflowError("immutable evidence destination exists")
    shutil.copytree(root,dest); validate_manifest(dest); return dest
