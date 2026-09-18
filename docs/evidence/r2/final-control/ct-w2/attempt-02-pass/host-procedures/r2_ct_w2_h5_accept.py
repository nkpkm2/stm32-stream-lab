#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(r"E:\Projects\stm32-stream-lab")
BUILD = Path(r"E:\Projects\stm32-stream-lab\build\r2-ct-w2-k8-normal-6b43bec8")

EXPECTED_HEAD = "45a5b0b3983d01b59893aea8cfb1b42c4afb2420"
EXPECTED_CT_SOURCE_SHA256 = (
    "630FC34A41DA02CFDB0EC00A12EA576CE8915EF7C87A6F477CB63733311FEAD1"
)
EXPECTED_ELF_SHA256 = (
    "7C2CCA4982D39930BC4FFE31328F985880D4828F0F209C8B150F3A44D7042CFB"
)
EXPECTED_BIN_SHA256 = (
    "C16309F614A5E2B918FFD2B053E45DDAD70BE36B0E3A42E9316DD8C9996696B3"
)

CT_SOURCE = REPO / r"firmware\acquisition\r2_ct_control.c"
ELF = BUILD / "cubemx.elf"
BIN = BUILD / "cubemx.bin"

H2_LOG = BUILD / "hardware-attempt02-flash-verify.txt"
H3_SUMMARY = BUILD / "hardware-attempt02-h3-runtime.json"
H4_SUMMARY = BUILD / "hardware-attempt02-h4-summary.json"
H4_INSPECTION = BUILD / "hardware-attempt02-h4-inspection.txt"

OUT_JSON = BUILD / "hardware-attempt02-h5-acceptance.json"
OUT_TXT = BUILD / "hardware-attempt02-h5-acceptance.txt"

U32_MASK = 0xFFFFFFFF


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def run_text(args: list[str]) -> str:
    result = subprocess.run(
        args,
        cwd=str(REPO),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"Command failed ({result.returncode}): {' '.join(args)}\n"
            f"stdout={result.stdout}\n"
            f"stderr={result.stderr}"
        )
    return result.stdout.strip()


def parse_value(raw: str) -> int:
    raw = raw.strip()
    if raw.lower().startswith("0x"):
        return int(raw, 16)
    return int(raw, 10)


def parse_gdb_kv(path: Path) -> dict[str, int]:
    values: dict[str, int] = {}
    pattern = re.compile(r"^([A-Za-z0-9_]+)=(0x[0-9A-Fa-f]+|[0-9]+)$")

    for raw_line in path.read_text(
        encoding="utf-8",
        errors="replace",
    ).splitlines():
        line = raw_line.strip()
        match = pattern.fullmatch(line)
        if match:
            values[match.group(1)] = parse_value(match.group(2))

    return values


def require(values: dict[str, int], name: str) -> int:
    if name not in values:
        raise AssertionError(f"Missing RAM field: {name}")
    return values[name]


def expect_eq(
    values: dict[str, int],
    name: str,
    expected: int,
    checks: list[str],
) -> None:
    actual = require(values, name)
    if actual != expected:
        raise AssertionError(f"{name}={actual}, expected {expected}")
    checks.append(f"{name}={actual}: PASS")


def expect_zero(
    values: dict[str, int],
    name: str,
    checks: list[str],
) -> None:
    expect_eq(values, name, 0, checks)


def u32_delta(start: int, end: int) -> int:
    return (end - start) & U32_MASK


