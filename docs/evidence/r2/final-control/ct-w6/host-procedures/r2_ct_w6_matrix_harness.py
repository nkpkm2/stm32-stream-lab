#!/usr/bin/env python3
"""R2 CT-W6 six-cell matrix harness.

Usage:
    python r2_ct_w6_matrix_harness.py h2 k1-normal
    python r2_ct_w6_matrix_harness.py h3 k1-normal
    python r2_ct_w6_matrix_harness.py h4 k1-normal
    python r2_ct_w6_matrix_harness.py h5 k1-normal

Supported cells:
    k1-normal
    k2-normal
    k2-drop
    k4-normal
    k4-drop
    k8-drop

Each invocation performs exactly one workflow phase.

H2: flash + verify only.
H3: reset + one sealed adverse burst + passive completion wait only.
H4: post-run read-only GDB collection only.
H5: offline acceptance only.

No phase automatically invokes another phase.
No automatic retry.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import time
import zlib
from pathlib import Path


REPO = Path(r"E:\Projects\stm32-stream-lab")
ROOT = REPO / r"build\r2-ct-w6-matrix-94ca103c"
MATRIX_JSON = ROOT / "ct-w6-matrix-build-gate.json"

CHECKPOINT = "94ca103c27f508f97c38645883a712e0c16a76e8"

HOST_TOOL = REPO / r"tools\r2\r2_ct_burst.py"
EXPECTED_HOST = (
    "C70A794F61980DF6DBB0F4E6B61775C22E3AE51D37B2D32EB50BEE38717B143B"
)

PYTHON = Path(
    r"C:\Users\CHJ\AppData\Local\Programs\Python\Python310\python.exe"
)
PROGRAMMER = Path(
    r"E:\DevTools\STM32CubeProgrammer-2.23.0\bin\STM32_Programmer_CLI.exe"
)
GDB = Path(
    r"E:\DevTools\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi"
    r"\bin\arm-none-eabi-gdb.exe"
)
GDB_SERVER = Path(
    r"E:\DevTools\STM32CubeCLT-1.22.0\STLink-gdb-server"
    r"\bin\ST-LINK_gdbserver.exe"
)
CUBE_PROGRAMMER_BIN = Path(
    r"E:\DevTools\STM32CubeCLT-1.22.0\STM32CubeProgrammer\bin"
)

SUPPORTED_CELLS = (
    "k1-normal",
    "k2-normal",
    "k2-drop",
    "k4-normal",
    "k4-drop",
    "k8-drop",
)

CT_MAGIC = 0x52324354
W6_MAGIC = 0x52325736
W6_PHASE_COMPLETE = 5
W6_EVENTS = 800
BLOCK_SAMPLES = 256

DECISION_LIMIT = 57600
IRQ_EXIT_LIMIT = 80640
FINAL_WINDOW_LIMIT = 3600
U32_MASK = 0xFFFFFFFF

# H3 returns only after this passive wait. No target interaction occurs during
# the wait. 800 blocks at 1.28 ms = 1.024 s, so this is deliberately > 1.024 s.
POST_TRAFFIC_PASSIVE_WAIT_S = 1.25

GDB_PORT = 61234


def run(
    args: list[str],
    *,
    cwd: Path | None = None,
    timeout: float | None = None,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        args,
        cwd=str(cwd) if cwd else None,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        errors="replace",
        timeout=timeout,
        check=False,
    )

    if check and result.returncode != 0:
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

    with path.open("rb") as handle:
        for chunk in iter(
            lambda: handle.read(1024 * 1024),
            b"",
        ):
            h.update(chunk)

    return h.hexdigest().upper()


def require_file(path: Path) -> None:
    if not path.is_file():
        raise RuntimeError(
            f"required file missing: {path}"
        )


def common_repo_guard() -> None:
    if git("rev-parse", "HEAD") != CHECKPOINT:
        raise RuntimeError(
            "HEAD is not the frozen CT-W6 checkpoint"
        )

    if git("rev-parse", "origin/main") != CHECKPOINT:
        raise RuntimeError(
            "origin/main is not the frozen CT-W6 checkpoint"
        )

    status = git(
        "status",
        "--porcelain=v1",
        "-uall",
    )

    if status:
        raise RuntimeError(
            f"working tree is not clean:\n{status}"
        )

    if git("tag", "--list", "r2-pass"):
        raise RuntimeError(
            "r2-pass exists unexpectedly"
        )


def load_matrix() -> dict:
    require_file(MATRIX_JSON)

    data = json.loads(
        MATRIX_JSON.read_text(
            encoding="utf-8"
        )
    )

    if data.get("result") != "PASS":
        raise RuntimeError(
            "CT-W6 matrix build gate does not report PASS"
        )

    if data.get("checkpoint") != CHECKPOINT:
        raise RuntimeError(
            "CT-W6 matrix build-gate checkpoint mismatch"
        )

    if data.get("cell_count") != 6:
        raise RuntimeError(
            "CT-W6 matrix build gate does not contain exactly six cells"
        )

    if data.get("events") != 800:
        raise RuntimeError(
            "CT-W6 matrix event count mismatch"
        )

    if data.get("run_timeout_ms") != 1500:
        raise RuntimeError(
            "CT-W6 matrix timeout mismatch"
        )

    if sha256(HOST_TOOL) != EXPECTED_HOST:
        raise RuntimeError(
            "sealed adverse-burst host identity mismatch"
        )

    return data


def load_cell(slug: str) -> dict:
    matrix = load_matrix()

    matches = [
        item
        for item in matrix.get("remaining_cells", [])
        if item.get("cell") == slug
    ]

    if len(matches) != 1:
        raise RuntimeError(
            f"matrix cell lookup failed for {slug}: {len(matches)} matches"
        )

    cell = matches[0]

    expected_mode = (
        "DROP"
        if slug.endswith("-drop")
        else "NORMAL"
    )

    match = re.fullmatch(
        r"k([1248])-(normal|drop)",
        slug,
    )

    if not match:
        raise RuntimeError(
            f"malformed cell slug: {slug}"
        )

    expected_k = int(match.group(1))

    if cell.get("k") != expected_k:
        raise RuntimeError(
            f"{slug} K mismatch in matrix JSON"
        )

    if cell.get("mode") != expected_mode:
        raise RuntimeError(
            f"{slug} mode mismatch in matrix JSON"
        )

    build_dir = Path(
        cell["ct_on_candidate"]["build_directory"]
    )

    elf = build_dir / "cubemx.elf"
    bin_file = build_dir / "cubemx.bin"

    require_file(elf)
    require_file(bin_file)

    expected_elf = cell["ct_on_candidate"]["elf_sha256"]
    expected_bin = cell["ct_on_candidate"]["programmed_sha256"]

    if sha256(elf) != expected_elf:
        raise RuntimeError(
            f"{slug} ELF identity mismatch"
        )

    if sha256(bin_file) != expected_bin:
        raise RuntimeError(
            f"{slug} programmed-image identity mismatch"
        )

    # Verify the historical W6 cell regression was frozen PASS.
    regression = cell.get("ct_off_regression", {})

    if regression.get("programmed_identity") != "PASS":
        raise RuntimeError(
            f"{slug} sealed W6 regression is not PASS"
        )

    return cell


def paths_for(cell: dict) -> dict[str, Path]:
    build = Path(
        cell["ct_on_candidate"]["build_directory"]
    )

    return {
        "build": build,
        "elf": build / "cubemx.elf",
        "bin": build / "cubemx.bin",
        "h2_stdout": build / "hardware-attempt01-flash-verify.stdout.txt",
        "h2_stderr": build / "hardware-attempt01-flash-verify.stderr.txt",
        "h3_stdout": build / "hardware-attempt01-burst.stdout.txt",
        "h3_stderr": build / "hardware-attempt01-burst.stderr.txt",
        "reset_stdout": build / "hardware-attempt01-reset.stdout.txt",
        "reset_stderr": build / "hardware-attempt01-reset.stderr.txt",
        "h3_json": build / "hardware-attempt01-h3-runtime.json",
        "gdb_script": build / "hardware-attempt01-h4-inspection.gdb",
        "inspection": build / "hardware-attempt01-h4-inspection.txt",
        "inspection_stderr": build / "hardware-attempt01-h4-inspection.stderr.txt",
        "server_stdout": build / "hardware-attempt01-h4-gdbserver.stdout.txt",
        "server_stderr": build / "hardware-attempt01-h4-gdbserver.stderr.txt",
        "h4_json": build / "hardware-attempt01-h4-collection.json",
        "h5_json": build / "hardware-attempt01-h5-acceptance.json",
        "h5_txt": build / "hardware-attempt01-h5-acceptance.txt",
    }


def refuse_overwrite(*paths: Path) -> None:
    existing = [
        str(path)
        for path in paths
        if path.exists()
    ]

    if existing:
        raise RuntimeError(
            "refusing to overwrite existing evidence:\n"
            + "\n".join(existing)
        )


def h2(cell: dict) -> None:
    common_repo_guard()
    p = paths_for(cell)
    slug = cell["cell"]

    require_file(PROGRAMMER)

    refuse_overwrite(
        p["h2_stdout"],
        p["h2_stderr"],
    )

    print(f"=== CT-W6 / H2 {slug} FLASH + VERIFY ===")
    print(
        f"Profile: K={cell['k']} / {cell['mode']} / "
        "800 events / 1500 ms"
    )
    print(
        "Artifact identity: PASS\n"
        f"ELF SHA256: {cell['ct_on_candidate']['elf_sha256']}\n"
        f"BIN SHA256: {cell['ct_on_candidate']['programmed_sha256']}"
    )

    with p["h2_stdout"].open(
        "w",
        encoding="utf-8",
        newline="",
    ) as out_handle, p["h2_stderr"].open(
        "w",
        encoding="utf-8",
        newline="",
    ) as err_handle:
        result = subprocess.run(
            [
                str(PROGRAMMER),
                "-c",
                "port=SWD",
                "-d",
                str(p["elf"]),
                "-v",
            ],
            cwd=str(REPO),
            stdout=out_handle,
            stderr=err_handle,
            text=True,
            check=False,
        )

    if result.returncode != 0:
        raise RuntimeError(
            f"CubeProgrammer failed with exit code {result.returncode}"
        )

    combined = (
        p["h2_stdout"].read_text(
            encoding="utf-8",
            errors="replace",
        )
        + "\n"
        + p["h2_stderr"].read_text(
            encoding="utf-8",
            errors="replace",
        )
    )

    if "Download verified successfully" not in combined:
        raise RuntimeError(
            "CubeProgrammer returned 0 but verify marker is missing"
        )

    print()
    print("=== H2 RESULT ===")
    print(f"Cell:          {slug}")
    print("Flash+verify:  PASS")
    print("Runtime:       NOT RUN")
    print("Debugger:      NOT ATTACHED")
    print("Git:           UNCHANGED")


def select_stlink_vcp() -> str:
    try:
        from serial.tools import list_ports
    except ImportError as exc:
        raise RuntimeError(
            "H3 requires pyserial. Install/verify pyserial in the "
            "runtime Python environment before retrying H3."
        ) from exc

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


def verify_h2_anchor(p: dict[str, Path]) -> None:
    require_file(p["h2_stdout"])
    require_file(p["h2_stderr"])

    text = (
        p["h2_stdout"].read_text(
            encoding="utf-8",
            errors="replace",
        )
        + "\n"
        + p["h2_stderr"].read_text(
            encoding="utf-8",
            errors="replace",
        )
    )

    if "Download verified successfully" not in text:
        raise RuntimeError(
            "H2 flash/verify success marker missing"
        )


def h3(cell: dict) -> None:
    common_repo_guard()
    p = paths_for(cell)
    slug = cell["cell"]

    verify_h2_anchor(p)
    require_file(PYTHON)
    require_file(PROGRAMMER)
    require_file(HOST_TOOL)

    refuse_overwrite(
        p["h3_stdout"],
        p["h3_stderr"],
        p["reset_stdout"],
        p["reset_stderr"],
        p["h3_json"],
    )

    print(f"=== CT-W6 / H3 {slug} ADVERSE BURST ===")
    print("Flash: NO")
    print("Debugger/RAM inspection: NO")
    print("Automatic retry: NO")

    port = select_stlink_vcp()
    print(f"Selected VCP: {port}")

    base_id = int(cell["base_request_id"])

    host_args = [
        str(PYTHON),
        str(HOST_TOOL),
        "--port",
        port,
        "--baud",
        "115200",
        "--base-request-id",
        hex(base_id),
        "--delay-ms",
        "145",
        "--banner-timeout",
        "8",
        "--all-replies-timeout",
        "2",
        "--expect-k",
        str(cell["k"]),
        "--expect-mode",
        cell["mode"],
    ]

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

        p["h3_stdout"].write_text(
            stdout or "",
            encoding="utf-8",
            newline="\n",
        )
        p["h3_stderr"].write_text(
            stderr or "",
            encoding="utf-8",
            newline="\n",
        )

        raise RuntimeError(
            f"burst host exited before reset: rc={host.returncode}"
        )

    print("Host listener: READY")

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

    p["reset_stdout"].write_text(
        reset.stdout or "",
        encoding="utf-8",
        newline="\n",
    )
    p["reset_stderr"].write_text(
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
            "reset returned 0 but success marker is absent"
        )

    print("Explicit reset: PASS")

    try:
        stdout, stderr = host.communicate(
            timeout=15
        )
    except subprocess.TimeoutExpired:
        host.kill()
        stdout, stderr = host.communicate()

        p["h3_stdout"].write_text(
            stdout or "",
            encoding="utf-8",
            newline="\n",
        )
        p["h3_stderr"].write_text(
            stderr or "",
            encoding="utf-8",
            newline="\n",
        )

        raise RuntimeError(
            "adverse-burst host timed out"
        )

    p["h3_stdout"].write_text(
        stdout or "",
        encoding="utf-8",
        newline="\n",
    )
    p["h3_stderr"].write_text(
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
        result = json.loads(
            (stdout or "").strip()
        )
    except json.JSONDecodeError as exc:
        raise RuntimeError(
            f"burst host stdout is not JSON: {exc}"
        ) from exc

    if result.get("result") != "PASS":
        raise RuntimeError(
            "burst host JSON does not report PASS"
        )

    if result.get("profile") != "CT-W4_ADVERSE_BURST":
        raise RuntimeError(
            f"unexpected reused-host profile: {result.get('profile')!r}"
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
                f"burst shape mismatch: {key}={result.get(key)!r}, "
                f"expected {expected!r}"
            )

    records = result.get("records")

    if not isinstance(records, list) or len(records) != 10:
        raise RuntimeError(
            "expected exactly ten reply records"
        )

    expected_ids = list(
        range(base_id, base_id + 10)
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
        for earlier, later in zip(
            inputs,
            inputs[1:],
        )
    ):
        raise RuntimeError(
            f"W6 input observations are not strictly increasing: {inputs}"
        )

    expected_drop_mode = (
        1
        if cell["mode"] == "DROP"
        else 0
    )

    for index, record in enumerate(records):
        if int(record["target_phase"]) != 3:
            raise RuntimeError(
                f"reply {index} not processed while RUNNING"
            )

        if int(record["target_k"]) != int(cell["k"]):
            raise RuntimeError(
                f"reply {index} target K mismatch"
            )

        if int(record["target_drop_mode"]) != expected_drop_mode:
            raise RuntimeError(
                f"reply {index} target mode mismatch"
            )

    print(
        f"Traffic PASS: W6 input observations "
        f"{inputs[0]} -> {inputs[-1]}"
    )

    print(
        f"Passive post-traffic wait: "
        f"{POST_TRAFFIC_PASSIVE_WAIT_S:.2f} s "
        "(no target interaction)"
    )
    time.sleep(
        POST_TRAFFIC_PASSIVE_WAIT_S
    )

    summary = {
        "result": "PASS",
        "attempt": 1,
        "scope": f"CT-W6 H3 {slug} adverse burst",
        "checkpoint": CHECKPOINT,
        "cell": slug,
        "profile": {
            "k": int(cell["k"]),
            "mode": cell["mode"],
            "events": 800,
            "run_timeout_ms": 1500,
        },
        "request_id_first": base_id,
        "request_id_last": base_id + 9,
        **expected_shape,
        "host_burst_submit_ms": result["host_burst_submit_ms"],
        "host_all_replies_ms": result["host_all_replies_ms"],
        "first_w6_input": inputs[0],
        "last_w6_input": inputs[-1],
        "passive_completion_wait_s": POST_TRAFFIC_PASSIVE_WAIT_S,
        "host_returncode": host.returncode,
        "debugger_attached": False,
        "flash_performed": False,
        "ram_inspection_performed": False,
        "records": records,
    }

    p["h3_json"].write_text(
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
    print("=== H3 RESULT ===")
    print(f"Cell:                  {slug}")
    print("Adverse burst:         PASS")
    print("Replies:               10 / 10")
    print(
        f"W6 input observations: {inputs[0]} -> {inputs[-1]}"
    )
    print("Post-traffic wait:     COMPLETE")
    print("Flash during H3:       NO")
    print("Debugger:              NO")
    print("RAM inspection:        NO")


def build_gdb_script() -> str:
    return r"""set pagination off
