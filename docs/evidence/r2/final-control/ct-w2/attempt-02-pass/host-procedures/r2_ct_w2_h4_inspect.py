#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import socket
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(r"E:\Projects\stm32-stream-lab")
BUILD = Path(r"E:\Projects\stm32-stream-lab\build\r2-ct-w2-k8-normal-6b43bec8")

ELF = BUILD / "cubemx.elf"
H3_SUMMARY = BUILD / "hardware-attempt02-h3-runtime.json"

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

EXPECTED_ELF_SHA256 = (
    "7C2CCA4982D39930BC4FFE31328F985880D4828F0F209C8B150F3A44D7042CFB"
)

GDB_PORT = 61234

GDB_SCRIPT = BUILD / "hardware-attempt02-h4-inspection.gdb"
GDB_STDOUT = BUILD / "hardware-attempt02-h4-inspection.txt"
GDB_STDERR = BUILD / "hardware-attempt02-h4-inspection.stderr.txt"
SERVER_STDOUT = BUILD / "hardware-attempt02-h4-gdbserver.stdout.txt"
SERVER_STDERR = BUILD / "hardware-attempt02-h4-gdbserver.stderr.txt"
SUMMARY = BUILD / "hardware-attempt02-h4-summary.json"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def fail(message: str, code: int = 1):
    print()
    print("CT-W2 ATTEMPT 02 / H4: FAIL")
    print(message)
    print("No flash/reset/PING was performed by H4.")
    raise SystemExit(code)


