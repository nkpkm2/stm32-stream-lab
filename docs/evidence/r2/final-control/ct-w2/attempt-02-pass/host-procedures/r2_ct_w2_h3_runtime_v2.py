#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path

from serial.tools import list_ports

REPO = Path(r"E:\Projects\stm32-stream-lab")
BUILD = Path(r"E:\Projects\stm32-stream-lab\build\r2-ct-w2-k8-normal-6b43bec8")

PYTHON = Path(r"C:\Users\CHJ\AppData\Local\Programs\Python\Python310\python.exe")
PROGRAMMER = Path(r"E:\DevTools\STM32CubeProgrammer-2.23.0\bin\STM32_Programmer_CLI.exe")
HOST = REPO / r"tools\r2\ct_ping_smoke.py"

EXPECTED_HOST_SHA256 = "5D1AFBDCEBAB8C30B3AF369EE56AA7880BD6FFFE679EC4F9F426A6CB5903E187"

STDOUT_PATH = BUILD / "hardware-attempt02-ping.stdout.txt"
STDERR_PATH = BUILD / "hardware-attempt02-ping.stderr.txt"
RESET_STDOUT_PATH = BUILD / "hardware-attempt02-reset.stdout.txt"
RESET_STDERR_PATH = BUILD / "hardware-attempt02-reset.stderr.txt"
SUMMARY_PATH = BUILD / "hardware-attempt02-h3-runtime.json"

REQUEST_ID = 0x0201
BAUD = 115200
DELAY_MS = 145
BANNER_TIMEOUT = 8.0
REPLY_TIMEOUT = 1.0
OVERALL_TIMEOUT = 12.0


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def fail(message: str, code: int = 1) -> "NoReturn":
    print()
    print("CT-W2 ATTEMPT 02 / H3: FAIL")
    print(message)
    print("No flash was performed by H3.")
    print("Do not reset or rerun until reviewed.")
    raise SystemExit(code)


def detect_stlink_vcp() -> str:
    matches: list[str] = []
    for port in list_ports.comports():
        desc = (port.description or "").lower()
        manu = (port.manufacturer or "").lower()
        hwid = (port.hwid or "").lower()

        is_stlink = (
            port.vid == 0x0483
            or "stlink" in desc
            or "st-link" in desc
            or "stmicroelectronics" in desc
            or "stmicroelectronics" in manu
            or "vid:pid=0483" in hwid
        )
        if is_stlink:
            matches.append(port.device)

    if len(matches) != 1:
        fail(f"Expected exactly one ST-LINK/VCP port, found {len(matches)}: {matches}")

    return matches[0]