def main() -> int:
    print("=== CT-W2 ATTEMPT 02 / H5 OFFLINE ACCEPTANCE ===")
    print("Hardware access: NONE")
    print("Flash/reset/PING/GDB: NONE")

    required_paths = (
        CT_SOURCE,
        ELF,
        BIN,
        H2_LOG,
        H3_SUMMARY,
        H4_SUMMARY,
        H4_INSPECTION,
    )
    for path in required_paths:
        if not path.is_file():
            raise SystemExit(f"Missing required evidence/artifact: {path}")

    for path in (OUT_JSON, OUT_TXT):
        if path.exists():
            raise SystemExit(f"Refusing to overwrite existing H5 result: {path}")

    print()
    print("=== 1. SOURCE / ARTIFACT IDENTITY ===")

    head = run_text(["git", "rev-parse", "HEAD"])
    origin = run_text(["git", "rev-parse", "origin/main"])

    if head != EXPECTED_HEAD or origin != EXPECTED_HEAD:
        raise SystemExit(
            f"Repository baseline mismatch: HEAD={head}, origin/main={origin}"
        )

    staged = run_text(["git", "diff", "--cached", "--name-only"])
    if staged:
        raise SystemExit(f"Git index contains staged changes:\n{staged}")

    ct_hash = sha256(CT_SOURCE)
    elf_hash = sha256(ELF)
    bin_hash = sha256(BIN)

    if ct_hash != EXPECTED_CT_SOURCE_SHA256:
        raise SystemExit(f"CT source SHA256 mismatch: {ct_hash}")
    if elf_hash != EXPECTED_ELF_SHA256:
        raise SystemExit(f"ELF SHA256 mismatch: {elf_hash}")
    if bin_hash != EXPECTED_BIN_SHA256:
        raise SystemExit(f"BIN SHA256 mismatch: {bin_hash}")

    h2_text = H2_LOG.read_text(encoding="utf-8", errors="replace")
    if "Download verified successfully" not in h2_text:
        raise SystemExit("H2 flash evidence lacks verification-success marker.")

    print(f"HEAD/origin: {head}")
    print(f"CT source:   {ct_hash}")
    print(f"ELF:         {elf_hash}")
    print(f"BIN:         {bin_hash}")
    print("H2 flash/verify evidence: PASS")

    print()
    print("=== 2. H3 / H4 EVIDENCE CHAIN ===")

    h3 = json.loads(H3_SUMMARY.read_text(encoding="utf-8"))
    h4 = json.loads(H4_SUMMARY.read_text(encoding="utf-8"))

    h3_expected = {
        "result": "PASS",
        "flash_performed": False,
        "debugger_attached": False,
        "explicit_reset": True,
        "host_returncode": 0,
        "request_id": 0x0201,
        "request_payload_bytes": 64,
        "request_bytes": 76,
        "reply_payload_bytes": 12,
        "target_phase": 3,
        "target_k": 8,
        "target_drop_mode": 0,
    }

    for key, expected in h3_expected.items():
        actual = h3.get(key)
        if actual != expected:
            raise SystemExit(
                f"H3 evidence mismatch for {key}: actual={actual!r}, expected={expected!r}"
            )

    h3_input = h3.get("target_w6_input_at_process")
    if not isinstance(h3_input, int) or not (1 <= h3_input < 96):
        raise SystemExit(
            f"H3 target_w6_input_at_process invalid: {h3_input!r}"
        )

    h4_expected = {
        "result": "COLLECTED",
        "h3_result": "PASS",
        "flash_performed": False,
        "reset_performed": False,
        "ping_performed": False,
        "continue_or_restart_performed": False,
        "gdb_returncode": 0,
    }

    for key, expected in h4_expected.items():
        actual = h4.get(key)
        if actual != expected:
            raise SystemExit(
                f"H4 evidence mismatch for {key}: actual={actual!r}, expected={expected!r}"
            )

    if h4.get("h3_target_w6_input_at_process") != h3_input:
        raise SystemExit(
            "H4 does not anchor to the same H3 W6 input count."
        )

    if h4.get("elf_sha256") != EXPECTED_ELF_SHA256:
        raise SystemExit("H4 ELF identity does not match Attempt 02 ELF.")

    print(
        f"H3: PASS, PING at W6 input {h3_input}, "
        f"RTT={h3.get('host_round_trip_ms')} ms"
    )
    print("H4: same-run post-run RAM collection anchor PASS")

    print()
    print("=== 3. TARGET RAM ACCEPTANCE ===")

    v = parse_gdb_kv(H4_INSPECTION)
    checks: list[str] = []

    expect_eq(v, "ct_magic", 0x52324354, checks)
    expect_eq(v, "ct_task_created", 1, checks)
    expect_eq(v, "ct_task_started", 1, checks)
    expect_zero(v, "ct_fault_bits", checks)

    expect_eq(v, "ct_rx_byte_count", 76, checks)
    expect_eq(v, "ct_rx_frame_count", 1, checks)

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
        expect_zero(v, name, checks)

    expect_eq(v, "ct_processed_count", 1, checks)
    expect_eq(v, "ct_processed_while_running_count", 1, checks)
    expect_eq(v, "ct_reply_attempt_count", 1, checks)
    expect_eq(v, "ct_reply_success_count", 1, checks)
    expect_eq(v, "ct_trace_count", 1, checks)

    expect_eq(v, "ct0_request_id", 0x0201, checks)
    expect_eq(v, "ct0_payload_length", 64, checks)
    expect_eq(v, "ct0_w6_phase_at_process", 3, checks)
    expect_eq(v, "ct0_w6_input_at_process", h3_input, checks)
    expect_eq(v, "ct0_processed_while_running", 1, checks)
    expect_eq(v, "ct_first_processed_w6_input", h3_input, checks)
    expect_eq(v, "ct_last_processed_w6_input", h3_input, checks)

    host_crc_text = h3.get("payload_crc32")
    if not isinstance(host_crc_text, str):
        raise SystemExit("H3 payload_crc32 missing or not a string.")
    host_crc = int(host_crc_text, 16)
    expect_eq(v, "ct0_payload_crc32", host_crc, checks)

    rx_cycle = require(v, "ct0_rx_complete_cycle")
    processed_cycle = require(v, "ct0_processed_cycle")
    reply_start_cycle = require(v, "ct0_reply_start_cycle")
    reply_complete_cycle = require(v, "ct0_reply_complete_cycle")

    for name, value in (
        ("ct0_rx_complete_cycle", rx_cycle),
        ("ct0_processed_cycle", processed_cycle),
        ("ct0_reply_start_cycle", reply_start_cycle),
        ("ct0_reply_complete_cycle", reply_complete_cycle),
    ):
        if value == 0:
            raise AssertionError(f"{name}=0, expected nonzero")
        checks.append(f"{name} nonzero: PASS")

    rx_to_processed = u32_delta(rx_cycle, processed_cycle)
    processed_to_start = u32_delta(processed_cycle, reply_start_cycle)
    start_to_complete = u32_delta(reply_start_cycle, reply_complete_cycle)

    # These are ordering/sanity gates, not architecture timing limits.
    # Any stage taking >= 1 second at 180 MHz would be inconsistent with this smoke run.
    for label, delta in (
        ("rx_to_processed_cycles", rx_to_processed),
        ("processed_to_reply_start_cycles", processed_to_start),
        ("reply_start_to_complete_cycles", start_to_complete),
    ):
        if delta == 0 or delta >= 180_000_000:
            raise AssertionError(f"{label}={delta}, ordering/sanity gate failed")
        checks.append(f"{label}={delta}: PASS")

    expect_eq(v, "w6_magic", 0x52325736, checks)
    expect_eq(v, "w6_phase", 5, checks)
    expect_eq(v, "w6_test_pass", 1, checks)
    expect_zero(v, "w6_fault_bits", checks)
    expect_eq(v, "w6_configured_k", 8, checks)
    expect_eq(v, "w6_drop_mode", 0, checks)

    expect_eq(v, "w6_irq_count", 96, checks)
    expect_eq(v, "w6_input_count", 96, checks)
    expect_eq(v, "w6_admitted_count", 96, checks)
    expect_eq(v, "w6_capacity_drop_count", 0, checks)
    expect_eq(v, "w6_processed_count", 96, checks)
    expect_eq(v, "w6_released_count", 96, checks)

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
        expect_zero(v, name, checks)

    expect_eq(v, "w6_full_sample_count", 24576, checks)
    expect_eq(v, "w6_free_queue_depth_final", 8, checks)
    expect_eq(v, "w6_pool_free_count", 8, checks)
    expect_eq(v, "w6_pool_dma_owned_count", 2, checks)

    decision = require(v, "w6_max_nominal_to_decision_cycles")
    irq_exit = require(v, "w6_max_nominal_to_irq_exit_cycles")
    final_window = require(v, "w6_max_final_window_cycles")

    if decision > 57600:
        raise AssertionError(
            f"w6_max_nominal_to_decision_cycles={decision} > 57600"
        )
    checks.append(
        f"w6_max_nominal_to_decision_cycles={decision} <= 57600: PASS"
    )

    if irq_exit > 80640:
        raise AssertionError(
            f"w6_max_nominal_to_irq_exit_cycles={irq_exit} > 80640"
        )
    checks.append(
        f"w6_max_nominal_to_irq_exit_cycles={irq_exit} <= 80640: PASS"
    )

    if final_window > 3600:
        raise AssertionError(
            f"w6_max_final_window_cycles={final_window} > 3600"
        )
    checks.append(
        f"w6_max_final_window_cycles={final_window} <= 3600: PASS"
    )

    result = {
        "result": "PASS",
        "scope": "CT-W2 Attempt 02 one-real-PING hardware smoke",
        "source_head": head,
        "ct_source_sha256": ct_hash,
        "elf_sha256": elf_hash,
        "programmed_sha256": bin_hash,
        "h2_flash_verify": "PASS",
        "h3_runtime": "PASS",
        "h4_ram_collection": "PASS",
        "request_id": h3["request_id"],
        "request_payload_bytes": h3["request_payload_bytes"],
        "payload_crc32": host_crc_text.upper(),
        "host_round_trip_ms": h3.get("host_round_trip_ms"),
        "target_w6_input_at_process": h3_input,
        "ct_reply_success_count": require(v, "ct_reply_success_count"),
        "ct_reply_complete_cycle": reply_complete_cycle,
        "ct_rx_to_processed_cycles": rx_to_processed,
        "ct_processed_to_reply_start_cycles": processed_to_start,
        "ct_reply_start_to_complete_cycles": start_to_complete,
        "w6_irq_count": require(v, "w6_irq_count"),
        "w6_input_count": require(v, "w6_input_count"),
        "w6_admitted_count": require(v, "w6_admitted_count"),
        "w6_capacity_drop_count": require(v, "w6_capacity_drop_count"),
        "w6_processed_count": require(v, "w6_processed_count"),
        "w6_released_count": require(v, "w6_released_count"),
        "w6_max_nominal_to_decision_cycles": decision,
        "w6_max_nominal_to_irq_exit_cycles": irq_exit,
        "w6_max_final_window_cycles": final_window,
        "hardware_operations_during_h5": False,
        "acceptance_claim": (
            "CT-W2 smoke only: one 64-byte PING during K=8 NORMAL W6 run. "
            "This is not the final worst-permitted control-traffic acceptance."
        ),
        "checks": checks,
        "evidence_sha256": {
            H2_LOG.name: sha256(H2_LOG),
            H3_SUMMARY.name: sha256(H3_SUMMARY),
            H4_SUMMARY.name: sha256(H4_SUMMARY),
            H4_INSPECTION.name: sha256(H4_INSPECTION),
        },
    }

    OUT_JSON.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    text_lines = [
        "R2 CT-W2 HARDWARE SMOKE ATTEMPT 02: PASS",
        "",
        f"Source HEAD: {head}",
        f"CT source SHA256: {ct_hash}",
        f"CT-ON ELF SHA256: {elf_hash}",
        f"CT-ON programmed SHA256: {bin_hash}",
        "",
        "H2 flash + verify: PASS",
        "H3 one real 64-byte PING: PASS",
        "H4 same-run post-run RAM collection: PASS",
        "H5 offline acceptance: PASS",
        "",
        f"PING processed at W6 input: {h3_input}",
        f"Host round trip: {h3.get('host_round_trip_ms')} ms",
        f"Payload CRC32: {host_crc_text.upper()}",
        "CT reply attempt/success/error: 1 / 1 / 0",
        f"CT reply complete cycle: {reply_complete_cycle}",
        "",
        "W6 K8 NORMAL: 96 / 96 admitted, 0 drops",
        "W6 processed/released: 96 / 96",
        "W6 test_pass: 1",
        "W6 fault_bits: 0",
        f"W6 max decision cycles: {decision} / 57600",
        f"W6 max IRQ-exit cycles: {irq_exit} / 80640",
        f"W6 max final window cycles: {final_window} / 3600",
        "DMA/ADC/ownership/sample/canary errors: 0",
        "Final pool: 8 FREE + 2 DMA, 0 READY, 0 PROCESSING",
        "",
        "Debugger attached during measurement: NO",
        "H4 debugger action: post-run read-only inspection",
        "Hardware operations during H5: NONE",
        "",
        "SCOPE:",
        "CT-W2 smoke only: one real 64-byte PING during K=8 NORMAL.",
        "Do NOT claim worst-permitted control-traffic acceptance or R2 final closure yet.",
    ]

    OUT_TXT.write_text(
        "\n".join(text_lines) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    print()
    print("=== H5 RESULT ===")
    print("R2 CT-W2 HARDWARE SMOKE ATTEMPT 02: PASS")
    print(f"PING processed at W6 input:       {h3_input}")
    print(f"CT reply attempt/success/error:   1 / 1 / 0")
    print(f"CT reply complete cycle:          {reply_complete_cycle}")
    print("W6 K8 NORMAL:                     96 / 96 admitted, 0 drops")
    print("W6 processed/released:            96 / 96")
    print("W6 test_pass / fault_bits:        1 / 0")
    print(f"W6 max decision cycles:           {decision} / 57600")
    print(f"W6 max IRQ-exit cycles:           {irq_exit} / 80640")
    print(f"W6 max final window cycles:       {final_window} / 3600")
    print("DMA/ADC/ownership/sample/canary:  0 errors")
    print("Final pool:                       8 FREE + 2 DMA")
    print()
    print(f"Acceptance JSON: {OUT_JSON}")
    print(f"Acceptance TXT:  {OUT_TXT}")
    print()
    print("CT-W2 scope: CLOSED / KNOWN-GOOD SMOKE")
    print("R2 final control acceptance: NOT YET COMPLETE")
    print("Hardware operations during H5: NONE")

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as exc:
        print()
        print("=== H5 RESULT ===")
        print("R2 CT-W2 HARDWARE SMOKE ATTEMPT 02: FAIL")
        print(str(exc))
        print("Hardware operations during H5: NONE")
        sys.exit(1)