set confirm off
set print pretty off

target remote 127.0.0.1:61234

printf "ct_magic=0x%08x\n", (unsigned int)g_r2_ct_result.magic
printf "ct_task_created=%u\n", (unsigned int)g_r2_ct_result.task_created
printf "ct_task_started=%u\n", (unsigned int)g_r2_ct_result.task_started
printf "ct_fault_bits=0x%08x\n", (unsigned int)g_r2_ct_result.fault_bits
printf "ct_rx_byte_count=%u\n", (unsigned int)g_r2_ct_result.rx_byte_count
printf "ct_rx_frame_count=%u\n", (unsigned int)g_r2_ct_result.rx_frame_count
printf "ct_rx_length_error_count=%u\n", (unsigned int)g_r2_ct_result.rx_length_error_count
printf "ct_rx_crc_error_count=%u\n", (unsigned int)g_r2_ct_result.rx_crc_error_count
printf "ct_rx_version_error_count=%u\n", (unsigned int)g_r2_ct_result.rx_version_error_count
printf "ct_rx_unsupported_count=%u\n", (unsigned int)g_r2_ct_result.rx_unsupported_count
printf "ct_rx_overflow_count=%u\n", (unsigned int)g_r2_ct_result.rx_overflow_count
printf "ct_uart_error_count=%u\n", (unsigned int)g_r2_ct_result.uart_error_count
printf "ct_rate_limited_count=%u\n", (unsigned int)g_r2_ct_result.rate_limited_count
printf "ct_processed_count=%u\n", (unsigned int)g_r2_ct_result.processed_count
printf "ct_processed_while_running_count=%u\n", (unsigned int)g_r2_ct_result.processed_while_running_count
printf "ct_reply_attempt_count=%u\n", (unsigned int)g_r2_ct_result.reply_attempt_count
printf "ct_reply_success_count=%u\n", (unsigned int)g_r2_ct_result.reply_success_count
printf "ct_reply_error_count=%u\n", (unsigned int)g_r2_ct_result.reply_error_count
printf "ct_first_processed_w6_input=%u\n", (unsigned int)g_r2_ct_result.first_processed_w6_input
printf "ct_last_processed_w6_input=%u\n", (unsigned int)g_r2_ct_result.last_processed_w6_input
printf "ct_trace_count=%u\n", (unsigned int)g_r2_ct_result.trace_count

