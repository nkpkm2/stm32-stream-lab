#!/usr/bin/env python3
"""CT-W5 H3 adverse-burst runtime runner.

Allowed side effects:
- open ST-LINK VCP
- explicit target reset
- send exactly one 760-byte adverse burst via the already-sealed host tool
- collect host/reset evidence

Forbidden in this step:
- flash
- debugger/GDB
- RAM inspection
- Git modification
- second burst/retry
"""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path

from serial.tools import list_ports


REPO = Path(r"E:\Projects\stm32-stream-lab")
BUILD = REPO / r"build\r2-ct-w5-k1-drop-800-t1500-b3aa0b92"

CHECKPOINT = "b3aa0b926a49182e99095241a7939195073aa545"

EXPECTED_ELF = (
    "7736852DFAE9254055ADA8D1EF8DDE36AF09B478BB658AF9526D7143C51367C3"
)
EXPECTED_BIN = (
    "754AB433AE4E36EDA317F0F75AC148CC4C6D01AA867E5EB960D69EB5693F39E2"
)
EXPECTED_HOST = (
    "C70A794F61980DF6DBB0F4E6B61775C22E3AE51D37B2D32EB50BEE38717B143B"
)

PYTHON = Path(
    r"C:\Users\CHJ\AppData\Local\Programs\Python\Python310\python.exe"
)
PROGRAMMER = Path(
    r"E:\DevTools\STM32CubeProgrammer-2.23.0\bin\STM32_Programmer_CLI.exe"
)

ELF = BUILD / "cubemx.elf"
BIN = BUILD / "cubemx.bin"
GATE = BUILD / "ct-w5-build-gate.json"
HOST_TOOL = REPO / r"tools\r2\r2_ct_burst.py"

H2_STDOUT = BUILD / "hardware-attempt01-flash-verify.stdout.txt"
H2_STDERR = BUILD / "hardware-attempt01-flash-verify.stderr.txt"

HOST_STDOUT = BUILD / "hardware-attempt01-burst.stdout.txt"
HOST_STDERR = BUILD / "hardware-attempt01-burst.stderr.txt"
RESET_STDOUT = BUILD / "hardware-attempt01-reset.stdout.txt"
RESET_STDERR = BUILD / "hardware-attempt01-reset.stderr.txt"
SUMMARY = BUILD / "hardware-attempt01-h3-runtime.json"

BASE_REQUEST_ID = 0x0500


def run(
    args: list[str],
    *,
    cwd: Path | None = None,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        args,
        cwd=str(cwd) if cwd else None,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        errors="replace",
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {args!r}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )
    return result


def git(*args: str) -> str:
    return run(
        ["git", "-C", str(REPO), *args]
    ).stdout.strip()


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def require_file(path: Path) -> None:
    if not path.is_file():
        raise RuntimeError(f"required file missing: {path}")


def select_stlink_vcp() -> str:
    matches: list[str] = []

    for port in list_ports.comports():
        description = (port.description or "").lower()
        hwid = (port.hwid or "").lower()

        if (
            port.vid == 0x0483
            or "stlink" in description
            or "st-link" in description
            or "stmicroelectronics" in description
            or "vid:pid=0483" in hwid
        ):
            matches.append(port.device)

    if len(matches) != 1:
        raise RuntimeError(
            f"expected exactly one ST-LINK VCP, found {matches}"
        )

    return matches[0]


