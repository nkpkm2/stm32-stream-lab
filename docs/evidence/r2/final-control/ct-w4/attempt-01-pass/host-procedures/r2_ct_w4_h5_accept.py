#!/usr/bin/env python3
"""Offline acceptance for CT-W4 adverse-burst hardware Attempt 01.

No hardware access. Reads only committed artifacts plus existing H2/H3/H4 evidence.
"""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
import zlib
from pathlib import Path

REPO = Path(r"E:\Projects\stm32-stream-lab")
BUILD = Path(
    r"E:\Projects\stm32-stream-lab\build\r2-ct-w4-k8-normal-800-t1500-fd5ddcab"
)

CHECKPOINT = "fd5ddcab2b4c2b831b2105a97ff94d83619d303f"
EXPECTED_ELF = "64BA47D3A9EA93BB9980D2412932223131CD17F3A6296111FDA1F5CE259CFC8D"
EXPECTED_BIN = "D83E2B181F33EB1A5D994D61DA02083D096C23181015A8795E378152DC9C44F7"
EXPECTED_HOST = "C70A794F61980DF6DBB0F4E6B61775C22E3AE51D37B2D32EB50BEE38717B143B"

ELF = BUILD / "cubemx.elf"
BIN = BUILD / "cubemx.bin"
HOST_TOOL = REPO / r"tools\r2\r2_ct_burst.py"

H2_LOG = BUILD / "hardware-attempt01-flash-verify.txt"
H3_SUMMARY = BUILD / "hardware-attempt01-h3-runtime.json"
H3_STDOUT = BUILD / "hardware-attempt01-burst.stdout.txt"
H3_STDERR = BUILD / "hardware-attempt01-burst.stderr.txt"
H4_INSPECTION = BUILD / "hardware-attempt01-h4-inspection.txt"

OUT_JSON = BUILD / "hardware-attempt01-h5-acceptance.json"
OUT_TXT = BUILD / "hardware-attempt01-h5-acceptance.txt"

U32_MASK = 0xFFFFFFFF


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def git_text(*args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(REPO), *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"git {' '.join(args)} failed ({result.returncode})\n"
            f"stdout={result.stdout}\n"
            f"stderr={result.stderr}"
        )
    return result.stdout.strip()


def parse_value(raw: str) -> int:
    raw = raw.strip()
    return int(raw, 16) if raw.lower().startswith("0x") else int(raw, 10)


def parse_gdb(path: Path) -> dict[str, int]:
    values: dict[str, int] = {}
    pattern = re.compile(r"^([A-Za-z0-9_]+)=(0x[0-9A-Fa-f]+|[0-9]+)$")

    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        match = pattern.fullmatch(line)
        if match:
            values[match.group(1)] = parse_value(match.group(2))

    return values


def require(values: dict[str, int], name: str) -> int:
    if name not in values:
        raise AssertionError(f"missing RAM field: {name}")
    return values[name]


def expect(values: dict[str, int], name: str, expected: int) -> None:
    actual = require(values, name)
    if actual != expected:
        raise AssertionError(f"{name}={actual}, expected {expected}")


def expect_zero(values: dict[str, int], name: str) -> None:
    expect(values, name, 0)


def u32_delta(start: int, end: int) -> int:
    return (end - start) & U32_MASK


def expected_payload(request_id: int) -> bytes:
    return bytes(
        ((request_id + i * 17 + 0x5A) & 0xFF)
        for i in range(64)
    )