set $i = 0
while $i < 10
  printf "CTTRACE,%u,%u,%u,%u,%u,%u,%u,%u,0x%08x,%u\n", $i, (unsigned int)g_r2_ct_result.trace[$i].request_id, (unsigned int)g_r2_ct_result.trace[$i].payload_length, (unsigned int)g_r2_ct_result.trace[$i].rx_complete_cycle, (unsigned int)g_r2_ct_result.trace[$i].processed_cycle, (unsigned int)g_r2_ct_result.trace[$i].reply_start_cycle, (unsigned int)g_r2_ct_result.trace[$i].reply_complete_cycle, (unsigned int)g_r2_ct_result.trace[$i].w6_input_at_process, (unsigned int)g_r2_ct_result.trace[$i].payload_crc32, (unsigned int)g_r2_ct_result.trace[$i].processed_while_running
  set $i = $i + 1
end

printf "w6_magic=0x%08x\n", (unsigned int)g_r2_w6_result.magic
printf "w6_phase=%u\n", (unsigned int)g_r2_w6_result.phase
printf "w6_test_pass=%u\n", (unsigned int)g_r2_w6_result.test_pass
printf "w6_fault_bits=0x%08x\n", (unsigned int)g_r2_w6_result.fault_bits
printf "w6_configured_k=%u\n", (unsigned int)g_r2_w6_result.configured_k
printf "w6_drop_mode=%u\n", (unsigned int)g_r2_w6_result.drop_mode
printf "w6_process_hold_blocks=%u\n", (unsigned int)g_r2_w6_result.process_hold_blocks
printf "w6_irq_count=%u\n", (unsigned int)g_r2_w6_result.irq_count
printf "w6_input_count=%u\n", (unsigned int)g_r2_w6_result.input_count
printf "w6_admitted_count=%u\n", (unsigned int)g_r2_w6_result.admitted_count
printf "w6_capacity_drop_count=%u\n", (unsigned int)g_r2_w6_result.capacity_drop_count
printf "w6_processed_count=%u\n", (unsigned int)g_r2_w6_result.processed_count
printf "w6_released_count=%u\n", (unsigned int)g_r2_w6_result.released_count
printf "w6_recovered_admission_after_drop_count=%u\n", (unsigned int)g_r2_w6_result.recovered_admission_after_drop_count
printf "w6_current_drop_streak=%u\n", (unsigned int)g_r2_w6_result.current_drop_streak
printf "w6_max_drop_streak=%u\n", (unsigned int)g_r2_w6_result.max_drop_streak
printf "w6_max_nominal_to_decision_cycles=%u\n", (unsigned int)g_r2_w6_result.max_nominal_to_decision_cycles
printf "w6_max_nominal_to_irq_exit_cycles=%u\n", (unsigned int)g_r2_w6_result.max_nominal_to_irq_exit_cycles
printf "w6_max_final_window_cycles=%u\n", (unsigned int)g_r2_w6_result.max_final_window_cycles
printf "w6_illegal_free_send_count=%u\n", (unsigned int)g_r2_w6_result.illegal_free_send_count
printf "w6_ready_send_fail_count=%u\n", (unsigned int)g_r2_w6_result.ready_send_fail_count
printf "w6_notification_fail_count=%u\n", (unsigned int)g_r2_w6_result.notification_fail_count
printf "w6_token_ledger_errors=%u\n", (unsigned int)g_r2_w6_result.token_ledger_errors
printf "w6_dma_error_flags_seen=%u\n", (unsigned int)g_r2_w6_result.dma_error_flags_seen
printf "w6_adc_ovr_seen=%u\n", (unsigned int)g_r2_w6_result.adc_ovr_seen
printf "w6_full_sample_count=%u\n", (unsigned int)g_r2_w6_result.full_sample_count
printf "w6_sample_errors=%u\n", (unsigned int)g_r2_w6_result.sample_errors
printf "w6_canary_errors=%u\n", (unsigned int)g_r2_w6_result.canary_errors
printf "w6_free_queue_depth_final=%u\n", (unsigned int)g_r2_w6_result.free_queue_depth_final
printf "w6_ready_queue_depth_final=%u\n", (unsigned int)g_r2_w6_result.ready_queue_depth_final
printf "w6_quiet_irq_count=%u\n", (unsigned int)g_r2_w6_result.quiet_irq_count
printf "w6_quiet_input_count=%u\n", (unsigned int)g_r2_w6_result.quiet_input_count
printf "w6_quiet_admitted_count=%u\n", (unsigned int)g_r2_w6_result.quiet_admitted_count
printf "w6_quiet_drop_count=%u\n", (unsigned int)g_r2_w6_result.quiet_drop_count
printf "w6_quiet_processed_count=%u\n", (unsigned int)g_r2_w6_result.quiet_processed_count
printf "w6_pool_free_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.free_count
printf "w6_pool_dma_owned_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.dma_owned_count
printf "w6_pool_ready_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.ready_count
printf "w6_pool_processing_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.processing_count
printf "w6_pool_violation_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.violation_count
printf "w6_slots_violation_count=%u\n", (unsigned int)g_r2_w6_result.slots_at_stop.violation_count