def main() -> int:
    print("=== CT-W5 / H3 K1-DROP ADVERSE BURST ===")
    print("Flash: NO")
    print("Debugger/RAM inspection: NO")
    print("Second burst/retry: NO")

    # ------------------------------------------------------------
    # 1. Provenance and H2 anchor
    # ------------------------------------------------------------

    if git("rev-parse", "HEAD") != CHECKPOINT:
        raise RuntimeError("HEAD is not the CT-W5 checkpoint")

    if git("rev-parse", "origin/main") != CHECKPOINT:
        raise RuntimeError("origin/main is not the CT-W5 checkpoint")

    if git("status", "--porcelain=v1", "-uall"):
        raise RuntimeError("working tree is not clean")

    if git("tag", "--list", "r2-pass"):
        raise RuntimeError("r2-pass exists unexpectedly")

    for path in (
        PYTHON,
        PROGRAMMER,
        ELF,
        BIN,
        GATE,
        HOST_TOOL,
        H2_STDOUT,
        H2_STDERR,
    ):
        require_file(path)

    if sha256(ELF) != EXPECTED_ELF:
        raise RuntimeError("CT-W5 ELF identity mismatch")

    if sha256(BIN) != EXPECTED_BIN:
        raise RuntimeError("CT-W5 programmed-image identity mismatch")

    if sha256(HOST_TOOL) != EXPECTED_HOST:
        raise RuntimeError("sealed burst host identity mismatch")

    gate = json.loads(
        GATE.read_text(encoding="utf-8")
    )

    if gate.get("result") != "PASS":
        raise RuntimeError("CT-W5 build gate does not report PASS")

    profile = gate.get("profile", {})
    expected_profile = {
        "k": 1,
        "mode": "DROP",
        "events": 800,
        "run_timeout_ms": 1500,
        "process_hold_blocks": 6,
    }

    for key, expected in expected_profile.items():
        if profile.get(key) != expected:
            raise RuntimeError(
                f"build-gate profile mismatch: "
                f"{key}={profile.get(key)!r}, expected {expected!r}"
            )

    h2_text = (
        H2_STDOUT.read_text(
            encoding="utf-8",
            errors="replace",
        )
        + "\n"
        + H2_STDERR.read_text(
            encoding="utf-8",
            errors="replace",
        )
    )

    if "Download verified successfully" not in h2_text:
        raise RuntimeError("H2 verify-success marker missing")

    print("Checkpoint/profile/artifact/H2 anchor: PASS")

    # ------------------------------------------------------------
    # 2. Immutable evidence guard
    # ------------------------------------------------------------

    for path in (
        HOST_STDOUT,
        HOST_STDERR,
        RESET_STDOUT,
        RESET_STDERR,
        SUMMARY,
    ):
        if path.exists():
            raise RuntimeError(
                f"refusing to overwrite H3 evidence: {path}"
            )

    # ------------------------------------------------------------
    # 3. Start listener first
    # ------------------------------------------------------------

    port = select_stlink_vcp()
    print(f"Selected VCP: {port}")

    host_args = [
        str(PYTHON),
        str(HOST_TOOL),
        "--port",
        port,
        "--baud",
        "115200",
        "--base-request-id",
        hex(BASE_REQUEST_ID),
        "--delay-ms",
        "145",
        "--banner-timeout",
        "8",
        "--all-replies-timeout",
        "2",
        "--expect-k",
        "1",
        "--expect-mode",
        "DROP",
    ]

    print("Starting sealed adverse-burst host listener...")

    host = subprocess.Popen(
        host_args,
        cwd=str(REPO),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        errors="replace",
    )

    time.sleep(0.8)

    if host.poll() is not None:
        stdout, stderr = host.communicate()
        HOST_STDOUT.write_text(
            stdout or "",
            encoding="utf-8",
            newline="\n",
        )
        HOST_STDERR.write_text(
            stderr or "",
            encoding="utf-8",
            newline="\n",
        )
        raise RuntimeError(
            f"host exited before reset: rc={host.returncode}"
        )

    print("Host listener: READY")

    # ------------------------------------------------------------
    # 4. Explicit reset only; no flash
    # ------------------------------------------------------------

    print("Issuing explicit reset...")

    reset = subprocess.run(
        [
            str(PROGRAMMER),
            "-c",
            "port=SWD",
            "-rst",
        ],
        cwd=str(REPO),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        errors="replace",
        timeout=15,
        check=False,
    )

    RESET_STDOUT.write_text(
        reset.stdout or "",
        encoding="utf-8",
        newline="\n",
    )
    RESET_STDERR.write_text(
        reset.stderr or "",
        encoding="utf-8",
        newline="\n",
    )

    if reset.returncode != 0:
        host.terminate()
        try:
            host.wait(timeout=3)
        except subprocess.TimeoutExpired:
            host.kill()
            host.wait(timeout=3)

        raise RuntimeError(
            f"reset failed: rc={reset.returncode}"
        )

    reset_text = (
        (reset.stdout or "")
        + "\n"
        + (reset.stderr or "")
    )

    if "Software reset is performed" not in reset_text:
        host.terminate()
        try:
            host.wait(timeout=3)
        except subprocess.TimeoutExpired:
            host.kill()
            host.wait(timeout=3)

        raise RuntimeError(
            "reset returned 0 but reset-success marker is absent"
        )

    print("Explicit reset: PASS")

    # ------------------------------------------------------------
    # 5. Allow the sealed host tool to perform exactly one burst
    # ------------------------------------------------------------

    try:
        stdout, stderr = host.communicate(timeout=15)
    except subprocess.TimeoutExpired:
        host.kill()
        stdout, stderr = host.communicate()

        HOST_STDOUT.write_text(
            stdout or "",
            encoding="utf-8",
            newline="\n",
        )
        HOST_STDERR.write_text(
            stderr or "",
            encoding="utf-8",
            newline="\n",
        )

        raise RuntimeError("burst host timed out")

    HOST_STDOUT.write_text(
        stdout or "",
        encoding="utf-8",
        newline="\n",
    )
    HOST_STDERR.write_text(
        stderr or "",
        encoding="utf-8",
        newline="\n",
    )

    if host.returncode != 0:
        raise RuntimeError(
            f"burst host failed: rc={host.returncode}\n"
            f"stderr={stderr}"
        )

    if (stderr or "").strip():
        raise RuntimeError(
            f"burst host stderr is non-empty: {stderr}"
        )

    try:
        result = json.loads((stdout or "").strip())
    except json.JSONDecodeError as exc:
        raise RuntimeError(
            f"burst host stdout is not valid JSON: {exc}\n"
            f"{stdout}"
        ) from exc

    # ------------------------------------------------------------
    # 6. H3-only host acceptance
    # ------------------------------------------------------------

    if result.get("result") != "PASS":
        raise RuntimeError("burst host JSON does not report PASS")

    if result.get("profile") != "CT-W4_ADVERSE_BURST":
        raise RuntimeError(
            "sealed burst host returned unexpected profile: "
            f"{result.get('profile')!r}"
        )

    expected_shape = {
        "request_count": 10,
        "payload_bytes_each": 64,
        "request_frame_bytes_each": 76,
        "burst_bytes": 760,
        "single_write_call": True,
        "intentional_inter_request_delay_ms": 0,
        "wait_for_reply_before_burst_complete": False,
    }

    for key, expected in expected_shape.items():
        if result.get(key) != expected:
            raise RuntimeError(
                f"burst shape mismatch: "
                f"{key}={result.get(key)!r}, expected {expected!r}"
            )

    records = result.get("records")
    if not isinstance(records, list) or len(records) != 10:
        raise RuntimeError("expected exactly 10 reply records")

    expected_ids = list(
        range(BASE_REQUEST_ID, BASE_REQUEST_ID + 10)
    )
    actual_ids = [
        int(record["request_id"])
        for record in records
    ]

    if actual_ids != expected_ids:
        raise RuntimeError(
            f"reply ID sequence mismatch: {actual_ids}"
        )

    inputs = [
        int(record["target_w6_input_at_process"])
        for record in records
    ]

    if any(
        later <= earlier
        for earlier, later in zip(inputs, inputs[1:])
    ):
        raise RuntimeError(
            f"W6 input observations are not strictly increasing: {inputs}"
        )

    for index, record in enumerate(records):
        if int(record["target_phase"]) != 3:
            raise RuntimeError(
                f"reply {index} was not processed while W6 RUNNING"
            )

        if int(record["target_k"]) != 1:
            raise RuntimeError(
                f"reply {index} target K != 1"
            )

        if int(record["target_drop_mode"]) != 1:
            raise RuntimeError(
                f"reply {index} target mode != DROP"
            )

    summary = {
        "result": "PASS",
        "attempt": 1,
        "scope": "CT-W5 H3 K1/DROP adverse burst runtime",
        "checkpoint": CHECKPOINT,
        "profile": {
            "k": 1,
            "mode": "DROP",
            "events": 800,
            "run_timeout_ms": 1500,
            "process_hold_blocks": 6,
        },
        "request_id_first": expected_ids[0],
        "request_id_last": expected_ids[-1],
        "request_count": 10,
        "payload_bytes_each": 64,
        "request_frame_bytes_each": 76,
        "burst_bytes": 760,
        "single_write_call": True,
        "intentional_inter_request_delay_ms": 0,
        "wait_for_reply_before_burst_complete": False,
        "host_burst_submit_ms": result["host_burst_submit_ms"],
        "host_all_replies_ms": result["host_all_replies_ms"],
        "first_w6_input": inputs[0],
        "last_w6_input": inputs[-1],
        "host_returncode": host.returncode,
        "debugger_attached": False,
        "flash_performed": False,
        "ram_inspection_performed": False,
        "records": records,
    }

    SUMMARY.write_text(
        json.dumps(
            summary,
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
        newline="\n",
    )

    print()
    print("=== CT-W5 H3 RESULT ===")
    print("K1/DROP adverse burst: PASS")
    print("Requests:               10 / 10 replies PASS")
    print("Payload:                64 B each")
    print("Burst:                  one 760 B serial write")
    print("Intentional gap:        0 ms")
    print(
        f"Host burst submit:      "
        f"{float(result['host_burst_submit_ms']):.3f} ms"
    )
    print(
        f"All replies complete:   "
        f"{float(result['host_all_replies_ms']):.3f} ms"
    )
    print(
        f"W6 input range:         "
        f"{inputs[0]} -> {inputs[-1]}"
    )
    print("Target profile:         K1 / DROP")
    print("Debugger:               NOT ATTACHED")
    print("Flash during H3:        NOT PERFORMED")
    print("RAM inspection:         NOT PERFORMED")
    print(f"Evidence:               {SUMMARY}")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print()
        print("=== CT-W5 H3 RESULT ===")
        print("K1/DROP adverse burst: FAIL")
        print(str(exc))
        print("NO automatic retry was performed.")
        raise SystemExit(1)
