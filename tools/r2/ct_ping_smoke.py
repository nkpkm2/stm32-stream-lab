#!/usr/bin/env python3
"""One-request CT-W2 hardware smoke for the R2 final control path."""

from __future__ import annotations

import argparse
import json
import struct
import time
import zlib

try:
    import serial
except ImportError as exc:  # pragma: no cover - handled on the real host
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

MAGIC = b"\xA5\x5A"
VERSION = 1
PING = 0x01
PING_REPLY = 0x81
MAX_PAYLOAD = 64
W6_RUNNING = 3
BANNER = b"P0-B VCP READY\r\n"


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def encode(msg_type: int, request_id: int, payload: bytes) -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload exceeds CT runtime maximum")
    header = MAGIC + bytes((VERSION, msg_type)) + struct.pack("<HH", request_id, len(payload))
    body = header + payload
    return body + struct.pack("<I", crc32(body))


def read_frame(port: serial.Serial, timeout_s: float) -> tuple[int, int, bytes]:
    deadline = time.monotonic() + timeout_s
    buf = bytearray()
    expected = None

    while time.monotonic() < deadline:
        chunk = port.read(max(1, port.in_waiting))
        if chunk:
            buf.extend(chunk)
        else:
            continue

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
        observed_crc = struct.unpack_from("<I", candidate, expected - 4)[0]
        if observed_crc != crc32(candidate[:-4]):
            del buf[0]
            continue

        msg_type = candidate[3]
        request_id = struct.unpack_from("<H", candidate, 4)[0]
        payload = candidate[8:-4]
        return msg_type, request_id, payload

    raise TimeoutError(f"no valid reply frame within {timeout_s:.3f}s")


def wait_for_banner(port: serial.Serial, timeout_s: float) -> None:
    deadline = time.monotonic() + timeout_s
    buf = bytearray()
    while time.monotonic() < deadline:
        chunk = port.read(max(1, port.in_waiting))
        if chunk:
            buf.extend(chunk)
            if BANNER in buf:
                return
    raise TimeoutError("P0-B VCP READY banner not observed")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--request-id", type=lambda x: int(x, 0), default=0x0201)
    parser.add_argument("--delay-ms", type=float, default=145.0,
                        help="delay from startup banner before sending the one PING")
    parser.add_argument("--banner-timeout", type=float, default=8.0)
    parser.add_argument("--reply-timeout", type=float, default=1.0)
    parser.add_argument("--expect-k", type=int, default=8)
    parser.add_argument("--expect-mode", choices=("NORMAL", "DROP"), default="NORMAL")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    request_id = args.request_id & 0xFFFF
    payload = bytes(((request_id + i * 17 + 0x5A) & 0xFF) for i in range(MAX_PAYLOAD))
    request = encode(PING, request_id, payload)

    with serial.Serial(args.port, args.baud, timeout=0.02, write_timeout=1.0) as port:
        port.reset_input_buffer()
        wait_for_banner(port, args.banner_timeout)
        time.sleep(args.delay_ms / 1000.0)
        sent_at = time.monotonic()
        written = port.write(request)
        port.flush()
        if written != len(request):
            raise RuntimeError(f"short serial write: {written}/{len(request)}")
        msg_type, reply_id, reply_payload = read_frame(port, args.reply_timeout)
        received_at = time.monotonic()

    if msg_type != PING_REPLY:
        raise RuntimeError(f"unexpected reply type 0x{msg_type:02X}")
    if reply_id != request_id:
        raise RuntimeError(f"reply request_id mismatch: {reply_id} != {request_id}")
    if len(reply_payload) != 12:
        raise RuntimeError(f"reply payload length {len(reply_payload)} != 12")

    payload_crc, w6_input = struct.unpack_from("<II", reply_payload, 0)
    phase, k_value, drop_mode, echoed_length = reply_payload[8:12]
    expected_mode = 0 if args.expect_mode == "NORMAL" else 1

    if payload_crc != crc32(payload):
        raise RuntimeError("target payload CRC does not match the 64-byte request payload")
    if phase != W6_RUNNING:
        raise RuntimeError(f"target processed PING outside W6 RUNNING: phase={phase}")
    if k_value != args.expect_k:
        raise RuntimeError(f"target K mismatch: {k_value} != {args.expect_k}")
    if drop_mode != expected_mode:
        raise RuntimeError(f"target mode mismatch: {drop_mode} != {expected_mode}")
    if echoed_length != len(payload):
        raise RuntimeError(f"target payload length mismatch: {echoed_length} != {len(payload)}")
    if w6_input == 0:
        raise RuntimeError("PING was processed before the first W6 input event")

    result = {
        "result": "PASS",
        "port": args.port,
        "baud": args.baud,
        "request_id": request_id,
        "request_bytes": len(request),
        "request_payload_bytes": len(payload),
        "reply_payload_bytes": len(reply_payload),
        "payload_crc32": f"{payload_crc:08X}",
        "target_phase": phase,
        "target_k": k_value,
        "target_drop_mode": drop_mode,
        "target_w6_input_at_process": w6_input,
        "host_round_trip_ms": round((received_at - sent_at) * 1000.0, 3),
    }

    if args.json:
        print(json.dumps(result, sort_keys=True))
    else:
        print("R2 CT-W2 PING SMOKE: PASS")
        for key, value in result.items():
            if key != "result":
                print(f"{key}: {value}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
