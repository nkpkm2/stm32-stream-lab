#!/usr/bin/env python3
"""CT-W5 H4 same-run post-run RAM collector.

Read-only collection for the already-completed CT-W5 H3 run.

Allowed:
- start ST-LINK GDB server without reset
- attach/halt completed target
- read RAM/symbols
- detach
- write host-side evidence files

Forbidden:
- flash
- reset
- continue/restart
- serial traffic
- acceptance decision
- Git modification
"""

from __future__ import annotations

import hashlib
import json
import subprocess
import time
from pathlib import Path


REPO = Path(r"E:\Projects\stm32-stream-lab")
BUILD = REPO / r"build\r2-ct-w5-k1-drop-800-t1500-b3aa0b92"

CHECKPOINT = "b3aa0b926a49182e99095241a7939195073aa545"
EXPECTED_ELF = (
    "7736852DFAE9254055ADA8D1EF8DDE36AF09B478BB658AF9526D7143C51367C3"
)

ELF = BUILD / "cubemx.elf"
H3 = BUILD / "hardware-attempt01-h3-runtime.json"

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

GDB_SCRIPT = BUILD / "hardware-attempt01-h4-inspection.gdb"
INSPECTION = BUILD / "hardware-attempt01-h4-inspection.txt"
INSPECTION_ERR = BUILD / "hardware-attempt01-h4-inspection.stderr.txt"
SERVER_OUT = BUILD / "hardware-attempt01-h4-gdbserver.stdout.txt"
SERVER_ERR = BUILD / "hardware-attempt01-h4-gdbserver.stderr.txt"
SUMMARY = BUILD / "hardware-attempt01-h4-collection.json"

PORT = 61234


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
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def require_file(path: Path) -> None:
    if not path.is_file():
        raise RuntimeError(f"required file missing: {path}")


def build_gdb_script() -> str:
    return r"""set pagination off
set confirm off
set print pretty off

target remote 127.0.0.1:61234

printf "=== CT SUMMARY ===\n"
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

printf "=== CT TRACE ===\n"
set $i = 0
while $i < 10
  printf "CTTRACE,%u,%u,%u,%u,%u,%u,%u,%u,0x%08x,%u\n", $i, (unsigned int)g_r2_ct_result.trace[$i].request_id, (unsigned int)g_r2_ct_result.trace[$i].payload_length, (unsigned int)g_r2_ct_result.trace[$i].rx_complete_cycle, (unsigned int)g_r2_ct_result.trace[$i].processed_cycle, (unsigned int)g_r2_ct_result.trace[$i].reply_start_cycle, (unsigned int)g_r2_ct_result.trace[$i].reply_complete_cycle, (unsigned int)g_r2_ct_result.trace[$i].w6_input_at_process, (unsigned int)g_r2_ct_result.trace[$i].payload_crc32, (unsigned int)g_r2_ct_result.trace[$i].processed_while_running
  set $i = $i + 1
end

printf "=== W6 SUMMARY ===\n"
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

printf "=== W6 TRACE 800 ===\n"
printf "W6TRACE_HEADER,index,sequence,decision,completed_id,replacement_id,mapping_epoch_after,m0_before,m0_after,m1_before,m1_after,free_depth_after_take,ready_depth_after_publish,processed_ok,nominal_to_decision_cycles,final_window_cycles\n"
set $i = 0
while $i < 800
  printf "W6TRACE,%u,%u,%u,%u,%u,%u,0x%08x,0x%08x,0x%08x,0x%08x,%u,%u,%u,%u,%u\n", $i, (unsigned int)g_r2_w6_result.trace[$i].sequence, (unsigned int)g_r2_w6_result.trace[$i].decision, (unsigned int)g_r2_w6_result.trace[$i].completed_id, (unsigned int)g_r2_w6_result.trace[$i].replacement_id, (unsigned int)g_r2_w6_result.trace[$i].mapping_epoch_after, (unsigned int)g_r2_w6_result.trace[$i].m0_before, (unsigned int)g_r2_w6_result.trace[$i].m0_after, (unsigned int)g_r2_w6_result.trace[$i].m1_before, (unsigned int)g_r2_w6_result.trace[$i].m1_after, (unsigned int)g_r2_w6_result.trace[$i].free_depth_after_take, (unsigned int)g_r2_w6_result.trace[$i].ready_depth_after_publish, (unsigned int)g_r2_w6_result.trace[$i].processed_ok, (unsigned int)g_r2_w6_result.trace[$i].nominal_to_decision_cycles, (unsigned int)g_r2_w6_result.trace[$i].final_window_cycles
  set $i = $i + 1
end

detach
quit
"""


