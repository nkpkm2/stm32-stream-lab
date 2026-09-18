#!/usr/bin/env python3
"""CT-W5 H5 offline acceptance.

Consumes only already-created H2/H3/H4 evidence and committed artifacts.
No hardware access, no GDB server, no serial, no reset, no flash.

Architecture-level purpose:
- validate the real adverse burst control path,
- validate the complete K=1/DROP 800-event run,
- validate per-event DROP/ADMIT invariants,
- prove a controlled DROP followed by later ADMIT occurs inside the
  guaranteed event interval between the first and last CT processing points.
"""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
import zlib
from pathlib import Path


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

ELF = BUILD / "cubemx.elf"
BIN = BUILD / "cubemx.bin"
BUILD_GATE = BUILD / "ct-w5-build-gate.json"
HOST_TOOL = REPO / r"tools\r2\r2_ct_burst.py"

H2_STDOUT = BUILD / "hardware-attempt01-flash-verify.stdout.txt"
H2_STDERR = BUILD / "hardware-attempt01-flash-verify.stderr.txt"

H3_JSON = BUILD / "hardware-attempt01-h3-runtime.json"
H3_STDOUT = BUILD / "hardware-attempt01-burst.stdout.txt"
H3_STDERR = BUILD / "hardware-attempt01-burst.stderr.txt"

H4_JSON = BUILD / "hardware-attempt01-h4-collection.json"
H4_INSPECTION = BUILD / "hardware-attempt01-h4-inspection.txt"
H4_INSPECTION_ERR = BUILD / "hardware-attempt01-h4-inspection.stderr.txt"

OUT_JSON = BUILD / "hardware-attempt01-h5-acceptance.json"
OUT_TXT = BUILD / "hardware-attempt01-h5-acceptance.txt"

CT_MAGIC = 0x52324354
W6_MAGIC = 0x52325736

W6_PHASE_COMPLETE = 5
W6_EVENTS = 800
BLOCK_SAMPLES = 256

MIN_ADMISSIONS = 4
MIN_DROPS = 8
MIN_DROP_STREAK = 3
MIN_RECOVERIES = 3

DECISION_LIMIT = 57600
IRQ_EXIT_LIMIT = 80640
FINAL_WINDOW_LIMIT = 3600

U32_MASK = 0xFFFFFFFF


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


def parse_int(raw: str) -> int:
    text = raw.strip()
    return int(text, 16) if text.lower().startswith("0x") else int(text, 10)


def parse_key_values(text: str) -> dict[str, int]:
    result: dict[str, int] = {}
    pattern = re.compile(
        r"^([A-Za-z0-9_]+)=(0x[0-9A-Fa-f]+|[0-9]+)$"
    )

    for raw in text.splitlines():
        match = pattern.fullmatch(raw.strip())
        if match:
            result[match.group(1)] = parse_int(match.group(2))

    return result


def require_value(values: dict[str, int], name: str) -> int:
    if name not in values:
        raise AssertionError(f"missing inspection field: {name}")
    return values[name]


def expect(values: dict[str, int], name: str, expected: int) -> None:
    actual = require_value(values, name)
    if actual != expected:
        raise AssertionError(
            f"{name}={actual}, expected {expected}"
        )


def expect_zero(values: dict[str, int], name: str) -> None:
    expect(values, name, 0)


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


def u32_delta(start: int, end: int) -> int:
    return (end - start) & U32_MASK


def expected_payload(request_id: int) -> bytes:
    return bytes(
        ((request_id + i * 17 + 0x5A) & 0xFF)
        for i in range(64)
    )


def derive_decision_codes(
    rows: list[dict[str, int]],
) -> tuple[int, int]:
    """Derive ADMIT/DROP codes from completed-run trace semantics.

    In a fault-free completed W6 run, admitted events have processed_ok=1
    and controlled drops have processed_ok=0. Require each class to map to
    exactly one decision code and require the two codes to differ.
    """
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
            f"could not uniquely derive ADMIT decision code: {admit_codes}"
        )

    if len(drop_codes) != 1:
        raise AssertionError(
            f"could not uniquely derive DROP decision code: {drop_codes}"
        )

    admit = next(iter(admit_codes))
    drop = next(iter(drop_codes))

    if admit == drop:
        raise AssertionError(
            "ADMIT and DROP decision codes unexpectedly identical"
        )

    return admit, drop