def main() -> int:
    print("=== CT-W2 ATTEMPT 02 / H3 RUNTIME ===")

    for path in (PYTHON, PROGRAMMER, HOST):
        if not path.is_file():
            fail(f"Required file missing: {path}")

    if sha256(HOST) != EXPECTED_HOST_SHA256:
        fail("ct_ping_smoke.py SHA256 does not match the reviewed host tool.")

    for path in (
        STDOUT_PATH,
        STDERR_PATH,
        RESET_STDOUT_PATH,
        RESET_STDERR_PATH,
        SUMMARY_PATH,
    ):
        if path.exists():
            fail(f"Refusing to overwrite existing H3 evidence: {path}")

    port = detect_stlink_vcp()
    print(f"Selected VCP: {port}")

    host_args = [
        str(PYTHON),
        str(HOST),
        "--port", port,
        "--baud", str(BAUD),
        "--request-id", hex(REQUEST_ID),
        "--delay-ms", str(DELAY_MS),
        "--banner-timeout", str(BANNER_TIMEOUT),
        "--reply-timeout", str(REPLY_TIMEOUT),
        "--expect-k", "8",
        "--expect-mode", "NORMAL",
        "--json",
    ]

    print()
    print("=== START HOST LISTENER ===")

    with STDOUT_PATH.open("w", encoding="utf-8", newline="\n") as host_out, \
         STDERR_PATH.open("w", encoding="utf-8", newline="\n") as host_err:

        host = subprocess.Popen(
            host_args,
            stdout=host_out,
            stderr=host_err,
            cwd=str(REPO),
            text=True,
        )

        time.sleep(1.0)

        if host.poll() is not None:
            rc = host.returncode
            stderr_text = STDERR_PATH.read_text(encoding="utf-8", errors="replace")
            stdout_text = STDOUT_PATH.read_text(encoding="utf-8", errors="replace")
            fail(
                "Host listener exited before reset.\n"
                f"returncode={rc}\n"
                f"stdout={stdout_text}\n"
                f"stderr={stderr_text}"
            )

        print("Host listener: READY")

        print()
        print("=== EXPLICIT MCU RESET ===")

        reset = subprocess.run(
            [str(PROGRAMMER), "-c", "port=SWD", "-rst"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=15,
            cwd=str(REPO),
        )

        RESET_STDOUT_PATH.write_text(
            reset.stdout or "",
            encoding="utf-8",
            newline="\n",
        )
        RESET_STDERR_PATH.write_text(
            reset.stderr or "",
            encoding="utf-8",
            newline="\n",
        )

        print(f"CubeProgrammer reset return code: {reset.returncode}")

        if reset.returncode != 0:
            host.terminate()
            try:
                host.wait(timeout=2)
            except subprocess.TimeoutExpired:
                host.kill()
                host.wait(timeout=2)
            fail(
                "Explicit reset failed.\n"
                f"stdout={reset.stdout}\n"
                f"stderr={reset.stderr}"
            )

        reset_text = (reset.stdout or "") + "\n" + (reset.stderr or "")
        if "Software reset is performed" not in reset_text:
            host.terminate()
            try:
                host.wait(timeout=2)
            except subprocess.TimeoutExpired:
                host.kill()
                host.wait(timeout=2)
            fail("Reset command returned 0 but expected reset marker was absent.")

        print("Explicit reset: PASS")
        print("Debugger attached during runtime: NO")
        print("Flash performed during H3: NO")

        print()
        print("=== WAIT FOR ONE REAL PING ===")

        try:
            host_rc = host.wait(timeout=OVERALL_TIMEOUT)
        except subprocess.TimeoutExpired:
            host.kill()
            host.wait(timeout=2)
            fail("Host PING did not complete within the H3 timeout.")

    stdout_text = STDOUT_PATH.read_text(encoding="utf-8", errors="replace").strip()
    stderr_text = STDERR_PATH.read_text(encoding="utf-8", errors="replace").strip()

    print(f"Host return code: {host_rc}")
    print(f"Host stdout: {stdout_text}")
    print(f"Host stderr: {stderr_text}")

    if host_rc != 0:
        fail(f"Host PING returned nonzero exit code {host_rc}.")

    if stderr_text:
        fail("Host stderr is non-empty.")

    try:
        result = json.loads(stdout_text)
    except json.JSONDecodeError as exc:
        fail(f"Host stdout is not valid JSON: {exc}")

    required = {
        "result": "PASS",
        "request_id": REQUEST_ID,
        "request_payload_bytes": 64,
        "request_bytes": 76,
        "reply_payload_bytes": 12,
        "target_phase": 3,
        "target_k": 8,
        "target_drop_mode": 0,
    }

    for key, expected in required.items():
        actual = result.get(key)
        if actual != expected:
            fail(f"Host result mismatch for {key}: actual={actual!r}, expected={expected!r}")

    input_at_process = result.get("target_w6_input_at_process")
    if not isinstance(input_at_process, int) or not (1 <= input_at_process < 96):
        fail(
            "PING was not reported inside the W6 RUNNING interval: "
            f"target_w6_input_at_process={input_at_process!r}"
        )

    summary = {
        "result": "PASS",
        "scope": "CT-W2 Attempt 02 H3 runtime only",
        "flash_performed": False,
        "debugger_attached": False,
        "explicit_reset": True,
        "vcp": port,
        "host_returncode": host_rc,
        "request_id": REQUEST_ID,
        "request_payload_bytes": result["request_payload_bytes"],
        "request_bytes": result["request_bytes"],
        "reply_payload_bytes": result["reply_payload_bytes"],
        "payload_crc32": result.get("payload_crc32"),
        "target_phase": result["target_phase"],
        "target_k": result["target_k"],
        "target_drop_mode": result["target_drop_mode"],
        "target_w6_input_at_process": input_at_process,
        "host_round_trip_ms": result.get("host_round_trip_ms"),
        "host_stdout_file": str(STDOUT_PATH),
        "host_stderr_file": str(STDERR_PATH),
        "reset_stdout_file": str(RESET_STDOUT_PATH),
        "reset_stderr_file": str(RESET_STDERR_PATH),
    }

    SUMMARY_PATH.write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    print()
    print("=== H3 RESULT ===")
    print("CT-W2 Attempt 02 runtime PING: PASS")
    print("PING requests:                  1 / 1 PASS")
    print("PING payload:                   64 B")
    print(f"PING processed at W6 input:     {input_at_process}")
    print(f"Host round trip:                {result.get('host_round_trip_ms')} ms")
    print("Debugger attached:              NO")
    print("Flash performed:                NO")
    print(f"Evidence summary:               {SUMMARY_PATH}")
    print()
    print("RAM inspection:                 NOT RUN")
    print("Final hardware acceptance:      NOT YET CLAIMED")

    return 0


if __name__ == "__main__":
    sys.exit(main())