def port_is_free(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            s.bind(("127.0.0.1", port))
        except OSError:
            return False
    return True


def main() -> int:
    print("=== CT-W2 ATTEMPT 02 / H4 POST-RUN RAM INSPECTION ===")

    for path in (ELF, H3_SUMMARY, GDB, GDB_SERVER, CUBE_PROGRAMMER_BIN):
        if not path.exists():
            fail(f"Required path missing: {path}")

    for path in (
        GDB_SCRIPT,
        GDB_STDOUT,
        GDB_STDERR,
        SERVER_STDOUT,
        SERVER_STDERR,
        SUMMARY,
    ):
        if path.exists():
            fail(f"Refusing to overwrite existing H4 evidence: {path}")

    elf_hash = sha256(ELF)
    print(f"ELF SHA256: {elf_hash}")

    if elf_hash != EXPECTED_ELF_SHA256:
        fail("ELF identity mismatch.")

    try:
        h3 = json.loads(H3_SUMMARY.read_text(encoding="utf-8"))
    except Exception as exc:
        fail(f"Could not parse H3 summary: {exc}")

    if h3.get("result") != "PASS":
        fail("H3 summary does not report PASS.")

    if h3.get("flash_performed") is not False:
        fail("Unexpected H3 summary: flash_performed is not false.")

    if h3.get("debugger_attached") is not False:
        fail("Unexpected H3 summary: debugger_attached is not false.")

    print(
        "H3 anchor: PASS "
        f"(W6 input={h3.get('target_w6_input_at_process')}, "
        f"RTT={h3.get('host_round_trip_ms')} ms)"
    )

    if not port_is_free(GDB_PORT):
        fail(f"TCP port {GDB_PORT} is already in use.")

    gdb_text = r'''set pagination off
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

printf "ct0_request_id=%u\n", (unsigned int)g_r2_ct_result.trace[0].request_id
printf "ct0_payload_length=%u\n", (unsigned int)g_r2_ct_result.trace[0].payload_length
printf "ct0_rx_complete_cycle=%u\n", (unsigned int)g_r2_ct_result.trace[0].rx_complete_cycle
printf "ct0_processed_cycle=%u\n", (unsigned int)g_r2_ct_result.trace[0].processed_cycle
printf "ct0_reply_start_cycle=%u\n", (unsigned int)g_r2_ct_result.trace[0].reply_start_cycle
printf "ct0_reply_complete_cycle=%u\n", (unsigned int)g_r2_ct_result.trace[0].reply_complete_cycle
printf "ct0_w6_input_at_process=%u\n", (unsigned int)g_r2_ct_result.trace[0].w6_input_at_process
printf "ct0_w6_phase_at_process=%u\n", (unsigned int)g_r2_ct_result.trace[0].w6_phase_at_process
printf "ct0_payload_crc32=0x%08x\n", (unsigned int)g_r2_ct_result.trace[0].payload_crc32
printf "ct0_processed_while_running=%u\n", (unsigned int)g_r2_ct_result.trace[0].processed_while_running

printf "w6_magic=0x%08x\n", (unsigned int)g_r2_w6_result.magic
printf "w6_phase=%u\n", (unsigned int)g_r2_w6_result.phase
printf "w6_test_pass=%u\n", (unsigned int)g_r2_w6_result.test_pass
printf "w6_fault_bits=0x%08x\n", (unsigned int)g_r2_w6_result.fault_bits
printf "w6_configured_k=%u\n", (unsigned int)g_r2_w6_result.configured_k
printf "w6_drop_mode=%u\n", (unsigned int)g_r2_w6_result.drop_mode
printf "w6_irq_count=%u\n", (unsigned int)g_r2_w6_result.irq_count
printf "w6_input_count=%u\n", (unsigned int)g_r2_w6_result.input_count
printf "w6_admitted_count=%u\n", (unsigned int)g_r2_w6_result.admitted_count
printf "w6_capacity_drop_count=%u\n", (unsigned int)g_r2_w6_result.capacity_drop_count
printf "w6_processed_count=%u\n", (unsigned int)g_r2_w6_result.processed_count
printf "w6_released_count=%u\n", (unsigned int)g_r2_w6_result.released_count
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
printf "w6_pool_free_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.free_count
printf "w6_pool_dma_owned_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.dma_owned_count
printf "w6_pool_ready_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.ready_count
printf "w6_pool_processing_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.processing_count
printf "w6_pool_violation_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.violation_count
printf "w6_slots_violation_count=%u\n", (unsigned int)g_r2_w6_result.slots_at_stop.violation_count

detach
quit
'''

    GDB_SCRIPT.write_text(gdb_text, encoding="utf-8", newline="\n")

    print()
    print("=== START POST-RUN GDB SERVER ===")
    print("Flash:    NO")
    print("Reset:    NO")
    print("Continue: NO")
    print("PING:     NO")

    with SERVER_STDOUT.open("w", encoding="utf-8", newline="\n") as server_out, \
         SERVER_STDERR.open("w", encoding="utf-8", newline="\n") as server_err:

        server = subprocess.Popen(
            [
                str(GDB_SERVER),
                "-p", str(GDB_PORT),
                "-d",
                "-g",
                "-l", "1",
                "-cp", str(CUBE_PROGRAMMER_BIN),
            ],
            stdout=server_out,
            stderr=server_err,
            cwd=str(REPO),
            text=True,
        )

        try:
            time.sleep(1.2)

            if server.poll() is not None:
                stdout_text = SERVER_STDOUT.read_text(
                    encoding="utf-8", errors="replace"
                )
                stderr_text = SERVER_STDERR.read_text(
                    encoding="utf-8", errors="replace"
                )
                fail(
                    "ST-LINK GDB server exited before inspection.\n"
                    f"returncode={server.returncode}\n"
                    f"stdout={stdout_text}\n"
                    f"stderr={stderr_text}"
                )

            print("GDB server: READY")

            print()
            print("=== READ EXISTING RAM ===")

            inspection = subprocess.run(
                [
                    str(GDB),
                    "--batch",
                    "--quiet",
                    str(ELF),
                    "-x",
                    str(GDB_SCRIPT),
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=20,
                cwd=str(REPO),
            )

            GDB_STDOUT.write_text(
                inspection.stdout or "",
                encoding="utf-8",
                newline="\n",
            )
            GDB_STDERR.write_text(
                inspection.stderr or "",
                encoding="utf-8",
                newline="\n",
            )

            print(f"GDB return code: {inspection.returncode}")

            if inspection.returncode != 0:
                fail(
                    "GDB inspection failed.\n"
                    f"stdout={inspection.stdout}\n"
                    f"stderr={inspection.stderr}"
                )

        finally:
            if server.poll() is None:
                server.terminate()
                try:
                    server.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait(timeout=3)

    inspection_text = GDB_STDOUT.read_text(
        encoding="utf-8", errors="replace"
    )

    required_markers = (
        "ct_magic=",
        "ct_reply_success_count=",
        "ct0_reply_complete_cycle=",
        "w6_magic=",
        "w6_phase=",
        "w6_test_pass=",
    )

    missing = [m for m in required_markers if m not in inspection_text]
    if missing:
        fail(f"GDB output is missing expected fields: {missing}")

    summary = {
        "result": "COLLECTED",
        "scope": "CT-W2 Attempt 02 H4 post-run RAM inspection only",
        "elf_sha256": elf_hash,
        "h3_result": h3.get("result"),
        "h3_target_w6_input_at_process": h3.get("target_w6_input_at_process"),
        "flash_performed": False,
        "reset_performed": False,
        "ping_performed": False,
        "continue_or_restart_performed": False,
        "gdb_returncode": inspection.returncode,
        "inspection_file": str(GDB_STDOUT),
        "inspection_stderr_file": str(GDB_STDERR),
        "gdbserver_stdout_file": str(SERVER_STDOUT),
        "gdbserver_stderr_file": str(SERVER_STDERR),
    }

    SUMMARY.write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    print()
    print("=== H4 RESULT ===")
    print("CT-W2 Attempt 02 RAM inspection: COLLECTED")
    print("Flash performed:                 NO")
    print("Reset performed:                 NO")
    print("PING performed:                  NO")
    print("Continue/restart performed:      NO")
    print(f"Inspection evidence:             {GDB_STDOUT}")
    print(f"H4 summary:                      {SUMMARY}")
    print()
    print("Acceptance evaluation:           NOT RUN")

    return 0


if __name__ == "__main__":
    sys.exit(main())