printf "W6TRACE_HEADER,index,sequence,decision,completed_id,replacement_id,mapping_epoch_after,m0_before,m0_after,m1_before,m1_after,free_depth_after_take,ready_depth_after_publish,processed_ok,nominal_to_decision_cycles,final_window_cycles\n"
set $i = 0
while $i < 800
  printf "W6TRACE,%u,%u,%u,%u,%u,%u,0x%08x,0x%08x,0x%08x,0x%08x,%u,%u,%u,%u,%u\n", $i, (unsigned int)g_r2_w6_result.trace[$i].sequence, (unsigned int)g_r2_w6_result.trace[$i].decision, (unsigned int)g_r2_w6_result.trace[$i].completed_id, (unsigned int)g_r2_w6_result.trace[$i].replacement_id, (unsigned int)g_r2_w6_result.trace[$i].mapping_epoch_after, (unsigned int)g_r2_w6_result.trace[$i].m0_before, (unsigned int)g_r2_w6_result.trace[$i].m0_after, (unsigned int)g_r2_w6_result.trace[$i].m1_before, (unsigned int)g_r2_w6_result.trace[$i].m1_after, (unsigned int)g_r2_w6_result.trace[$i].free_depth_after_take, (unsigned int)g_r2_w6_result.trace[$i].ready_depth_after_publish, (unsigned int)g_r2_w6_result.trace[$i].processed_ok, (unsigned int)g_r2_w6_result.trace[$i].nominal_to_decision_cycles, (unsigned int)g_r2_w6_result.trace[$i].final_window_cycles
  set $i = $i + 1
end