def main() -> int:
    print("=== CT-W5 / H4 SAME-RUN POST-RUN RAM COLLECTION ===")
    print("Flash: NO")
    print("Reset: NO")
    print("Serial traffic: NO")
    print("Continue/restart: NO")
    print("Acceptance evaluation: NO")

    if git("rev-parse", "HEAD") != CHECKPOINT:
        raise RuntimeError("HEAD is not the CT-W5 checkpoint")

    if git("rev-parse", "origin/main") != CHECKPOINT:
        raise RuntimeError("origin/main is not the CT-W5 checkpoint")

    if git("status", "--porcelain=v1", "-uall"):
        raise RuntimeError("working tree is not clean")

    for path in (ELF, H3, GDB, GDB_SERVER):
        require_file(path)

    if not CUBE_PROGRAMMER_BIN.is_dir():
        raise RuntimeError(
            f"CubeProgrammer support directory missing: {CUBE_PROGRAMMER_BIN}"
        )

    if sha256(ELF) != EXPECTED_ELF:
        raise RuntimeError("CT-W5 ELF identity mismatch")

    h3 = json.loads(H3.read_text(encoding="utf-8"))

    if h3.get("result") != "PASS":
        raise RuntimeError("CT-W5 H3 does not report PASS")

    profile = h3.get("profile", {})
    if (
        profile.get("k") != 1
        or profile.get("mode") != "DROP"
        or profile.get("events") != 800
        or profile.get("run_timeout_ms") != 1500
    ):
        raise RuntimeError("CT-W5 H3 profile mismatch")

    if h3.get("request_count") != 10:
        raise RuntimeError("CT-W5 H3 request count is not 10")

    if h3.get("burst_bytes") != 760:
        raise RuntimeError("CT-W5 H3 burst size is not 760")

    if h3.get("single_write_call") is not True:
        raise RuntimeError("CT-W5 H3 was not a single-write burst")

    if h3.get("debugger_attached") is not False:
        raise RuntimeError("H3 debugger flag is not false")

    if h3.get("flash_performed") is not False:
        raise RuntimeError("H3 flash flag is not false")

    print(
        "H3 same-run anchor: PASS "
        f"(W6 inputs {h3['first_w6_input']} -> {h3['last_w6_input']})"
    )

    for path in (
        GDB_SCRIPT,
        INSPECTION,
        INSPECTION_ERR,
        SERVER_OUT,
        SERVER_ERR,
        SUMMARY,
    ):
        if path.exists():
            raise RuntimeError(
                f"refusing to overwrite H4 evidence: {path}"
            )

    GDB_SCRIPT.write_text(
        build_gdb_script(),
        encoding="utf-8",
        newline="\n",
    )

    server_out_handle = SERVER_OUT.open(
        "w",
        encoding="utf-8",
        newline="",
    )
    server_err_handle = SERVER_ERR.open(
        "w",
        encoding="utf-8",
        newline="",
    )

    server = subprocess.Popen(
        [
            str(GDB_SERVER),
            "-p",
            str(PORT),
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
                f"GDB server exited early with code {server.returncode}"
            )

        print("GDB server: READY")

        with INSPECTION.open(
            "w",
            encoding="utf-8",
            newline="",
        ) as out_handle, INSPECTION_ERR.open(
            "w",
            encoding="utf-8",
            newline="",
        ) as err_handle:
            gdb = subprocess.run(
                [
                    str(GDB),
                    "--batch",
                    "--quiet",
                    str(ELF),
                    "-x",
                    str(GDB_SCRIPT),
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

    text = INSPECTION.read_text(
        encoding="utf-8",
        errors="replace",
    )

    for marker in (
        "ct_magic=",
        "ct_trace_count=",
        "w6_magic=",
        "w6_phase=",
        "w6_input_count=",
        "w6_capacity_drop_count=",
        "w6_recovered_admission_after_drop_count=",
        "W6TRACE_HEADER,",
    ):
        if marker not in text:
            raise RuntimeError(
                f"inspection output missing marker: {marker}"
            )

    ct_trace_lines = [
        line
        for line in text.splitlines()
        if line.startswith("CTTRACE,")
    ]

    w6_trace_lines = [
        line
        for line in text.splitlines()
        if line.startswith("W6TRACE,")
    ]

    if len(ct_trace_lines) != 10:
        raise RuntimeError(
            f"expected 10 CTTRACE lines, found {len(ct_trace_lines)}"
        )

    if len(w6_trace_lines) != 800:
        raise RuntimeError(
            f"expected 800 W6TRACE lines, found {len(w6_trace_lines)}"
        )

    sequences: list[int] = []

    for expected_index, line in enumerate(w6_trace_lines):
        parts = line.split(",")

        if len(parts) != 16:
            raise RuntimeError(
                f"malformed W6TRACE line {expected_index}: {line}"
            )

        index = int(parts[1], 10)
        sequence = int(parts[2], 10)

        if index != expected_index:
            raise RuntimeError(
                f"W6TRACE index discontinuity: {index} != {expected_index}"
            )

        sequences.append(sequence)

    if sequences != list(range(1, 801)):
        raise RuntimeError(
            "W6TRACE sequence is not exactly 1..800"
        )

    summary = {
        "result": "COLLECTED",
        "attempt": 1,
        "scope": "CT-W5 H4 same-run post-run RAM collection",
        "checkpoint": CHECKPOINT,
        "h3_first_w6_input": int(h3["first_w6_input"]),
        "h3_last_w6_input": int(h3["last_w6_input"]),
        "ct_trace_lines": len(ct_trace_lines),
        "w6_trace_lines": len(w6_trace_lines),
        "w6_sequence_first": sequences[0],
        "w6_sequence_last": sequences[-1],
        "flash_performed": False,
        "reset_performed": False,
        "serial_traffic_performed": False,
        "continue_or_restart_performed": False,
        "acceptance_evaluation_performed": False,
        "inspection_sha256": sha256(INSPECTION),
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

    if git("status", "--porcelain=v1", "-uall"):
        raise RuntimeError(
            "repository changed during H4 collection"
        )

    print()
    print("=== CT-W5 H4 RESULT ===")
    print("RAM inspection:        COLLECTED")
    print("Same-run H3 anchor:    PASS")
    print("CT trace entries:      10 / 10 COLLECTED")
    print("W6 trace entries:      800 / 800 COLLECTED")
    print("W6 sequence:           1 -> 800")
    print(
        "Control input window:  "
        f"{h3['first_w6_input']} -> {h3['last_w6_input']}"
    )
    print(f"Inspection:            {INSPECTION}")
    print(f"Collection JSON:       {SUMMARY}")
    print()
    print("Flash:                 NOT PERFORMED")
    print("Reset:                 NOT PERFORMED")
    print("Serial traffic:        NOT PERFORMED")
    print("Continue/restart:      NOT PERFORMED")
    print("Acceptance:            NOT RUN")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print()
        print("=== CT-W5 H4 RESULT ===")
        print("RAM inspection: FAIL")
        print(str(exc))
        print("No automatic retry was performed.")
        raise SystemExit(1)