def reconstruct_drop_accounting(
    rows: list[dict[str, int]],
    *,
    admit_code: int,
    drop_code: int,
) -> dict[str, int]:
    current_streak = 0
    max_streak = 0
    recoveries = 0
    admits = 0
    drops = 0

    for row in rows:
        decision = row["decision"]

        if decision == drop_code:
            drops += 1
            current_streak += 1
            max_streak = max(max_streak, current_streak)

        elif decision == admit_code:
            admits += 1
            if current_streak != 0:
                recoveries += 1
                current_streak = 0

        else:
            raise AssertionError(
                f"unexpected decision code {decision} "
                f"at sequence {row['sequence']}"
            )

    return {
        "admitted": admits,
        "drops": drops,
        "recoveries": recoveries,
        "max_drop_streak": max_streak,
        "current_drop_streak": current_streak,
    }


def find_window_recovery(
    rows: list[dict[str, int]],
    *,
    start_sequence: int,
    end_sequence: int,
    admit_code: int,
    drop_code: int,
) -> tuple[list[dict[str, int]], tuple[int, int] | None]:
    window = [
        row
        for row in rows
        if start_sequence <= row["sequence"] <= end_sequence
    ]

    first_drop: int | None = None

    for row in window:
        if row["decision"] == drop_code:
            if first_drop is None:
                first_drop = row["sequence"]

        elif (
            row["decision"] == admit_code
            and first_drop is not None
            and row["sequence"] > first_drop
        ):
            return window, (first_drop, row["sequence"])

    return window, None