def main() -> int:
    print("=== CT-W4 / H5 OFFLINE ACCEPTANCE ===")
    print("Hardware access: NONE")
    print("Flash/reset/burst/GDB: NONE")

    required_files = (
        ELF,
        BIN,
        HOST_TOOL,
        H2_LOG,
        H3_SUMMARY,
        H3_STDOUT,
        H3_STDERR,
        H4_INSPECTION,
    )

    for path in required_files:
        if not path.is_file():
            raise RuntimeError(f"required evidence/artifact missing: {path}")

    for path in (OUT_JSON, OUT_TXT):
        if path.exists():
            raise RuntimeError(f"refusing to overwrite H5 result: {path}")

    # ------------------------------------------------------------
    # 1. Committed-state provenance
    # ------------------------------------------------------------

    print()
    print("=== 1. COMMITTED ARTIFACT / SOURCE GUARD ===")

    head = git_text("rev-parse", "HEAD")
    origin = git_text("rev-parse", "origin/main")
    status = git_text("status", "--porcelain=v1", "-uall")

    if head != CHECKPOINT or origin != CHECKPOINT:
        raise AssertionError(f"wrong checkpoint: HEAD={head}, origin={origin}")

    if status:
        raise AssertionError(f"working tree is not clean:\n{status}")

    elf_hash = sha256(ELF)
    bin_hash = sha256(BIN)
    host_hash = sha256(HOST_TOOL)

    if elf_hash != EXPECTED_ELF:
        raise AssertionError(f"ELF SHA256 mismatch: {elf_hash}")

    if bin_hash != EXPECTED_BIN:
        raise AssertionError(f"BIN SHA256 mismatch: {bin_hash}")

    if host_hash != EXPECTED_HOST:
        raise AssertionError(f"host tool SHA256 mismatch: {host_hash}")

    h2_text = H2_LOG.read_text(encoding="utf-8", errors="replace")
    if "Download verified successfully" not in h2_text:
        raise AssertionError("H2 flash/verify success marker missing")

    print("Committed checkpoint/artifact identity: PASS")
    print("H2 flash/verify evidence: PASS")

    # ------------------------------------------------------------
    # 2. Host burst evidence
    # ------------------------------------------------------------

    print()
    print("=== 2. H3 ADVERSE-BURST HOST EVIDENCE ===")

    h3 = json.loads(H3_SUMMARY.read_text(encoding="utf-8"))
    raw_host = json.loads(H3_STDOUT.read_text(encoding="utf-8").strip())
    stderr_text = H3_STDERR.read_text(encoding="utf-8", errors="replace").strip()

    if stderr_text:
        raise AssertionError(f"H3 host stderr is non-empty: {stderr_text}")

    if h3.get("result") != "PASS":
        raise AssertionError("H3 summary does not report PASS")

    if raw_host.get("result") != "PASS":
        raise AssertionError("raw host JSON does not report PASS")

    if h3.get("profile") != "CT-W4_ADVERSE_BURST":
        raise AssertionError(f"unexpected H3 profile: {h3.get('profile')!r}")

    if raw_host.get("profile") != "CT-W4_ADVERSE_BURST":
        raise AssertionError(
            f"unexpected raw host profile: {raw_host.get('profile')!r}"
        )

    if h3.get("request_count") != 10 or raw_host.get("request_count") != 10:
        raise AssertionError("burst did not contain exactly 10 requests")

    for obj, name in ((h3, "H3"), (raw_host, "raw host")):
        if obj.get("payload_bytes_each") != 64:
            raise AssertionError(f"{name} payload size is not 64 bytes")
        if obj.get("request_frame_bytes_each") != 76:
            raise AssertionError(f"{name} request frame is not 76 bytes")
        if obj.get("burst_bytes") != 760:
            raise AssertionError(f"{name} burst size is not 760 bytes")
        if obj.get("single_write_call") is not True:
            raise AssertionError(f"{name} does not report single-write burst")
        if obj.get("intentional_inter_request_delay_ms") != 0:
            raise AssertionError(f"{name} reports nonzero intentional gap")
        if obj.get("wait_for_reply_before_burst_complete") is not False:
            raise AssertionError(f"{name} waited for reply before burst completed")

    if h3.get("debugger_attached") is not False:
        raise AssertionError("debugger was attached during H3")

    if h3.get("flash_performed") is not False:
        raise AssertionError("H3 unexpectedly performed flash")

    if h3.get("host_returncode") != 0:
        raise AssertionError("H3 host return code is not zero")

    burst_submit_ms = float(h3.get("host_burst_submit_ms"))
    all_replies_ms = float(h3.get("host_all_replies_ms"))

    if burst_submit_ms <= 0.0 or burst_submit_ms >= 1000.0:
        raise AssertionError(
            f"burst submission is not contained in one 1000 ms window: "
            f"{burst_submit_ms} ms"
        )

    if all_replies_ms < burst_submit_ms:
        raise AssertionError(
            f"all-replies time precedes burst submission: "
            f"{all_replies_ms} < {burst_submit_ms}"
        )

    records = h3.get("records")
    raw_records = raw_host.get("records")

    if not isinstance(records, list) or len(records) != 10:
        raise AssertionError("H3 summary must contain exactly 10 reply records")

    if records != raw_records:
        raise AssertionError("H3 summary records differ from raw host records")

    expected_ids = list(range(0x0400, 0x040A))
    actual_ids = [int(record["request_id"]) for record in records]

    if actual_ids != expected_ids:
        raise AssertionError(f"request ID sequence mismatch: {actual_ids}")

    host_inputs = [
        int(record["target_w6_input_at_process"])
        for record in records
    ]

    if any(b <= a for a, b in zip(host_inputs, host_inputs[1:])):
        raise AssertionError(
            f"host W6 inputs are not strictly increasing: {host_inputs}"
        )

    if int(h3.get("first_w6_input")) != host_inputs[0]:
        raise AssertionError("H3 first_w6_input mismatch")

    if int(h3.get("last_w6_input")) != host_inputs[-1]:
        raise AssertionError("H3 last_w6_input mismatch")

    for index, record in enumerate(records):
        if int(record["target_phase"]) != 3:
            raise AssertionError(f"host record {index} phase != RUNNING")
        if int(record["target_k"]) != 8:
            raise AssertionError(f"host record {index} K != 8")
        if int(record["target_drop_mode"]) != 0:
            raise AssertionError(f"host record {index} mode != NORMAL")

    print("Single-write 760-byte burst: PASS")
    print("10/10 host replies: PASS")
    print(f"Host burst submit time: {burst_submit_ms:.3f} ms")
    print(f"Host all-replies time:  {all_replies_ms:.3f} ms")
    print(f"Host W6 input range:    {host_inputs[0]} -> {host_inputs[-1]}")

    # ------------------------------------------------------------
    # 3. Target CT RAM acceptance
    # ------------------------------------------------------------

    print()
    print("=== 3. TARGET CT RAM ACCEPTANCE ===")

    v = parse_gdb(H4_INSPECTION)

    expect(v, "ct_magic", 0x52324354)
    expect(v, "ct_task_created", 1)
    expect(v, "ct_task_started", 1)
    expect_zero(v, "ct_fault_bits")

    expect(v, "ct_rx_byte_count", 760)
    expect(v, "ct_rx_frame_count", 10)

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
        expect_zero(v, name)

    expect(v, "ct_processed_count", 10)
    expect(v, "ct_processed_while_running_count", 10)
    expect(v, "ct_reply_attempt_count", 10)
    expect(v, "ct_reply_success_count", 10)
    expect(v, "ct_trace_count", 10)

    expect(v, "ct_first_processed_w6_input", host_inputs[0])
    expect(v, "ct_last_processed_w6_input", host_inputs[-1])

    target_inputs = []
    rx_to_process = []
    process_to_reply_start = []
    reply_dma = []
    rx_to_reply_complete = []

    for index, request_id in enumerate(expected_ids):
        prefix = f"ct_trace_{index}"

        expect(v, f"{prefix}_request_id", request_id)
        expect(v, f"{prefix}_payload_length", 64)
        expect(v, f"{prefix}_w6_phase", 3)
        expect(v, f"{prefix}_processed_while_running", 1)

        target_input = require(v, f"{prefix}_w6_input")
        target_inputs.append(target_input)

        if target_input != host_inputs[index]:
            raise AssertionError(
                f"trace {index} W6 input={target_input}, "
                f"host reported {host_inputs[index]}"
            )

        expected_crc = zlib.crc32(expected_payload(request_id)) & 0xFFFFFFFF
        expect(v, f"{prefix}_payload_crc32", expected_crc)

        host_crc = int(str(records[index]["payload_crc32"]), 16)
        if host_crc != expected_crc:
            raise AssertionError(
                f"host payload CRC mismatch at trace {index}: "
                f"{host_crc:08X} != {expected_crc:08X}"
            )

        rx_cycle = require(v, f"{prefix}_rx_complete_cycle")
        processed_cycle = require(v, f"{prefix}_processed_cycle")
        reply_start_cycle = require(v, f"{prefix}_reply_start_cycle")
        reply_complete_cycle = require(v, f"{prefix}_reply_complete_cycle")

        for name, value in (
            ("rx_complete", rx_cycle),
            ("processed", processed_cycle),
            ("reply_start", reply_start_cycle),
            ("reply_complete", reply_complete_cycle),
        ):
            if value == 0:
                raise AssertionError(f"trace {index} {name} cycle is zero")

        d_rx_process = u32_delta(rx_cycle, processed_cycle)
        d_process_start = u32_delta(processed_cycle, reply_start_cycle)
        d_reply = u32_delta(reply_start_cycle, reply_complete_cycle)
        d_total = u32_delta(rx_cycle, reply_complete_cycle)

        for delta in (
            d_rx_process,
            d_process_start,
            d_reply,
            d_total,
        ):
            if delta == 0 or delta >= 180_000_000:
                raise AssertionError(
                    f"trace {index} timestamp ordering/sanity failed: "
                    f"{d_rx_process}, {d_process_start}, {d_reply}, {d_total}"
                )

        rx_to_process.append(d_rx_process)
        process_to_reply_start.append(d_process_start)
        reply_dma.append(d_reply)
        rx_to_reply_complete.append(d_total)

    if target_inputs != host_inputs:
        raise AssertionError("target trace W6 inputs differ from host records")

    max_rx_process = max(rx_to_process)
    max_process_reply = max(process_to_reply_start)
    max_reply_dma = max(reply_dma)
    max_total_service = max(rx_to_reply_complete)

    print("CT counters/errors: PASS")
    print("CT trace 0..9 IDs / CRC / phase / completion: PASS")
    print("CT rate_limited_count: 0 PASS")
    print("CT rx_overflow_count: 0 PASS")
    print(
        "CT max service cycles "
        f"(rx->proc / proc->tx / tx / rx->done): "
        f"{max_rx_process} / {max_process_reply} / "
        f"{max_reply_dma} / {max_total_service}"
    )

    # ------------------------------------------------------------
    # 4. W6 final state
    # ------------------------------------------------------------

    print()
    print("=== 4. W6 800-EVENT ACCEPTANCE ===")

    expect(v, "w6_magic", 0x52325736)
    expect(v, "w6_phase", 5)
    expect(v, "w6_test_pass", 1)
    expect_zero(v, "w6_fault_bits")

    expect(v, "w6_configured_k", 8)
    expect(v, "w6_drop_mode", 0)

    expect(v, "w6_irq_count", 800)
    expect(v, "w6_input_count", 800)
    expect(v, "w6_admitted_count", 800)
    expect(v, "w6_capacity_drop_count", 0)
    expect(v, "w6_processed_count", 800)
    expect(v, "w6_released_count", 800)

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
        expect_zero(v, name)

    expect(v, "w6_full_sample_count", 800 * 256)
    expect(v, "w6_free_queue_depth_final", 8)
    expect(v, "w6_pool_free_count", 8)
    expect(v, "w6_pool_dma_owned_count", 2)

    decision = require(v, "w6_max_nominal_to_decision_cycles")
    irq_exit = require(v, "w6_max_nominal_to_irq_exit_cycles")
    final_window = require(v, "w6_max_final_window_cycles")

    if decision > 57600:
        raise AssertionError(f"W6 decision timing failed: {decision} > 57600")

    if irq_exit > 80640:
        raise AssertionError(f"W6 IRQ-exit timing failed: {irq_exit} > 80640")

    if final_window > 3600:
        raise AssertionError(
            f"W6 final-window timing failed: {final_window} > 3600"
        )

    print("W6 K8 NORMAL: 800 / 800 admitted, 0 drops")
    print("W6 processed/released: 800 / 800")
    print(f"W6 timing: {decision} / {irq_exit} / {final_window} cycles")
    print("W6 DMA/ADC/ownership/sample/canary errors: 0")
    print("Final pool: 8 FREE + 2 DMA")

    # ------------------------------------------------------------
    # 5. Seal offline result
    # ------------------------------------------------------------

    result = {
        "result": "PASS",
        "attempt": 1,
        "scope": "CT-W4 adverse burst real control traffic",
        "checkpoint": CHECKPOINT,
        "elf_sha256": elf_hash,
        "programmed_sha256": bin_hash,
        "host_tool_sha256": host_hash,
        "request_count": 10,
        "payload_bytes_each": 64,
        "request_frame_bytes_each": 76,
        "burst_bytes": 760,
        "single_write_call": True,
        "intentional_inter_request_delay_ms": 0,
        "wait_for_reply_before_burst_complete": False,
        "host_burst_submit_ms": burst_submit_ms,
        "host_all_replies_ms": all_replies_ms,
        "first_w6_input": host_inputs[0],
        "last_w6_input": host_inputs[-1],
        "ct_rx_byte_count": require(v, "ct_rx_byte_count"),
        "ct_processed_count": require(v, "ct_processed_count"),
        "ct_reply_success_count": require(v, "ct_reply_success_count"),
        "ct_rate_limited_count": require(v, "ct_rate_limited_count"),
        "ct_rx_overflow_count": require(v, "ct_rx_overflow_count"),
        "ct_max_rx_to_process_cycles": max_rx_process,
        "ct_max_process_to_reply_start_cycles": max_process_reply,
        "ct_max_reply_dma_cycles": max_reply_dma,
        "ct_max_rx_to_reply_complete_cycles": max_total_service,
        "w6_input_count": require(v, "w6_input_count"),
        "w6_admitted_count": require(v, "w6_admitted_count"),
        "w6_capacity_drop_count": require(v, "w6_capacity_drop_count"),
        "w6_processed_count": require(v, "w6_processed_count"),
        "w6_released_count": require(v, "w6_released_count"),
        "w6_max_nominal_to_decision_cycles": decision,
        "w6_max_nominal_to_irq_exit_cycles": irq_exit,
        "w6_max_final_window_cycles": final_window,
        "hardware_operations_during_h5": False,
        "evidence_sha256": {
            H2_LOG.name: sha256(H2_LOG),
            H3_SUMMARY.name: sha256(H3_SUMMARY),
            H3_STDOUT.name: sha256(H3_STDOUT),
            H3_STDERR.name: sha256(H3_STDERR),
            H4_INSPECTION.name: sha256(H4_INSPECTION),
        },
    }

    OUT_JSON.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    lines = [
        "R2 CT-W4 ADVERSE BURST HARDWARE ATTEMPT 01: PASS",
        "",
        f"Checkpoint: {CHECKPOINT}",
        f"ELF SHA256: {elf_hash}",
        f"Programmed SHA256: {bin_hash}",
        f"Host tool SHA256: {host_hash}",
        "",
        "Traffic: 10 x 64-byte PING",
        "Request frame size: 76 bytes",
        "Burst bytes: 760",
        "Burst submission: one serial write",
        "Intentional inter-request delay: 0 ms",
        f"Host burst submit time: {burst_submit_ms:.3f} ms",
        f"Host all-replies time: {all_replies_ms:.3f} ms",
        f"W6 input range: {host_inputs[0]} -> {host_inputs[-1]}",
        "",
        "CT processed / while-running / reply-success: 10 / 10 / 10",
        "CT rate-limited: 0",
        "CT RX overflow: 0",
        "CT parser/UART/reply errors: 0",
        (
            "CT max service cycles "
            "(rx->proc / proc->tx / tx / rx->done): "
            f"{max_rx_process} / {max_process_reply} / "
            f"{max_reply_dma} / {max_total_service}"
        ),
        "",
        "W6 K8 NORMAL: 800 / 800 admitted, 0 drops",
        "W6 processed/released: 800 / 800",
        "W6 test_pass: 1",
        "W6 fault_bits: 0",
        f"W6 max decision cycles: {decision} / 57600",
        f"W6 max IRQ-exit cycles: {irq_exit} / 80640",
        f"W6 max final window cycles: {final_window} / 3600",
        "DMA/ADC/ownership/sample/canary errors: 0",
        "Final pool: 8 FREE + 2 DMA",
        "",
        "Debugger attached during runtime: NO",
        "H4 action: post-run read-only inspection",
        "Hardware operations during H5: NONE",
        "",
        "CT-W4 adverse-burst scope: PASS",
        "R2 final control acceptance: NOT YET COMPLETE",
    ]

    OUT_TXT.write_text(
        "\n".join(lines) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    print()
    print("=== CT-W4 / H5 RESULT ===")
    print("Adverse-burst acceptance: PASS")
    print("Traffic:                  10 x 64 B PING")
    print("Burst submission:         one 760 B serial write")
    print(f"Host burst submit:        {burst_submit_ms:.3f} ms")
    print(f"All replies:              {all_replies_ms:.3f} ms")
    print(f"W6 input range:           {host_inputs[0]} -> {host_inputs[-1]}")
    print("CT reply success:         10 / 10")
    print("CT rate limited:          0")
    print("CT RX overflow:           0")
    print(
        "CT max service:           "
        f"{max_total_service} cycles "
        "(RX complete -> reply complete)"
    )
    print("W6 admitted:              800 / 800")
    print("W6 drops:                 0")
    print("W6 processed:             800 / 800")
    print(
        f"Timing cycles:            "
        f"{decision} / {irq_exit} / {final_window}"
    )
    print("Integrity errors:         0")
    print("Final pool:               8 FREE + 2 DMA")
    print()
    print(f"Acceptance JSON:          {OUT_JSON}")
    print(f"Acceptance TXT:           {OUT_TXT}")
    print("Hardware:                 NOT ACCESSED")
    print()
    print("CT-W4: CLOSED / KNOWN-GOOD BURST")
    print("R2 final acceptance: NOT YET COMPLETE")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print()
        print("=== CT-W4 / H5 RESULT ===")
        print("Adverse-burst acceptance: FAIL")
        print(str(exc))
        print("Hardware: NOT ACCESSED")
        raise SystemExit(1)
