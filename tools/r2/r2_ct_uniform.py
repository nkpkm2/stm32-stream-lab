#!/usr/bin/env python3
"""CT-W3 uniform real-control-traffic host generator.

Runtime profile:
- 10 PING requests
- 64-byte payload per request
- 100 ms absolute start-to-start spacing
- intended for W6 K=8 / NORMAL / 800-event run

Use --self-test for a pure host-side test with no serial access.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import time
import zlib

MAGIC = b"\xA5\x5A"
VERSION = 1
PING = 0x01
PING_REPLY = 0x81
MAX_PAYLOAD = 64
W6_RUNNING = 3
BANNER = b"P0-B VCP READY\r\n"

DEFAULT_COUNT = 10
DEFAULT_INTERVAL_MS = 100.0
DEFAULT_DELAY_MS = 145.0
DEFAULT_REPLY_TIMEOUT_MS = 80.0
DEFAULT_BASE_REQUEST_ID = 0x0300


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def make_payload(request_id: int) -> bytes:
    return bytes(
        ((request_id + i * 17 + 0x5A) & 0xFF)
        for i in range(MAX_PAYLOAD)
    )


def encode(msg_type: int, request_id: int, payload: bytes) -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload exceeds CT runtime maximum")

    header = (
        MAGIC
        + bytes((VERSION, msg_type))
        + struct.pack("<HH", request_id & 0xFFFF, len(payload))
    )
    body = header + payload
    return body + struct.pack("<I", crc32(body))


def decode_frame(frame: bytes) -> tuple[int, int, bytes]:
    if len(frame) < 12:
        raise ValueError("frame too short")
    if frame[:2] != MAGIC:
        raise ValueError("bad magic")
    if frame[2] != VERSION:
        raise ValueError("bad version")

    payload_len = struct.unpack_from("<H", frame, 6)[0]
    expected = 8 + payload_len + 4

    if payload_len > MAX_PAYLOAD:
        raise ValueError("payload too long")
    if len(frame) != expected:
        raise ValueError("frame length mismatch")

    observed_crc = struct.unpack_from("<I", frame, expected - 4)[0]
    if observed_crc != crc32(frame[:-4]):
        raise ValueError("bad frame CRC")

    msg_type = frame[3]
    request_id = struct.unpack_from("<H", frame, 4)[0]
    payload = frame[8:-4]
    return msg_type, request_id, payload


def parse_reply(
    frame: bytes,
    expected_request_id: int,
    expected_payload: bytes,
    expected_k: int,
    expected_mode: int,
) -> dict:
    msg_type, request_id, payload = decode_frame(frame)

    if msg_type != PING_REPLY:
        raise RuntimeError(f"unexpected reply type 0x{msg_type:02X}")
    if request_id != (expected_request_id & 0xFFFF):
        raise RuntimeError(
            f"reply request_id mismatch: {request_id} != {expected_request_id & 0xFFFF}"
        )
    if len(payload) != 12:
        raise RuntimeError(f"reply payload length {len(payload)} != 12")

    payload_crc, w6_input = struct.unpack_from("<II", payload, 0)
    phase, k_value, drop_mode, echoed_length = payload[8:12]

    if payload_crc != crc32(expected_payload):
        raise RuntimeError("target payload CRC mismatch")
    if phase != W6_RUNNING:
        raise RuntimeError(
            f"target processed PING outside W6 RUNNING: phase={phase}"
        )
    if k_value != expected_k:
        raise RuntimeError(f"target K mismatch: {k_value} != {expected_k}")
    if drop_mode != expected_mode:
        raise RuntimeError(
            f"target mode mismatch: {drop_mode} != {expected_mode}"
        )
    if echoed_length != len(expected_payload):
        raise RuntimeError(
            f"target payload length mismatch: {echoed_length} != {len(expected_payload)}"
        )
    if w6_input == 0:
        raise RuntimeError("PING was processed before first W6 input")

    return {
        "request_id": request_id,
        "payload_crc32": payload_crc,
        "w6_input": w6_input,
        "phase": phase,
        "k_value": k_value,
        "drop_mode": drop_mode,
        "echoed_length": echoed_length,
    }


def read_frame(port, timeout_s: float) -> bytes:
    deadline = time.monotonic() + timeout_s
    buf = bytearray()

    while time.monotonic() < deadline:
        chunk = port.read(max(1, port.in_waiting))
        if not chunk:
            continue

        buf.extend(chunk)

        while len(buf) >= 2 and buf[:2] != MAGIC:
            del buf[0]

        if len(buf) < 8:
            continue

        if buf[2] != VERSION:
            del buf[0]
            continue

        payload_len = struct.unpack_from("<H", buf, 6)[0]
        if payload_len > MAX_PAYLOAD:
            del buf[0]
            continue

        expected = 8 + payload_len + 4
        if len(buf) < expected:
            continue

        candidate = bytes(buf[:expected])

        try:
            decode_frame(candidate)
        except ValueError:
            del buf[0]
            continue

        return candidate

    raise TimeoutError(
        f"no valid reply frame within {timeout_s:.3f}s"
    )


def wait_for_banner(port, timeout_s: float) -> None:
    deadline = time.monotonic() + timeout_s
    buf = bytearray()

    while time.monotonic() < deadline:
        chunk = port.read(max(1, port.in_waiting))
        if chunk:
            buf.extend(chunk)
            if BANNER in buf:
                return

    raise TimeoutError("P0-B VCP READY banner not observed")


def sleep_until(deadline: float) -> None:
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return
        time.sleep(min(remaining, 0.005))


def self_test() -> int:
    if crc32(b"123456789") != 0xCBF43926:
        raise RuntimeError("CRC-32 check vector failed")

    golden_empty = bytes.fromhex(
        "A5 5A 01 01 34 12 00 00 85 62 FD 96"
    )
    if encode(PING, 0x1234, b"") != golden_empty:
        raise RuntimeError("empty-payload golden frame mismatch")

    ids = [
        (DEFAULT_BASE_REQUEST_ID + i) & 0xFFFF
        for i in range(DEFAULT_COUNT)
    ]

    if len(ids) != len(set(ids)):
        raise RuntimeError("request IDs are not unique")

    offsets_ms = [
        i * DEFAULT_INTERVAL_MS
        for i in range(DEFAULT_COUNT)
    ]

    expected_offsets = [
        0.0, 100.0, 200.0, 300.0, 400.0,
        500.0, 600.0, 700.0, 800.0, 900.0,
    ]

    if offsets_ms != expected_offsets:
        raise RuntimeError("absolute schedule offsets are wrong")

    for index, request_id in enumerate(ids):
        payload = make_payload(request_id)
        request = encode(PING, request_id, payload)

        if len(payload) != 64:
            raise RuntimeError(
                f"payload length failed at request {index}"
            )
        if len(request) != 76:
            raise RuntimeError(
                f"request frame length failed at request {index}"
            )

        reply_payload = (
            struct.pack(
                "<II",
                crc32(payload),
                40 + index * 70,
            )
            + bytes((W6_RUNNING, 8, 0, 64))
        )

        reply_frame = encode(
            PING_REPLY,
            request_id,
            reply_payload,
        )

        reply = parse_reply(
            reply_frame,
            request_id,
            payload,
            expected_k=8,
            expected_mode=0,
        )

        if reply["request_id"] != request_id:
            raise RuntimeError(
                "reply ID self-test failed"
            )

    result = {
        "result": "PASS",
        "requests": DEFAULT_COUNT,
        "payload_bytes_each": MAX_PAYLOAD,
        "request_bytes_each": 76,
        "absolute_interval_ms": DEFAULT_INTERVAL_MS,
        "schedule_offsets_ms": offsets_ms,
        "first_delay_ms_runtime_default": DEFAULT_DELAY_MS,
        "reply_timeout_ms_runtime_default": DEFAULT_REPLY_TIMEOUT_MS,
        "base_request_id": DEFAULT_BASE_REQUEST_ID,
    }

    print(json.dumps(result, sort_keys=True))
    print("CT-W3 uniform host self-test: PASS")
    print("Serial access: NONE")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--base-request-id",
        type=lambda x: int(x, 0),
        default=DEFAULT_BASE_REQUEST_ID,
    )
    parser.add_argument(
        "--count",
        type=int,
        default=DEFAULT_COUNT,
    )
    parser.add_argument(
        "--interval-ms",
        type=float,
        default=DEFAULT_INTERVAL_MS,
    )
    parser.add_argument(
        "--delay-ms",
        type=float,
        default=DEFAULT_DELAY_MS,
    )
    parser.add_argument(
        "--banner-timeout",
        type=float,
        default=8.0,
    )
    parser.add_argument(
        "--reply-timeout-ms",
        type=float,
        default=DEFAULT_REPLY_TIMEOUT_MS,
    )
    parser.add_argument(
        "--expect-k",
        type=int,
        default=8,
    )
    parser.add_argument(
        "--expect-mode",
        choices=("NORMAL", "DROP"),
        default="NORMAL",
    )
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    if not args.port:
        parser.error(
            "--port is required unless --self-test is used"
        )
    if args.count != 10:
        raise RuntimeError(
            "CT-W3 uniform profile requires exactly 10 requests"
        )
    if abs(args.interval_ms - 100.0) > 1e-9:
        raise RuntimeError(
            "CT-W3 uniform profile requires exactly 100 ms spacing"
        )
    if (
        args.reply_timeout_ms <= 0
        or args.reply_timeout_ms >= args.interval_ms
    ):
        raise RuntimeError(
            "reply timeout must be positive and less than interval"
        )
    if not (0 <= args.base_request_id <= 0xFFFF):
        raise RuntimeError("base request ID out of range")
    if args.base_request_id + args.count - 1 > 0xFFFF:
        raise RuntimeError("request ID sequence would wrap")

    try:
        import serial
    except ImportError as exc:
        raise SystemExit(
            "pyserial is required: python -m pip install pyserial"
        ) from exc

    expected_mode = (
        0 if args.expect_mode == "NORMAL" else 1
    )

    records = []

    with serial.Serial(
        args.port,
        args.baud,
        timeout=0.005,
        write_timeout=1.0,
    ) as port:
        port.reset_input_buffer()
        wait_for_banner(
            port,
            args.banner_timeout,
        )

        first_deadline = (
            time.monotonic()
            + args.delay_ms / 1000.0
        )

        previous_sent_at = None

        for index in range(args.count):
            request_id = (
                args.base_request_id + index
            )
            payload = make_payload(request_id)
            request = encode(
                PING,
                request_id,
                payload,
            )

            scheduled_at = (
                first_deadline
                + index
                * args.interval_ms
                / 1000.0
            )

            sleep_until(scheduled_at)

            sent_at = time.monotonic()
            lateness_ms = (
                sent_at - scheduled_at
            ) * 1000.0

            if lateness_ms >= args.interval_ms:
                raise RuntimeError(
                    f"request {index} missed schedule by "
                    f"{lateness_ms:.3f} ms"
                )

            written = port.write(request)
            port.flush()

            if written != len(request):
                raise RuntimeError(
                    f"short serial write: "
                    f"{written}/{len(request)}"
                )

            reply_frame = read_frame(
                port,
                args.reply_timeout_ms / 1000.0,
            )

            received_at = time.monotonic()

            reply = parse_reply(
                reply_frame,
                request_id,
                payload,
                expected_k=args.expect_k,
                expected_mode=expected_mode,
            )

            if previous_sent_at is None:
                start_to_start_ms = None
            else:
                start_to_start_ms = (
                    sent_at - previous_sent_at
                ) * 1000.0

            previous_sent_at = sent_at

            records.append(
                {
                    "index": index,
                    "request_id": request_id,
                    "scheduled_offset_ms": round(
                        index * args.interval_ms,
                        3,
                    ),
                    "schedule_lateness_ms": round(
                        lateness_ms,
                        3,
                    ),
                    "start_to_start_ms": (
                        None
                        if start_to_start_ms is None
                        else round(
                            start_to_start_ms,
                            3,
                        )
                    ),
                    "round_trip_ms": round(
                        (
                            received_at
                            - sent_at
                        ) * 1000.0,
                        3,
                    ),
                    "payload_crc32": (
                        f"{reply['payload_crc32']:08X}"
                    ),
                    "target_w6_input_at_process": (
                        reply["w6_input"]
                    ),
                    "target_phase": (
                        reply["phase"]
                    ),
                    "target_k": (
                        reply["k_value"]
                    ),
                    "target_drop_mode": (
                        reply["drop_mode"]
                    ),
                }
            )

    w6_inputs = [
        record["target_w6_input_at_process"]
        for record in records
    ]

    if any(
        b <= a
        for a, b in zip(
            w6_inputs,
            w6_inputs[1:],
        )
    ):
        raise RuntimeError(
            "target W6 input counters are not "
            f"strictly increasing: {w6_inputs}"
        )

    result = {
        "result": "PASS",
        "profile": "CT-W3_UNIFORM_10HZ",
        "port": args.port,
        "baud": args.baud,
        "request_count": len(records),
        "payload_bytes_each": MAX_PAYLOAD,
        "request_bytes_each": 76,
        "interval_ms": args.interval_ms,
        "delay_ms": args.delay_ms,
        "base_request_id": args.base_request_id,
        "expect_k": args.expect_k,
        "expect_mode": args.expect_mode,
        "first_w6_input": w6_inputs[0],
        "last_w6_input": w6_inputs[-1],
        "records": records,
    }

    print(
        json.dumps(
            result,
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