def main() -> int:
    print("=== CT-W5 / H5 OFFLINE ARCHITECTURE ACCEPTANCE ===")
    print("Hardware access: NONE")
    print("Flash/reset/serial/GDB-server: NONE")

    required = (
        ELF,
        BIN,
        BUILD_GATE,
        HOST_TOOL,
        H2_STDOUT,
        H2_STDERR,
        H3_JSON,
        H3_STDOUT,
        H3_STDERR,
        H4_JSON,
        H4_INSPECTION,
        H4_INSPECTION_ERR,
    )

    for path in required:
        require_file(path)

    for output in (OUT_JSON, OUT_TXT):
        if output.exists():
            raise RuntimeError(
                f"refusing to overwrite H5 result: {output}"
            )

    # ------------------------------------------------------------
    # 1. Provenance and immutable evidence anchors
    # ------------------------------------------------------------

    if git("rev-parse", "HEAD") != CHECKPOINT:
        raise AssertionError("HEAD is not the CT-W5 checkpoint")

    if git("rev-parse", "origin/main") != CHECKPOINT:
        raise AssertionError("origin/main is not the CT-W5 checkpoint")

    if git("status", "--porcelain=v1", "-uall"):
        raise AssertionError("working tree is not clean")

    if git("tag", "--list", "r2-pass"):
        raise AssertionError("r2-pass exists unexpectedly")

    elf_hash = sha256(ELF)
    bin_hash = sha256(BIN)
    host_hash = sha256(HOST_TOOL)

    if elf_hash != EXPECTED_ELF:
        raise AssertionError(f"ELF identity mismatch: {elf_hash}")

    if bin_hash != EXPECTED_BIN:
        raise AssertionError(f"programmed identity mismatch: {bin_hash}")

    if host_hash != EXPECTED_HOST:
        raise AssertionError(f"host tool identity mismatch: {host_hash}")

    build_gate = json.loads(
        BUILD_GATE.read_text(encoding="utf-8")
    )

    if build_gate.get("result") != "PASS":
        raise AssertionError("build gate does not report PASS")

    gate_profile = build_gate.get("profile", {})
    expected_profile = {
        "k": 1,
        "mode": "DROP",
        "events": 800,
        "run_timeout_ms": 1500,
        "process_hold_blocks": 6,
    }

    for key, expected in expected_profile.items():
        if gate_profile.get(key) != expected:
            raise AssertionError(
                f"build profile mismatch for {key}: "
                f"{gate_profile.get(key)!r} != {expected!r}"
            )

    h2_text = (
        H2_STDOUT.read_text(encoding="utf-8", errors="replace")
        + "\n"
        + H2_STDERR.read_text(encoding="utf-8", errors="replace")
    )

    if "Download verified successfully" not in h2_text:
        raise AssertionError("H2 verify-success marker missing")

    print("Provenance/artifact/H2 anchors: PASS")

    # ------------------------------------------------------------
    # 2. H3 real adverse-burst evidence
    # ------------------------------------------------------------

    h3 = json.loads(H3_JSON.read_text(encoding="utf-8"))
    raw_host = json.loads(
        H3_STDOUT.read_text(
            encoding="utf-8",
            errors="replace",
        ).strip()
    )
    host_stderr = H3_STDERR.read_text(
        encoding="utf-8",
        errors="replace",
    ).strip()

    if host_stderr:
        raise AssertionError(
            f"H3 host stderr is non-empty: {host_stderr}"
        )

    if h3.get("result") != "PASS":
        raise AssertionError("H3 summary does not report PASS")

    if raw_host.get("result") != "PASS":
        raise AssertionError("raw burst host does not report PASS")

    if h3.get("request_count") != 10:
        raise AssertionError("H3 request count != 10")

    for obj, label in ((h3, "H3"), (raw_host, "raw host")):
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
            if obj.get(key) != expected:
                raise AssertionError(
                    f"{label} burst shape mismatch: "
                    f"{key}={obj.get(key)!r}, expected {expected!r}"
                )

    records = h3.get("records")
    raw_records = raw_host.get("records")

    if not isinstance(records, list) or len(records) != 10:
        raise AssertionError("H3 must contain exactly 10 records")

    if records != raw_records:
        raise AssertionError(
            "H3 summary records differ from raw burst-host records"
        )

    expected_ids = list(range(0x0500, 0x050A))
    actual_ids = [
        int(record["request_id"])
        for record in records
    ]

    if actual_ids != expected_ids:
        raise AssertionError(
            f"H3 request ID sequence mismatch: {actual_ids}"
        )

    host_inputs = [
        int(record["target_w6_input_at_process"])
        for record in records
    ]

    if any(
        later <= earlier
        for earlier, later in zip(host_inputs, host_inputs[1:])
    ):
        raise AssertionError(
            f"H3 W6 input observations are not strictly increasing: "
            f"{host_inputs}"
        )

    for index, record in enumerate(records):
        if int(record["target_phase"]) != 3:
            raise AssertionError(
                f"H3 record {index} was not processed while RUNNING"
            )
        if int(record["target_k"]) != 1:
            raise AssertionError(
                f"H3 record {index} target K != 1"
            )
        if int(record["target_drop_mode"]) != 1:
            raise AssertionError(
                f"H3 record {index} target mode != DROP"
            )

    if int(h3["first_w6_input"]) != host_inputs[0]:
        raise AssertionError("H3 first_w6_input mismatch")

    if int(h3["last_w6_input"]) != host_inputs[-1]:
        raise AssertionError("H3 last_w6_input mismatch")

    print("H3 adverse-burst host evidence: PASS")
    print(
        f"H3 W6 input observations: {host_inputs[0]} -> {host_inputs[-1]}"
    )

    # ------------------------------------------------------------
    # 3. H4 collection integrity and target CT state
    # ------------------------------------------------------------

    h4 = json.loads(H4_JSON.read_text(encoding="utf-8"))

    if h4.get("result") != "COLLECTED":
        raise AssertionError("H4 collection does not report COLLECTED")

    if int(h4.get("ct_trace_lines", -1)) != 10:
        raise AssertionError("H4 CT trace line count != 10")

    if int(h4.get("w6_trace_lines", -1)) != 800:
        raise AssertionError("H4 W6 trace line count != 800")

    if int(h4.get("h3_first_w6_input", -1)) != host_inputs[0]:
        raise AssertionError("H4/H3 first input anchor mismatch")

    if int(h4.get("h3_last_w6_input", -1)) != host_inputs[-1]:
        raise AssertionError("H4/H3 last input anchor mismatch")

    inspection_hash = sha256(H4_INSPECTION)

    if inspection_hash != h4.get("inspection_sha256"):
        raise AssertionError(
            "H4 inspection hash does not match H4 collection JSON"
        )

    inspection = H4_INSPECTION.read_text(
        encoding="utf-8",
        errors="replace",
    )
    values = parse_key_values(inspection)
    ct_rows = parse_ct_trace(inspection)
    w6_rows = parse_w6_trace(inspection)

    if len(ct_rows) != 10:
        raise AssertionError(
            f"parsed CT trace count={len(ct_rows)}, expected 10"
        )

    if len(w6_rows) != 800:
        raise AssertionError(
            f"parsed W6 trace count={len(w6_rows)}, expected 800"
        )

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
    expect(values, "ct_first_processed_w6_input", host_inputs[0])
    expect(values, "ct_last_processed_w6_input", host_inputs[-1])

    max_rx_to_process = 0
    max_process_to_tx = 0
    max_tx = 0
    max_rx_to_done = 0

    for index, row in enumerate(ct_rows):
        if row["index"] != index:
            raise AssertionError(
                f"CT trace index discontinuity at {index}"
            )

        if row["request_id"] != expected_ids[index]:
            raise AssertionError(
                f"CT trace request ID mismatch at index {index}"
            )

        if row["payload_length"] != 64:
            raise AssertionError(
                f"CT trace payload length != 64 at index {index}"
            )

        if row["w6_input"] != host_inputs[index]:
            raise AssertionError(
                f"CT/H3 W6-input mismatch at index {index}: "
                f"{row['w6_input']} != {host_inputs[index]}"
            )

        if row["processed_while_running"] != 1:
            raise AssertionError(
                f"CT trace {index} not marked processed while RUNNING"
            )

        expected_crc = (
            zlib.crc32(expected_payload(expected_ids[index]))
            & U32_MASK
        )

        if row["payload_crc32"] != expected_crc:
            raise AssertionError(
                f"CT payload CRC mismatch at index {index}"
            )

        host_crc_raw = records[index]["payload_crc32"]
        host_crc = (
            int(host_crc_raw, 16)
            if isinstance(host_crc_raw, str)
            else int(host_crc_raw)
        )

        if host_crc != expected_crc:
            raise AssertionError(
                f"H3 host payload CRC mismatch at index {index}"
            )

        rx = row["rx_complete_cycle"]
        proc = row["processed_cycle"]
        tx_start = row["reply_start_cycle"]
        done = row["reply_complete_cycle"]

        if 0 in (rx, proc, tx_start, done):
            raise AssertionError(
                f"CT trace {index} has zero service timestamp"
            )

        d1 = u32_delta(rx, proc)
        d2 = u32_delta(proc, tx_start)
        d3 = u32_delta(tx_start, done)
        d4 = u32_delta(rx, done)

        for delta in (d1, d2, d3, d4):
            if delta >= 180_000_000:
                raise AssertionError(
                    f"CT trace {index} timestamp ordering/sanity failed"
                )

        max_rx_to_process = max(max_rx_to_process, d1)
        max_process_to_tx = max(max_process_to_tx, d2)
        max_tx = max(max_tx, d3)
        max_rx_to_done = max(max_rx_to_done, d4)

    print("Target CT counters/trace integrity: PASS")

    # ------------------------------------------------------------
    # 4. W6 completed-run summary acceptance
    # ------------------------------------------------------------

    expect(values, "w6_magic", W6_MAGIC)
    expect(values, "w6_phase", W6_PHASE_COMPLETE)
    expect(values, "w6_test_pass", 1)
    expect_zero(values, "w6_fault_bits")
    expect(values, "w6_configured_k", 1)
    expect(values, "w6_drop_mode", 1)
    expect(values, "w6_process_hold_blocks", 6)
    expect(values, "w6_irq_count", W6_EVENTS)
    expect(values, "w6_input_count", W6_EVENTS)

    admitted = require_value(values, "w6_admitted_count")
    drops = require_value(values, "w6_capacity_drop_count")
    processed = require_value(values, "w6_processed_count")
    released = require_value(values, "w6_released_count")
    recoveries = require_value(
        values,
        "w6_recovered_admission_after_drop_count",
    )
    current_streak = require_value(
        values,
        "w6_current_drop_streak",
    )
    max_streak = require_value(
        values,
        "w6_max_drop_streak",
    )

    if admitted + drops != W6_EVENTS:
        raise AssertionError(
            f"W6 accounting failed: admitted+drops="
            f"{admitted + drops}, expected {W6_EVENTS}"
        )

    if admitted < MIN_ADMISSIONS:
        raise AssertionError(
            f"W6 admissions {admitted} < {MIN_ADMISSIONS}"
        )

    if drops < MIN_DROPS:
        raise AssertionError(
            f"W6 drops {drops} < {MIN_DROPS}"
        )

    if recoveries < MIN_RECOVERIES:
        raise AssertionError(
            f"W6 recoveries {recoveries} < {MIN_RECOVERIES}"
        )

    if max_streak < MIN_DROP_STREAK:
        raise AssertionError(
            f"W6 max drop streak {max_streak} < {MIN_DROP_STREAK}"
        )

    if processed != admitted:
        raise AssertionError(
            f"W6 processed {processed} != admitted {admitted}"
        )

    if released != admitted:
        raise AssertionError(
            f"W6 released {released} != admitted {admitted}"
        )

    expect(
        values,
        "w6_full_sample_count",
        admitted * BLOCK_SAMPLES,
    )

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

    expect(values, "w6_free_queue_depth_final", 1)
    expect(values, "w6_pool_free_count", 1)
    expect(values, "w6_pool_dma_owned_count", 2)

    expect(values, "w6_quiet_irq_count", W6_EVENTS)
    expect(values, "w6_quiet_input_count", W6_EVENTS)
    expect(values, "w6_quiet_admitted_count", admitted)
    expect(values, "w6_quiet_drop_count", drops)
    expect(values, "w6_quiet_processed_count", processed)

    max_decision_summary = require_value(
        values,
        "w6_max_nominal_to_decision_cycles",
    )
    max_irq_exit_summary = require_value(
        values,
        "w6_max_nominal_to_irq_exit_cycles",
    )
    max_final_summary = require_value(
        values,
        "w6_max_final_window_cycles",
    )

    if max_decision_summary > DECISION_LIMIT:
        raise AssertionError(
            f"W6 decision timing {max_decision_summary} "
            f"> {DECISION_LIMIT}"
        )

    if max_irq_exit_summary > IRQ_EXIT_LIMIT:
        raise AssertionError(
            f"W6 IRQ-exit timing {max_irq_exit_summary} "
            f"> {IRQ_EXIT_LIMIT}"
        )

    if max_final_summary > FINAL_WINDOW_LIMIT:
        raise AssertionError(
            f"W6 final-window timing {max_final_summary} "
            f"> {FINAL_WINDOW_LIMIT}"
        )

    print("W6 completed-run summary acceptance: PASS")

    # ------------------------------------------------------------
    # 5. Independent 800-event trace audit
    # ------------------------------------------------------------

    for expected_index, row in enumerate(w6_rows):
        if row["index"] != expected_index:
            raise AssertionError(
                f"W6 trace index {row['index']} != {expected_index}"
            )

        if row["sequence"] != expected_index + 1:
            raise AssertionError(
                f"W6 sequence {row['sequence']} != {expected_index + 1}"
            )

    admit_code, drop_code = derive_decision_codes(w6_rows)

    reconstructed = reconstruct_drop_accounting(
        w6_rows,
        admit_code=admit_code,
        drop_code=drop_code,
    )

    if reconstructed["admitted"] != admitted:
        raise AssertionError(
            "trace-derived admitted count does not match summary"
        )

    if reconstructed["drops"] != drops:
        raise AssertionError(
            "trace-derived drop count does not match summary"
        )

    if reconstructed["recoveries"] != recoveries:
        raise AssertionError(
            f"trace-derived recoveries {reconstructed['recoveries']} "
            f"!= summary {recoveries}"
        )

    if reconstructed["max_drop_streak"] != max_streak:
        raise AssertionError(
            f"trace-derived max streak "
            f"{reconstructed['max_drop_streak']} != summary {max_streak}"
        )

    if reconstructed["current_drop_streak"] != current_streak:
        raise AssertionError(
            f"trace-derived current streak "
            f"{reconstructed['current_drop_streak']} "
            f"!= summary {current_streak}"
        )

    # Initial K=1 state provides one FREE token, so the first completion
    # must be admitted in a valid completed run.
    if w6_rows[0]["decision"] != admit_code:
        raise AssertionError(
            "first K=1 event is not ADMIT"
        )

    previous_epoch: int | None = None

    for row in w6_rows:
        decision = row["decision"]

        if row["nominal_to_decision_cycles"] > DECISION_LIMIT:
            raise AssertionError(
                f"sequence {row['sequence']} decision timing exceeds limit"
            )

        if row["final_window_cycles"] > FINAL_WINDOW_LIMIT:
            raise AssertionError(
                f"sequence {row['sequence']} final-window exceeds limit"
            )

        if decision == drop_code:
            if row["processed_ok"] != 0:
                raise AssertionError(
                    f"DROP sequence {row['sequence']} processed_ok != 0"
                )

            if row["free_depth_after_take"] != 0:
                raise AssertionError(
                    f"DROP sequence {row['sequence']} free depth != 0"
                )

            if row["m0_before"] != row["m0_after"]:
                raise AssertionError(
                    f"DROP sequence {row['sequence']} changed M0AR"
                )

            if row["m1_before"] != row["m1_after"]:
                raise AssertionError(
                    f"DROP sequence {row['sequence']} changed M1AR"
                )

            if (
                previous_epoch is not None
                and row["mapping_epoch_after"] != previous_epoch
            ):
                raise AssertionError(
                    f"DROP sequence {row['sequence']} advanced mapping epoch"
                )

        elif decision == admit_code:
            if row["processed_ok"] != 1:
                raise AssertionError(
                    f"ADMIT sequence {row['sequence']} processed_ok != 1"
                )

            changed_m0 = row["m0_before"] != row["m0_after"]
            changed_m1 = row["m1_before"] != row["m1_after"]

            if changed_m0 == changed_m1:
                raise AssertionError(
                    f"ADMIT sequence {row['sequence']} did not change "
                    f"exactly one MxAR"
                )

            if (
                previous_epoch is not None
                and row["mapping_epoch_after"] != previous_epoch + 1
            ):
                raise AssertionError(
                    f"ADMIT sequence {row['sequence']} did not advance "
                    f"mapping epoch by exactly one"
                )

        else:
            raise AssertionError(
                f"unexpected W6 decision at sequence {row['sequence']}"
            )

        previous_epoch = row["mapping_epoch_after"]

    trace_max_decision = max(
        row["nominal_to_decision_cycles"]
        for row in w6_rows
    )
    trace_max_final = max(
        row["final_window_cycles"]
        for row in w6_rows
    )

    if trace_max_decision != max_decision_summary:
        raise AssertionError(
            f"trace max decision {trace_max_decision} "
            f"!= summary {max_decision_summary}"
        )

    if trace_max_final != max_final_summary:
        raise AssertionError(
            f"trace max final-window {trace_max_final} "
            f"!= summary {max_final_summary}"
        )

    print(
        "800-event DROP/ADMIT trace invariants: PASS "
        f"(ADMIT code={admit_code}, DROP code={drop_code})"
    )

    # ------------------------------------------------------------
    # 6. CT x DROP/recovery temporal intersection
    # ------------------------------------------------------------

    first_input = host_inputs[0]
    last_input = host_inputs[-1]

    if last_input <= first_input:
        raise AssertionError(
            "CT input interval is not positive"
        )

    # If the first CT frame is processed after W6 input_count=N, event N
    # has already occurred. Events N+1 through the last observed input_count
    # are guaranteed to complete before/equal the final CT processing point.
    strict_start_sequence = first_input + 1
    strict_end_sequence = last_input

    if strict_start_sequence > strict_end_sequence:
        raise AssertionError(
            "no guaranteed W6 event interval exists between CT endpoints"
        )

    window, recovery_pair = find_window_recovery(
        w6_rows,
        start_sequence=strict_start_sequence,
        end_sequence=strict_end_sequence,
        admit_code=admit_code,
        drop_code=drop_code,
    )

    window_drops = [
        row["sequence"]
        for row in window
        if row["decision"] == drop_code
    ]
    window_admits = [
        row["sequence"]
        for row in window
        if row["decision"] == admit_code
    ]

    if not window_drops:
        raise AssertionError(
            "no controlled DROP occurred inside the guaranteed CT interval"
        )

    if recovery_pair is None:
        raise AssertionError(
            "no DROP -> later ADMIT recovery occurred inside "
            "the guaranteed CT interval"
        )

    drop_sequence, recovery_sequence = recovery_pair

    print(
        "CT x DROP/recovery temporal intersection: PASS"
    )
    print(
        f"Guaranteed W6 event interval: "
        f"{strict_start_sequence}..{strict_end_sequence}"
    )
    print(
        f"Window ADMIT/DROP counts: "
        f"{len(window_admits)} / {len(window_drops)}"
    )
    print(
        f"Observed in-window recovery: "
        f"DROP seq {drop_sequence} -> ADMIT seq {recovery_sequence}"
    )

    # ------------------------------------------------------------
    # 7. Emit final offline result
    # ------------------------------------------------------------

    result = {
        "result": "PASS",
        "attempt": 1,
        "scope": "CT-W5 K1/DROP real-control recovery intersection",
        "checkpoint": CHECKPOINT,
        "profile": expected_profile,
        "artifact_identity": {
            "elf_sha256": elf_hash,
            "programmed_sha256": bin_hash,
            "host_tool_sha256": host_hash,
        },
        "control_traffic": {
            "requests": 10,
            "payload_bytes_each": 64,
            "burst_bytes": 760,
            "single_write_call": True,
            "rate_limited_count": 0,
            "rx_overflow_count": 0,
            "reply_success_count": 10,
            "first_w6_input": first_input,
            "last_w6_input": last_input,
            "max_rx_to_process_cycles": max_rx_to_process,
            "max_process_to_tx_cycles": max_process_to_tx,
            "max_tx_cycles": max_tx,
            "max_rx_to_reply_complete_cycles": max_rx_to_done,
            "host_submit_ms": float(h3["host_burst_submit_ms"]),
            "host_all_replies_ms": float(h3["host_all_replies_ms"]),
            "host_timestamp_interpretation": (
                "orchestration only; not used as target service-latency gate"
            ),
        },
        "w6": {
            "events": W6_EVENTS,
            "admitted": admitted,
            "drops": drops,
            "processed": processed,
            "released": released,
            "recoveries": recoveries,
            "max_drop_streak": max_streak,
            "current_drop_streak": current_streak,
            "decision_code_admit": admit_code,
            "decision_code_drop": drop_code,
            "max_nominal_to_decision_cycles": max_decision_summary,
            "max_nominal_to_irq_exit_cycles": max_irq_exit_summary,
            "max_final_window_cycles": max_final_summary,
        },
        "intersection": {
            "guaranteed_event_start": strict_start_sequence,
            "guaranteed_event_end": strict_end_sequence,
            "window_admitted_count": len(window_admits),
            "window_drop_count": len(window_drops),
            "first_proven_recovery_drop_sequence": drop_sequence,
            "first_proven_recovery_admit_sequence": recovery_sequence,
            "status": "PASS",
        },
        "evidence_sha256": {
            H2_STDOUT.name: sha256(H2_STDOUT),
            H2_STDERR.name: sha256(H2_STDERR),
            H3_JSON.name: sha256(H3_JSON),
            H3_STDOUT.name: sha256(H3_STDOUT),
            H3_STDERR.name: sha256(H3_STDERR),
            H4_JSON.name: sha256(H4_JSON),
            H4_INSPECTION.name: inspection_hash,
            H4_INSPECTION_ERR.name: sha256(H4_INSPECTION_ERR),
        },
        "hardware_operations_during_h5": False,
    }

    OUT_JSON.write_text(
        json.dumps(
            result,
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
        newline="\n",
    )

    text_lines = [
        "R2 CT-W5 K1/DROP REAL-CONTROL RECOVERY: PASS",
        "",
        f"Checkpoint: {CHECKPOINT}",
        f"ELF SHA256: {elf_hash}",
        f"Programmed SHA256: {bin_hash}",
        f"Host tool SHA256: {host_hash}",
        "",
        "Control traffic:",
        "  10 x 64-byte PING",
        "  one 760-byte serial write",
        "  rate-limited: 0",
        "  RX overflow: 0",
        "  replies: 10 / 10",
        f"  W6 input observations: {first_input} -> {last_input}",
        (
            "  CT max service cycles "
            "(rx->proc / proc->tx / tx / rx->done): "
            f"{max_rx_to_process} / {max_process_to_tx} / "
            f"{max_tx} / {max_rx_to_done}"
        ),
        "",
        "W6 K1/DROP:",
        f"  admitted: {admitted}",
        f"  capacity drops: {drops}",
        f"  processed/released: {processed} / {released}",
        f"  recoveries: {recoveries}",
        f"  max drop streak: {max_streak}",
        f"  current final drop streak: {current_streak}",
        (
            "  timing cycles "
            "(decision / IRQ-exit / final-window): "
            f"{max_decision_summary} / "
            f"{max_irq_exit_summary} / "
            f"{max_final_summary}"
        ),
        "  integrity errors: 0",
        "  final pool: 1 FREE + 2 DMA",
        "",
        "CT x DROP/recovery intersection:",
        (
            f"  guaranteed event interval: "
            f"{strict_start_sequence}..{strict_end_sequence}"
        ),
        (
            f"  in-window admitted/dropped: "
            f"{len(window_admits)} / {len(window_drops)}"
        ),
        (
            f"  proven recovery: DROP seq {drop_sequence} "
            f"-> ADMIT seq {recovery_sequence}"
        ),
        "  status: PASS",
        "",
        (
            "Host submit/all-replies millisecond timestamps are retained "
            "as orchestration evidence only and are not used as a target "
            "service-latency gate."
        ),
        "",
        "Hardware operations during H5: NONE",
        "CT-W5 hardware acceptance: PASS",
        "R2 final acceptance: NOT YET COMPLETE",
    ]

    OUT_TXT.write_text(
        "\n".join(text_lines) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    print()
    print("=== CT-W5 / H5 RESULT ===")
    print("Offline acceptance:              PASS")
    print("Real adverse burst:              PASS")
    print(
        f"W6 admitted / dropped:           "
        f"{admitted} / {drops}"
    )
    print(
        f"W6 recoveries / max streak:      "
        f"{recoveries} / {max_streak}"
    )
    print(
        f"W6 timing cycles:                "
        f"{max_decision_summary} / "
        f"{max_irq_exit_summary} / "
        f"{max_final_summary}"
    )
    print("DROP M0/M1 invariance:           PASS")
    print("DROP mapping-epoch invariance:   PASS")
    print("ADMIT one-MxAR rotation:         PASS")
    print("Trace/summary accounting:        PASS")
    print(
        f"Guaranteed CT event interval:    "
        f"{strict_start_sequence}..{strict_end_sequence}"
    )
    print(
        f"In-window admitted / dropped:    "
        f"{len(window_admits)} / {len(window_drops)}"
    )
    print(
        f"In-window recovery:              "
        f"DROP {drop_sequence} -> ADMIT {recovery_sequence} PASS"
    )
    print(
        f"CT max rx->reply complete:       "
        f"{max_rx_to_done} cycles"
    )
    print()
    print(f"Acceptance JSON:                 {OUT_JSON}")
    print(f"Acceptance TXT:                  {OUT_TXT}")
    print("Hardware:                        NOT ACCESSED")
    print()
    print("CT-W5: HARDWARE ACCEPTANCE PASS")
    print("R2 final acceptance: NOT YET COMPLETE")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print()
        print("=== CT-W5 / H5 RESULT ===")
        print("Offline acceptance: FAIL")
        print(str(exc))
        print("Hardware: NOT ACCESSED")
        raise SystemExit(1)