detach
quit
"""


def h4(cell: dict) -> None:
    common_repo_guard()
    p = paths_for(cell)
    slug = cell["cell"]

    require_file(p["h3_json"])
    require_file(GDB)
    require_file(GDB_SERVER)

    if not CUBE_PROGRAMMER_BIN.is_dir():
        raise RuntimeError(
            f"CubeProgrammer support directory missing: "
            f"{CUBE_PROGRAMMER_BIN}"
        )

    h3_data = json.loads(
        p["h3_json"].read_text(
            encoding="utf-8"
        )
    )

    if h3_data.get("result") != "PASS":
        raise RuntimeError(
            "H3 does not report PASS"
        )

    if h3_data.get("cell") != slug:
        raise RuntimeError(
            "H3 cell identity mismatch"
        )

    refuse_overwrite(
        p["gdb_script"],
        p["inspection"],
        p["inspection_stderr"],
        p["server_stdout"],
        p["server_stderr"],
        p["h4_json"],
    )

    p["gdb_script"].write_text(
        build_gdb_script(),
        encoding="utf-8",
        newline="\n",
    )

    print(f"=== CT-W6 / H4 {slug} POST-RUN RAM COLLECTION ===")
    print("Flash/reset/serial/continue: NO")
    print(
        f"H3 control observations: "
        f"{h3_data['first_w6_input']} -> "
        f"{h3_data['last_w6_input']}"
    )

    server_out_handle = p["server_stdout"].open(
        "w",
        encoding="utf-8",
        newline="",
    )
    server_err_handle = p["server_stderr"].open(
        "w",
        encoding="utf-8",
        newline="",
    )

    server = subprocess.Popen(
        [
            str(GDB_SERVER),
            "-p",
            str(GDB_PORT),
            "-d",
            "-g",
            "-l",
            "1",
            "-cp",
            str(CUBE_PROGRAMMER_BIN),
        ],
        cwd=str(REPO),
        stdout=server_out_handle,
        stderr=server_err_handle,
        text=True,
    )

    try:
        time.sleep(1.2)

        if server.poll() is not None:
            raise RuntimeError(
                f"GDB server exited early: {server.returncode}"
            )

        print("GDB server: READY")

        with p["inspection"].open(
            "w",
            encoding="utf-8",
            newline="",
        ) as out_handle, p["inspection_stderr"].open(
            "w",
            encoding="utf-8",
            newline="",
        ) as err_handle:
            gdb = subprocess.run(
                [
                    str(GDB),
                    "--batch",
                    "--quiet",
                    str(p["elf"]),
                    "-x",
                    str(p["gdb_script"]),
                ],
                cwd=str(REPO),
                stdout=out_handle,
                stderr=err_handle,
                text=True,
                check=False,
                timeout=60,
            )

        if gdb.returncode != 0:
            raise RuntimeError(
                f"GDB inspection failed with code {gdb.returncode}"
            )

    finally:
        if server.poll() is None:
            server.terminate()
            try:
                server.wait(timeout=3)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=3)

        server_out_handle.close()
        server_err_handle.close()

    text = p["inspection"].read_text(
        encoding="utf-8",
        errors="replace",
    )

    markers = (
        "ct_magic=",
        "ct_trace_count=",
        "w6_magic=",
        "w6_phase=",
        "w6_input_count=",
        "W6TRACE_HEADER,",
    )

    for marker in markers:
        if marker not in text:
            raise RuntimeError(
                f"inspection missing marker: {marker}"
            )

    ct_rows = [
        line
        for line in text.splitlines()
        if line.startswith("CTTRACE,")
    ]
    w6_rows = [
        line
        for line in text.splitlines()
        if line.startswith("W6TRACE,")
    ]

    if len(ct_rows) != 10:
        raise RuntimeError(
            f"expected 10 CTTRACE rows, found {len(ct_rows)}"
        )

    if len(w6_rows) != 800:
        raise RuntimeError(
            f"expected 800 W6TRACE rows, found {len(w6_rows)}"
        )

    for index, line in enumerate(w6_rows):
        parts = line.split(",")

        if len(parts) != 16:
            raise RuntimeError(
                f"malformed W6TRACE row {index}"
            )

        if int(parts[1], 10) != index:
            raise RuntimeError(
                f"W6TRACE index discontinuity at {index}"
            )

        if int(parts[2], 10) != index + 1:
            raise RuntimeError(
                f"W6TRACE sequence discontinuity at {index}"
            )

    summary = {
        "result": "COLLECTED",
        "attempt": 1,
        "cell": slug,
        "checkpoint": CHECKPOINT,
        "h3_first_w6_input": int(h3_data["first_w6_input"]),
        "h3_last_w6_input": int(h3_data["last_w6_input"]),
        "ct_trace_lines": 10,
        "w6_trace_lines": 800,
        "w6_sequence_first": 1,
        "w6_sequence_last": 800,
        "flash_performed": False,
        "reset_performed": False,
        "serial_traffic_performed": False,
        "continue_or_restart_performed": False,
        "acceptance_evaluation_performed": False,
        "inspection_sha256": sha256(
            p["inspection"]
        ),
    }

    p["h4_json"].write_text(
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
    print("=== H4 RESULT ===")
    print(f"Cell:             {slug}")
    print("RAM collection:   PASS")
    print("CT trace:         10 / 10")
    print("W6 trace:         800 / 800")
    print("Sequence:         1 -> 800")
    print("Acceptance:       NOT RUN")


def parse_int(raw: str) -> int:
    raw = raw.strip()
    return (
        int(raw, 16)
        if raw.lower().startswith("0x")
        else int(raw, 10)
    )


def parse_key_values(text: str) -> dict[str, int]:
    result: dict[str, int] = {}
    pattern = re.compile(
        r"^([A-Za-z0-9_]+)=(0x[0-9A-Fa-f]+|[0-9]+)$"
    )

    for raw in text.splitlines():
        match = pattern.fullmatch(
            raw.strip()
        )

        if match:
            result[match.group(1)] = parse_int(
                match.group(2)
            )

    return result


def parse_ct_trace(text: str) -> list[dict[str, int]]:
    rows: list[dict[str, int]] = []

    for raw in text.splitlines():
        if not raw.startswith("CTTRACE,"):
            continue

        parts = raw.split(",")

        if len(parts) != 11:
            raise AssertionError(
                f"malformed CTTRACE row: {raw}"
            )

        rows.append(
            {
                "index": int(parts[1], 10),
                "request_id": int(parts[2], 10),
                "payload_length": int(parts[3], 10),
                "rx_complete_cycle": int(parts[4], 10),
                "processed_cycle": int(parts[5], 10),
                "reply_start_cycle": int(parts[6], 10),
                "reply_complete_cycle": int(parts[7], 10),
                "w6_input": int(parts[8], 10),
                "payload_crc32": parse_int(parts[9]),
                "processed_while_running": int(parts[10], 10),
            }
        )

    return rows


def parse_w6_trace(text: str) -> list[dict[str, int]]:
    rows: list[dict[str, int]] = []

    for raw in text.splitlines():
        if not raw.startswith("W6TRACE,"):
            continue

        parts = raw.split(",")

        if len(parts) != 16:
            raise AssertionError(
                f"malformed W6TRACE row: {raw}"
            )

        rows.append(
            {
                "index": int(parts[1], 10),
                "sequence": int(parts[2], 10),
                "decision": int(parts[3], 10),
                "completed_id": int(parts[4], 10),
                "replacement_id": int(parts[5], 10),
                "mapping_epoch_after": int(parts[6], 10),
                "m0_before": parse_int(parts[7]),
                "m0_after": parse_int(parts[8]),
                "m1_before": parse_int(parts[9]),
                "m1_after": parse_int(parts[10]),
                "free_depth_after_take": int(parts[11], 10),
                "ready_depth_after_publish": int(parts[12], 10),
                "processed_ok": int(parts[13], 10),
                "nominal_to_decision_cycles": int(parts[14], 10),
                "final_window_cycles": int(parts[15], 10),
            }
        )

    return rows


def expect(
    values: dict[str, int],
    name: str,
    expected: int,
) -> None:
    if name not in values:
        raise AssertionError(
            f"missing inspection field: {name}"
        )

    actual = values[name]

    if actual != expected:
        raise AssertionError(
            f"{name}={actual}, expected {expected}"
        )


def expect_zero(
    values: dict[str, int],
    name: str,
) -> None:
    expect(values, name, 0)


def expected_payload(
    request_id: int,
) -> bytes:
    return bytes(
        (
            (request_id + i * 17 + 0x5A)
            & 0xFF
        )
        for i in range(64)
    )


def u32_delta(
    start: int,
    end: int,
) -> int:
    return (end - start) & U32_MASK


def derive_drop_codes(
    rows: list[dict[str, int]],
) -> tuple[int, int]:
    admit_codes = {
        row["decision"]
        for row in rows
        if row["processed_ok"] == 1
    }
    drop_codes = {
        row["decision"]
        for row in rows
        if row["processed_ok"] == 0
    }

    if len(admit_codes) != 1:
        raise AssertionError(
            f"could not derive unique ADMIT code: {admit_codes}"
        )

    if len(drop_codes) != 1:
        raise AssertionError(
            f"could not derive unique DROP code: {drop_codes}"
        )

    admit = next(iter(admit_codes))
    drop = next(iter(drop_codes))

    if admit == drop:
        raise AssertionError(
            "ADMIT/DROP decision codes identical"
        )

    return admit, drop


def reconstruct_drop(
    rows: list[dict[str, int]],
    *,
    admit_code: int,
    drop_code: int,
) -> dict[str, int]:
    admitted = 0
    drops = 0
    recoveries = 0
    current = 0
    maximum = 0

    for row in rows:
        if row["decision"] == drop_code:
            drops += 1
            current += 1
            maximum = max(
                maximum,
                current,
            )
        elif row["decision"] == admit_code:
            admitted += 1

            if current:
                recoveries += 1
                current = 0
        else:
            raise AssertionError(
                f"unexpected decision code at seq {row['sequence']}"
            )

    return {
        "admitted": admitted,
        "drops": drops,
        "recoveries": recoveries,
        "current_streak": current,
        "max_streak": maximum,
    }


def find_recovery_in_window(
    rows: list[dict[str, int]],
    *,
    start: int,
    end: int,
    admit_code: int,
    drop_code: int,
) -> tuple[
    list[dict[str, int]],
    tuple[int, int] | None,
]:
    window = [
        row
        for row in rows
        if start <= row["sequence"] <= end
    ]

    first_drop: int | None = None

    for row in window:
        if row["decision"] == drop_code:
            if first_drop is None:
                first_drop = row["sequence"]

        elif (
            row["decision"] == admit_code
            and first_drop is not None
        ):
            return (
                window,
                (
                    first_drop,
                    row["sequence"],
                ),
            )

    return window, None


def validate_ct(
    cell: dict,
    values: dict[str, int],
    ct_rows: list[dict[str, int]],
    h3_data: dict,
) -> dict:
    base_id = int(cell["base_request_id"])
    expected_ids = list(
        range(base_id, base_id + 10)
    )

    host_records = h3_data["records"]
    host_inputs = [
        int(record["target_w6_input_at_process"])
        for record in host_records
    ]

    expect(values, "ct_magic", CT_MAGIC)
    expect(values, "ct_task_created", 1)
    expect(values, "ct_task_started", 1)
    expect_zero(values, "ct_fault_bits")
    expect(values, "ct_rx_byte_count", 760)
    expect(values, "ct_rx_frame_count", 10)

    for name in (
        "ct_rx_length_error_count",
        "ct_rx_crc_error_count",
        "ct_rx_version_error_count",
        "ct_rx_unsupported_count",
        "ct_rx_overflow_count",
        "ct_uart_error_count",
        "ct_rate_limited_count",
        "ct_reply_error_count",
    ):
        expect_zero(values, name)

    expect(values, "ct_processed_count", 10)
    expect(values, "ct_processed_while_running_count", 10)
    expect(values, "ct_reply_attempt_count", 10)
    expect(values, "ct_reply_success_count", 10)
    expect(values, "ct_trace_count", 10)
    expect(
        values,
        "ct_first_processed_w6_input",
        host_inputs[0],
    )
    expect(
        values,
        "ct_last_processed_w6_input",
        host_inputs[-1],
    )

    if len(ct_rows) != 10:
        raise AssertionError(
            f"parsed CT rows={len(ct_rows)}, expected 10"
        )

    max_rx_proc = 0
    max_proc_tx = 0
    max_tx = 0
    max_total = 0

    for index, row in enumerate(ct_rows):
        if row["index"] != index:
            raise AssertionError(
                f"CT trace index mismatch at {index}"
            )

        if row["request_id"] != expected_ids[index]:
            raise AssertionError(
                f"CT request ID mismatch at {index}"
            )

        if row["payload_length"] != 64:
            raise AssertionError(
                f"CT payload length mismatch at {index}"
            )

        if row["w6_input"] != host_inputs[index]:
            raise AssertionError(
                f"CT/H3 W6 input mismatch at {index}"
            )

        if row["processed_while_running"] != 1:
            raise AssertionError(
                f"CT request {index} not processed while RUNNING"
            )

        expected_crc = (
            zlib.crc32(
                expected_payload(
                    expected_ids[index]
                )
            )
            & U32_MASK
        )

        if row["payload_crc32"] != expected_crc:
            raise AssertionError(
                f"CT payload CRC mismatch at {index}"
            )

        host_crc_raw = host_records[index][
            "payload_crc32"
        ]
        host_crc = (
            int(host_crc_raw, 16)
            if isinstance(
                host_crc_raw,
                str,
            )
            else int(host_crc_raw)
        )

        if host_crc != expected_crc:
            raise AssertionError(
                f"host payload CRC mismatch at {index}"
            )

        times = (
            row["rx_complete_cycle"],
            row["processed_cycle"],
            row["reply_start_cycle"],
            row["reply_complete_cycle"],
        )

        if 0 in times:
            raise AssertionError(
                f"zero CT service timestamp at {index}"
            )

        d1 = u32_delta(
            row["rx_complete_cycle"],
            row["processed_cycle"],
        )
        d2 = u32_delta(
            row["processed_cycle"],
            row["reply_start_cycle"],
        )
        d3 = u32_delta(
            row["reply_start_cycle"],
            row["reply_complete_cycle"],
        )
        d4 = u32_delta(
            row["rx_complete_cycle"],
            row["reply_complete_cycle"],
        )

        for delta in (
            d1,
            d2,
            d3,
            d4,
        ):
            if delta >= 180_000_000:
                raise AssertionError(
                    f"CT timestamp ordering invalid at {index}"
                )

        max_rx_proc = max(
            max_rx_proc,
            d1,
        )
        max_proc_tx = max(
            max_proc_tx,
            d2,
        )
        max_tx = max(
            max_tx,
            d3,
        )
        max_total = max(
            max_total,
            d4,
        )

    return {
        "first_w6_input": host_inputs[0],
        "last_w6_input": host_inputs[-1],
        "max_rx_to_process_cycles": max_rx_proc,
        "max_process_to_tx_cycles": max_proc_tx,
        "max_tx_cycles": max_tx,
        "max_rx_to_reply_complete_cycles": max_total,
    }


def validate_common_w6(
    cell: dict,
    values: dict[str, int],
) -> dict:
    k = int(cell["k"])
    drop_mode = (
        1
        if cell["mode"] == "DROP"
        else 0
    )
    hold_blocks = (
        k + 5
        if drop_mode
        else 0
    )

    expect(values, "w6_magic", W6_MAGIC)
    expect(
        values,
        "w6_phase",
        W6_PHASE_COMPLETE,
    )
    expect(values, "w6_test_pass", 1)
    expect_zero(values, "w6_fault_bits")
    expect(values, "w6_configured_k", k)
    expect(values, "w6_drop_mode", drop_mode)
    expect(
        values,
        "w6_process_hold_blocks",
        hold_blocks,
    )
    expect(values, "w6_irq_count", W6_EVENTS)
    expect(values, "w6_input_count", W6_EVENTS)

    for name in (
        "w6_illegal_free_send_count",
        "w6_ready_send_fail_count",
        "w6_notification_fail_count",
        "w6_token_ledger_errors",
        "w6_dma_error_flags_seen",
        "w6_adc_ovr_seen",
        "w6_sample_errors",
        "w6_canary_errors",
        "w6_ready_queue_depth_final",
        "w6_pool_ready_count",
        "w6_pool_processing_count",
        "w6_pool_violation_count",
        "w6_slots_violation_count",
    ):
        expect_zero(values, name)

    expect(
        values,
        "w6_free_queue_depth_final",
        k,
    )
    expect(
        values,
        "w6_pool_free_count",
        k,
    )
    expect(
        values,
        "w6_pool_dma_owned_count",
        2,
    )

    decision = values[
        "w6_max_nominal_to_decision_cycles"
    ]
    irq_exit = values[
        "w6_max_nominal_to_irq_exit_cycles"
    ]
    final_window = values[
        "w6_max_final_window_cycles"
    ]

    if decision > DECISION_LIMIT:
        raise AssertionError(
            f"W6 decision timing {decision} > {DECISION_LIMIT}"
        )

    if irq_exit > IRQ_EXIT_LIMIT:
        raise AssertionError(
            f"W6 IRQ-exit timing {irq_exit} > {IRQ_EXIT_LIMIT}"
        )

    if final_window > FINAL_WINDOW_LIMIT:
        raise AssertionError(
            f"W6 final-window timing {final_window} > "
            f"{FINAL_WINDOW_LIMIT}"
        )

    return {
        "max_nominal_to_decision_cycles": decision,
        "max_nominal_to_irq_exit_cycles": irq_exit,
        "max_final_window_cycles": final_window,
    }


def validate_normal(
    cell: dict,
    values: dict[str, int],
    rows: list[dict[str, int]],
) -> dict:
    expect(
        values,
        "w6_admitted_count",
        W6_EVENTS,
    )
    expect_zero(
        values,
        "w6_capacity_drop_count",
    )
    expect(
        values,
        "w6_processed_count",
        W6_EVENTS,
    )
    expect(
        values,
        "w6_released_count",
        W6_EVENTS,
    )
    expect_zero(
        values,
        "w6_recovered_admission_after_drop_count",
    )
    expect_zero(
        values,
        "w6_current_drop_streak",
    )
    expect_zero(
        values,
        "w6_max_drop_streak",
    )
    expect(
        values,
        "w6_full_sample_count",
        W6_EVENTS * BLOCK_SAMPLES,
    )

    expect(
        values,
        "w6_quiet_irq_count",
        W6_EVENTS,
    )
    expect(
        values,
        "w6_quiet_input_count",
        W6_EVENTS,
    )
    expect(
        values,
        "w6_quiet_admitted_count",
        W6_EVENTS,
    )
    expect_zero(
        values,
        "w6_quiet_drop_count",
    )
    expect(
        values,
        "w6_quiet_processed_count",
        W6_EVENTS,
    )

    codes = {
        row["decision"]
        for row in rows
    }

    if len(codes) != 1:
        raise AssertionError(
            f"NORMAL trace has multiple decision codes: {codes}"
        )

    admit_code = next(
        iter(codes)
    )

    previous_epoch: int | None = None

    for row in rows:
        if row["processed_ok"] != 1:
            raise AssertionError(
                f"NORMAL seq {row['sequence']} processed_ok != 1"
            )

        changed_m0 = (
            row["m0_before"]
            != row["m0_after"]
        )
        changed_m1 = (
            row["m1_before"]
            != row["m1_after"]
        )

        if changed_m0 == changed_m1:
            raise AssertionError(
                f"NORMAL seq {row['sequence']} did not change "
                "exactly one MxAR"
            )

        if (
            previous_epoch is not None
            and row["mapping_epoch_after"]
            != previous_epoch + 1
        ):
            raise AssertionError(
                f"NORMAL seq {row['sequence']} did not advance "
                "mapping epoch by one"
            )

        if (
            row["nominal_to_decision_cycles"]
            > DECISION_LIMIT
        ):
            raise AssertionError(
                f"NORMAL seq {row['sequence']} decision timing fail"
            )

        if (
            row["final_window_cycles"]
            > FINAL_WINDOW_LIMIT
        ):
            raise AssertionError(
                f"NORMAL seq {row['sequence']} final-window fail"
            )

        previous_epoch = row[
            "mapping_epoch_after"
        ]

    return {
        "admitted": 800,
        "drops": 0,
        "processed": 800,
        "released": 800,
        "recoveries": 0,
        "max_drop_streak": 0,
        "decision_code_admit": admit_code,
        "intersection_required": False,
    }


def validate_drop(
    cell: dict,
    values: dict[str, int],
    rows: list[dict[str, int]],
    ct_result: dict,
) -> dict:
    k = int(cell["k"])

    admitted = values[
        "w6_admitted_count"
    ]
    drops = values[
        "w6_capacity_drop_count"
    ]
    processed = values[
        "w6_processed_count"
    ]
    released = values[
        "w6_released_count"
    ]
    recoveries = values[
        "w6_recovered_admission_after_drop_count"
    ]
    current = values[
        "w6_current_drop_streak"
    ]
    maximum = values[
        "w6_max_drop_streak"
    ]

    if admitted + drops != W6_EVENTS:
        raise AssertionError(
            "DROP admitted+drops != 800"
        )

    if admitted < k + 3:
        raise AssertionError(
            f"DROP admitted {admitted} < {k + 3}"
        )

    if drops < 8:
        raise AssertionError(
            f"DROP count {drops} < 8"
        )

    if recoveries < 3:
        raise AssertionError(
            f"DROP recoveries {recoveries} < 3"
        )

    if maximum < 3:
        raise AssertionError(
            f"DROP max streak {maximum} < 3"
        )

    if processed != admitted:
        raise AssertionError(
            "DROP processed != admitted"
        )

    if released != admitted:
        raise AssertionError(
            "DROP released != admitted"
        )

    expect(
        values,
        "w6_full_sample_count",
        admitted * BLOCK_SAMPLES,
    )

    expect(
        values,
        "w6_quiet_irq_count",
        W6_EVENTS,
    )
    expect(
        values,
        "w6_quiet_input_count",
        W6_EVENTS,
    )
    expect(
        values,
        "w6_quiet_admitted_count",
        admitted,
    )
    expect(
        values,
        "w6_quiet_drop_count",
        drops,
    )
    expect(
        values,
        "w6_quiet_processed_count",
        processed,
    )

    admit_code, drop_code = derive_drop_codes(
        rows
    )

    reconstructed = reconstruct_drop(
        rows,
        admit_code=admit_code,
        drop_code=drop_code,
    )

    expected_reconstructed = {
        "admitted": admitted,
        "drops": drops,
        "recoveries": recoveries,
        "current_streak": current,
        "max_streak": maximum,
    }

    if reconstructed != expected_reconstructed:
        raise AssertionError(
            f"DROP trace accounting mismatch: "
            f"{reconstructed} != {expected_reconstructed}"
        )

    previous_epoch: int | None = None

    for row in rows:
        if row["decision"] == drop_code:
            if row["processed_ok"] != 0:
                raise AssertionError(
                    f"DROP seq {row['sequence']} processed_ok != 0"
                )

            if row["free_depth_after_take"] != 0:
                raise AssertionError(
                    f"DROP seq {row['sequence']} free depth != 0"
                )

            if (
                row["m0_before"]
                != row["m0_after"]
            ):
                raise AssertionError(
                    f"DROP seq {row['sequence']} changed M0AR"
                )

            if (
                row["m1_before"]
                != row["m1_after"]
            ):
                raise AssertionError(
                    f"DROP seq {row['sequence']} changed M1AR"
                )

            if (
                previous_epoch is not None
                and row["mapping_epoch_after"]
                != previous_epoch
            ):
                raise AssertionError(
                    f"DROP seq {row['sequence']} advanced mapping epoch"
                )

        elif row["decision"] == admit_code:
            if row["processed_ok"] != 1:
                raise AssertionError(
                    f"ADMIT seq {row['sequence']} processed_ok != 1"
                )

            changed_m0 = (
                row["m0_before"]
                != row["m0_after"]
            )
            changed_m1 = (
                row["m1_before"]
                != row["m1_after"]
            )

            if changed_m0 == changed_m1:
                raise AssertionError(
                    f"ADMIT seq {row['sequence']} did not change "
                    "exactly one MxAR"
                )

            if (
                previous_epoch is not None
                and row["mapping_epoch_after"]
                != previous_epoch + 1
            ):
                raise AssertionError(
                    f"ADMIT seq {row['sequence']} did not advance "
                    "mapping epoch by one"
                )

        else:
            raise AssertionError(
                f"unexpected DROP-mode decision at seq {row['sequence']}"
            )

        if (
            row["nominal_to_decision_cycles"]
            > DECISION_LIMIT
        ):
            raise AssertionError(
                f"seq {row['sequence']} decision timing fail"
            )

        if (
            row["final_window_cycles"]
            > FINAL_WINDOW_LIMIT
        ):
            raise AssertionError(
                f"seq {row['sequence']} final-window fail"
            )

        previous_epoch = row[
            "mapping_epoch_after"
        ]

    strict_start = (
        ct_result["first_w6_input"]
        + 1
    )
    strict_end = (
        ct_result["last_w6_input"]
    )

    if strict_start > strict_end:
        raise AssertionError(
            "no guaranteed event interval between CT endpoints"
        )

    window, pair = find_recovery_in_window(
        rows,
        start=strict_start,
        end=strict_end,
        admit_code=admit_code,
        drop_code=drop_code,
    )

    window_admits = sum(
        row["decision"] == admit_code
        for row in window
    )
    window_drops = sum(
        row["decision"] == drop_code
        for row in window
    )

    if window_drops == 0:
        raise AssertionError(
            "no DROP inside guaranteed CT interval"
        )

    if pair is None:
        raise AssertionError(
            "no DROP -> later ADMIT inside guaranteed CT interval"
        )

    return {
        "admitted": admitted,
        "drops": drops,
        "processed": processed,
        "released": released,
        "recoveries": recoveries,
        "max_drop_streak": maximum,
        "current_drop_streak": current,
        "decision_code_admit": admit_code,
        "decision_code_drop": drop_code,
        "intersection_required": True,
        "guaranteed_event_start": strict_start,
        "guaranteed_event_end": strict_end,
        "window_admitted_count": window_admits,
        "window_drop_count": window_drops,
        "recovery_drop_sequence": pair[0],
        "recovery_admit_sequence": pair[1],
    }


def h5(cell: dict) -> None:
    common_repo_guard()
    p = paths_for(cell)
    slug = cell["cell"]

    for path in (
        p["h3_json"],
        p["h3_stdout"],
        p["h3_stderr"],
        p["h4_json"],
        p["inspection"],
        p["inspection_stderr"],
    ):
        require_file(path)

    refuse_overwrite(
        p["h5_json"],
        p["h5_txt"],
    )

    h3_data = json.loads(
        p["h3_json"].read_text(
            encoding="utf-8"
        )
    )
    h4_data = json.loads(
        p["h4_json"].read_text(
            encoding="utf-8"
        )
    )

    if h3_data.get("result") != "PASS":
        raise RuntimeError(
            "H3 does not report PASS"
        )

    if h4_data.get("result") != "COLLECTED":
        raise RuntimeError(
            "H4 does not report COLLECTED"
        )

    if h3_data.get("cell") != slug:
        raise RuntimeError(
            "H3 cell mismatch"
        )

    if h4_data.get("cell") != slug:
        raise RuntimeError(
            "H4 cell mismatch"
        )

    if sha256(p["inspection"]) != h4_data.get(
        "inspection_sha256"
    ):
        raise RuntimeError(
            "H4 inspection hash mismatch"
        )

    host_stderr = p[
        "h3_stderr"
    ].read_text(
        encoding="utf-8",
        errors="replace",
    ).strip()

    if host_stderr:
        raise RuntimeError(
            f"H3 host stderr non-empty: {host_stderr}"
        )

    raw_host = json.loads(
        p["h3_stdout"].read_text(
            encoding="utf-8",
            errors="replace",
        ).strip()
    )

    if raw_host.get("result") != "PASS":
        raise RuntimeError(
            "raw H3 host JSON does not report PASS"
        )

    if raw_host.get("records") != h3_data.get(
        "records"
    ):
        raise RuntimeError(
            "raw H3 records differ from H3 summary"
        )

    inspection = p[
        "inspection"
    ].read_text(
        encoding="utf-8",
        errors="replace",
    )

    values = parse_key_values(
        inspection
    )
    ct_rows = parse_ct_trace(
        inspection
    )
    w6_rows = parse_w6_trace(
        inspection
    )

    if len(w6_rows) != 800:
        raise AssertionError(
            f"W6 trace rows={len(w6_rows)}, expected 800"
        )

    for index, row in enumerate(
        w6_rows
    ):
        if row["index"] != index:
            raise AssertionError(
                f"W6 trace index mismatch at {index}"
            )

        if row["sequence"] != index + 1:
            raise AssertionError(
                f"W6 sequence mismatch at {index}"
            )

    ct_result = validate_ct(
        cell,
        values,
        ct_rows,
        h3_data,
    )

    timing = validate_common_w6(
        cell,
        values,
    )

    if cell["mode"] == "NORMAL":
        mode_result = validate_normal(
            cell,
            values,
            w6_rows,
        )
    else:
        mode_result = validate_drop(
            cell,
            values,
            w6_rows,
            ct_result,
        )

    result = {
        "result": "PASS",
        "attempt": 1,
        "cell": slug,
        "checkpoint": CHECKPOINT,
        "profile": {
            "k": int(cell["k"]),
            "mode": cell["mode"],
            "events": 800,
            "run_timeout_ms": 1500,
        },
        "artifact_identity": {
            "elf_sha256": cell[
                "ct_on_candidate"
            ]["elf_sha256"],
            "programmed_sha256": cell[
                "ct_on_candidate"
            ]["programmed_sha256"],
            "host_tool_sha256": EXPECTED_HOST,
        },
        "control_traffic": {
            "requests": 10,
            "payload_bytes_each": 64,
            "burst_bytes": 760,
            "single_write_call": True,
            "rate_limited_count": 0,
            "rx_overflow_count": 0,
            "reply_success_count": 10,
            **ct_result,
            "host_submit_ms": float(
                h3_data["host_burst_submit_ms"]
            ),
            "host_all_replies_ms": float(
                h3_data["host_all_replies_ms"]
            ),
            "host_timestamp_interpretation": (
                "orchestration only; not a target service-latency gate"
            ),
        },
        "w6": {
            **mode_result,
            **timing,
        },
        "evidence_sha256": {
            p["h2_stdout"].name: sha256(
                p["h2_stdout"]
            ),
            p["h2_stderr"].name: sha256(
                p["h2_stderr"]
            ),
            p["h3_json"].name: sha256(
                p["h3_json"]
            ),
            p["h3_stdout"].name: sha256(
                p["h3_stdout"]
            ),
            p["h3_stderr"].name: sha256(
                p["h3_stderr"]
            ),
            p["h4_json"].name: sha256(
                p["h4_json"]
            ),
            p["inspection"].name: sha256(
                p["inspection"]
            ),
            p["inspection_stderr"].name: sha256(
                p["inspection_stderr"]
            ),
        },
        "hardware_operations_during_h5": False,
    }

    p["h5_json"].write_text(
        json.dumps(
            result,
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
        newline="\n",
    )

    lines = [
        f"R2 CT-W6 {slug} HARDWARE ACCEPTANCE: PASS",
        "",
        f"Checkpoint: {CHECKPOINT}",
        f"Cell: {slug}",
        (
            f"Profile: K={cell['k']} / {cell['mode']} / "
            "800 events / 1500 ms"
        ),
        "",
        "Control traffic:",
        "  10 x 64-byte PING",
        "  one 760-byte serial write",
        "  rate-limited: 0",
        "  RX overflow: 0",
        "  replies: 10 / 10",
        (
            f"  W6 input observations: "
            f"{ct_result['first_w6_input']} -> "
            f"{ct_result['last_w6_input']}"
        ),
        (
            "  CT max service cycles "
            "(rx->proc / proc->tx / tx / rx->done): "
            f"{ct_result['max_rx_to_process_cycles']} / "
            f"{ct_result['max_process_to_tx_cycles']} / "
            f"{ct_result['max_tx_cycles']} / "
            f"{ct_result['max_rx_to_reply_complete_cycles']}"
        ),
        "",
        "W6:",
        (
            f"  admitted / dropped: "
            f"{mode_result['admitted']} / "
            f"{mode_result['drops']}"
        ),
        (
            f"  processed / released: "
            f"{mode_result['processed']} / "
            f"{mode_result['released']}"
        ),
        (
            "  timing cycles "
            "(decision / IRQ-exit / final-window): "
            f"{timing['max_nominal_to_decision_cycles']} / "
            f"{timing['max_nominal_to_irq_exit_cycles']} / "
            f"{timing['max_final_window_cycles']}"
        ),
        "  integrity errors: 0",
        (
            f"  final pool: {cell['k']} FREE + 2 DMA"
        ),
    ]

    if cell["mode"] == "DROP":
        lines.extend(
            [
                f"  recoveries: {mode_result['recoveries']}",
                (
                    f"  max drop streak: "
                    f"{mode_result['max_drop_streak']}"
                ),
                "",
                "CT x DROP/recovery intersection:",
                (
                    f"  guaranteed events: "
                    f"{mode_result['guaranteed_event_start']}"
                    f"..{mode_result['guaranteed_event_end']}"
                ),
                (
                    f"  in-window admitted / dropped: "
                    f"{mode_result['window_admitted_count']} / "
                    f"{mode_result['window_drop_count']}"
                ),
                (
                    f"  recovery: DROP "
                    f"{mode_result['recovery_drop_sequence']} -> ADMIT "
                    f"{mode_result['recovery_admit_sequence']}"
                ),
            ]
        )

    lines.extend(
        [
            "",
            "Hardware operations during H5: NONE",
            "Acceptance: PASS",
        ]
    )

    p["h5_txt"].write_text(
        "\n".join(lines) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    print(f"=== CT-W6 / H5 {slug} RESULT ===")
    print("Offline acceptance: PASS")
    print(
        f"W6 admitted/dropped: "
        f"{mode_result['admitted']} / "
        f"{mode_result['drops']}"
    )
    print(
        f"Timing cycles: "
        f"{timing['max_nominal_to_decision_cycles']} / "
        f"{timing['max_nominal_to_irq_exit_cycles']} / "
        f"{timing['max_final_window_cycles']}"
    )
    print(
        f"CT max rx->reply complete: "
        f"{ct_result['max_rx_to_reply_complete_cycles']} cycles"
    )

    if cell["mode"] == "DROP":
        print(
            f"In-window recovery: DROP "
            f"{mode_result['recovery_drop_sequence']} -> "
            f"ADMIT {mode_result['recovery_admit_sequence']} PASS"
        )

    print("Hardware during H5: NONE")


def main() -> int:
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "phase",
        choices=(
            "h2",
            "h3",
            "h4",
            "h5",
        ),
    )
    parser.add_argument(
        "cell",
        choices=SUPPORTED_CELLS,
    )

    args = parser.parse_args()

    common_repo_guard()
    cell = load_cell(
        args.cell
    )

    if args.phase == "h2":
        h2(cell)
    elif args.phase == "h3":
        h3(cell)
    elif args.phase == "h4":
        h4(cell)
    elif args.phase == "h5":
        h5(cell)
    else:
        raise RuntimeError(
            f"unexpected phase: {args.phase}"
        )

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print()
        print("=== CT-W6 MATRIX HARNESS RESULT ===")
        print("Result: FAIL")
        print(str(exc))
        print("No automatic retry was performed.")
        raise SystemExit(1)
