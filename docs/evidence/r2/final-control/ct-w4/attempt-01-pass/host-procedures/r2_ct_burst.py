#!/usr/bin/env python3
"""CT-W4 adverse-burst real-control-traffic host generator.

Frozen runtime profile:
- W6 K=8 / NORMAL
- 800 W6 events, 1500 ms run timeout
- 10 PING requests
- 64-byte payload per request
- sequential request IDs
- all 10 request frames submitted in one serial write
- no intentional inter-request delay
- host does not wait for replies before submitting the complete burst

Use --self-test for a pure host-side test with no serial access.
"""

from __future__ import annotations

import argparse
import json
import struct
import time
import zlib

MAGIC = b"\xA5\x5A"
VERSION = 1
PING = 0x01
PING_REPLY = 0x81
MAX_PAYLOAD = 64
W6_RUNNING = 3
BANNER = b"P0-B VCP READY\r\n"

COUNT = 10
BASE_REQUEST_ID = 0x0400
DELAY_MS = 145.0
BANNER_TIMEOUT_S = 8.0
ALL_REPLIES_TIMEOUT_S = 2.0


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
    if payload_len > MAX_PAYLOAD:
        raise ValueError("payload too long")

    expected = 8 + payload_len + 4
    if len(frame) != expected:
        raise ValueError("frame length mismatch")

    observed_crc = struct.unpack_from("<I", frame, expected - 4)[0]
    if observed_crc != crc32(frame[:-4]):
        raise ValueError("bad frame CRC")

    msg_type = frame[3]
    request_id = struct.unpack_from("<H", frame, 4)[0]
    payload = frame[8:-4]
    return msg_type, request_id, payload


class FrameStreamReader:
    """Persistent frame reader that preserves bytes after the first frame."""

    def __init__(self) -> None:
        self.buffer = bytearray()

    def _extract(self) -> bytes | None:
        while len(self.buffer) >= 2 and self.buffer[:2] != MAGIC:
            del self.buffer[0]

        if len(self.buffer) < 8:
            return None

        if self.buffer[2] != VERSION:
            del self.buffer[0]
            return None

        payload_len = struct.unpack_from("<H", self.buffer, 6)[0]

        if payload_len > MAX_PAYLOAD:
            del self.buffer[0]
            return None

        expected = 8 + payload_len + 4

        if len(self.buffer) < expected:
            return None

        candidate = bytes(self.buffer[:expected])

        try:
            decode_frame(candidate)
        except ValueError:
            del self.buffer[0]
            return None

        del self.buffer[:expected]
        return candidate

    def read_frame(self, port, deadline: float) -> bytes:
        while time.monotonic() < deadline:
            frame = self._extract()
            if frame is not None:
                return frame

            chunk = port.read(max(1, port.in_waiting))
            if chunk:
                self.buffer.extend(chunk)

        frame = self._extract()
        if frame is not None:
            return frame

        raise TimeoutError("no valid reply frame before burst deadline")


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

    if request_id != expected_request_id:
        raise RuntimeError(
            f"reply request_id mismatch: {request_id} != {expected_request_id}"
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
            f"target payload length mismatch: "
            f"{echoed_length} != {len(expected_payload)}"
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


class FakePort:
    """Minimal port used only by --self-test."""

    def __init__(self, data: bytes) -> None:
        self.data = bytearray(data)

    @property
    def in_waiting(self) -> int:
        return len(self.data)

    def read(self, count: int) -> bytes:
        if not self.data:
            return b""
        # Deliberately return as much buffered data as requested so the
        # persistent reader must preserve multiple frames correctly.
        count = min(count, len(self.data))
        result = bytes(self.data[:count])
        del self.data[:count]
        return result


def self_test() -> int:
    if crc32(b"123456789") != 0xCBF43926:
        raise RuntimeError("CRC-32 check vector failed")

    golden_empty = bytes.fromhex(
        "A5 5A 01 01 34 12 00 00 85 62 FD 96"
    )
    if encode(PING, 0x1234, b"") != golden_empty:
        raise RuntimeError("empty-payload golden frame mismatch")

    request_ids = [
        BASE_REQUEST_ID + index
        for index in range(COUNT)
    ]

    requests = []
    expected_payloads = {}

    for request_id in request_ids:
        payload = make_payload(request_id)
        frame = encode(PING, request_id, payload)

        if len(payload) != 64:
            raise RuntimeError("request payload is not 64 bytes")
        if len(frame) != 76:
            raise RuntimeError("request frame is not 76 bytes")

        expected_payloads[request_id] = payload
        requests.append(frame)

    burst = b"".join(requests)

    if len(burst) != 760:
        raise RuntimeError(f"burst length {len(burst)} != 760")

    # Build ten concatenated replies and prove the persistent reader does not
    # discard frames when several arrive in one host read.
    replies = []

    for index, request_id in enumerate(request_ids):
        payload = expected_payloads[request_id]
        reply_payload = (
            struct.pack(
                "<II",
                crc32(payload),
                50 + index * 6,
            )
            + bytes((W6_RUNNING, 8, 0, 64))
        )
        replies.append(
            encode(
                PING_REPLY,
                request_id,
                reply_payload,
            )
        )

    fake_port = FakePort(b"".join(replies))
    reader = FrameStreamReader()
    decoded_ids = []

    deadline = time.monotonic() + 1.0

    for index, request_id in enumerate(request_ids):
        frame = reader.read_frame(fake_port, deadline)
        reply = parse_reply(
            frame,
            request_id,
            expected_payloads[request_id],
            expected_k=8,
            expected_mode=0,
        )
        decoded_ids.append(reply["request_id"])

    if decoded_ids != request_ids:
        raise RuntimeError("concatenated-reply reader self-test failed")

    if reader.buffer:
        raise RuntimeError("persistent frame reader left unexpected bytes")

    result = {
        "result": "PASS",
        "profile": "CT-W4_ADVERSE_BURST",
        "request_count": COUNT,
        "payload_bytes_each": MAX_PAYLOAD,
        "request_frame_bytes_each": 76,
        "burst_bytes": len(burst),
        "base_request_id": BASE_REQUEST_ID,
        "single_write_call": True,
        "intentional_inter_request_delay_ms": 0,
        "wait_for_reply_before_burst_complete": False,
        "persistent_multi_reply_reader": "PASS",
    }

    print(json.dumps(result, sort_keys=True))
    print("CT-W4 adverse-burst host self-test: PASS")
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
        default=BASE_REQUEST_ID,
    )
    parser.add_argument(
        "--delay-ms",
        type=float,
        default=DELAY_MS,
    )
    parser.add_argument(
        "--banner-timeout",
        type=float,
        default=BANNER_TIMEOUT_S,
    )
    parser.add_argument(
        "--all-replies-timeout",
        type=float,
        default=ALL_REPLIES_TIMEOUT_S,
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
        parser.error("--port is required unless --self-test is used")

    if not (0 <= args.base_request_id <= 0xFFFF):
        raise RuntimeError("base request ID out of range")

    if args.base_request_id + COUNT - 1 > 0xFFFF:
        raise RuntimeError("request ID sequence would wrap")

    if args.delay_ms < 0:
        raise RuntimeError("delay must be non-negative")

    if args.all_replies_timeout <= 0:
        raise RuntimeError("all-replies timeout must be positive")

    try:
        import serial
    except ImportError as exc:
        raise SystemExit(
            "pyserial is required: python -m pip install pyserial"
        ) from exc

    expected_mode = 0 if args.expect_mode == "NORMAL" else 1

    request_ids = [
        args.base_request_id + index
        for index in range(COUNT)
    ]

    expected_payloads = {}
    frames = []

    for request_id in request_ids:
        payload = make_payload(request_id)
        expected_payloads[request_id] = payload
        frames.append(
            encode(
                PING,
                request_id,
                payload,
            )
        )

    burst = b"".join(frames)

    if len(burst) != 760:
        raise RuntimeError(
            f"internal burst size mismatch: {len(burst)} != 760"
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

        time.sleep(args.delay_ms / 1000.0)

        burst_start = time.monotonic()

        written = port.write(burst)
        port.flush()

        burst_submit_done = time.monotonic()

        if written != len(burst):
            raise RuntimeError(
                f"short burst write: {written}/{len(burst)}"
            )

        reader = FrameStreamReader()
        deadline = (
            burst_start
            + args.all_replies_timeout
        )

        for index, request_id in enumerate(request_ids):
            reply_frame = reader.read_frame(
                port,
                deadline,
            )

            received_at = time.monotonic()

            reply = parse_reply(
                reply_frame,
                request_id,
                expected_payloads[request_id],
                expected_k=args.expect_k,
                expected_mode=expected_mode,
            )

            records.append(
                {
                    "index": index,
                    "request_id": request_id,
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
                    "reply_received_offset_ms": round(
                        (received_at - burst_start)
                        * 1000.0,
                        3,
                    ),
                }
            )

        all_replies_done = time.monotonic()

    actual_ids = [
        record["request_id"]
        for record in records
    ]

    if actual_ids != request_ids:
        raise RuntimeError(
            f"reply ID sequence mismatch: {actual_ids}"
        )

    w6_inputs = [
        record["target_w6_input_at_process"]
        for record in records
    ]

    if any(
        later <= earlier
        for earlier, later in zip(
            w6_inputs,
            w6_inputs[1:],
        )
    ):
        raise RuntimeError(
            f"target W6 inputs are not strictly increasing: {w6_inputs}"
        )

    result = {
        "result": "PASS",
        "profile": "CT-W4_ADVERSE_BURST",
        "port": args.port,
        "baud": args.baud,
        "request_count": COUNT,
        "payload_bytes_each": MAX_PAYLOAD,
        "request_frame_bytes_each": 76,
        "burst_bytes": len(burst),
        "single_write_call": True,
        "intentional_inter_request_delay_ms": 0,
        "wait_for_reply_before_burst_complete": False,
        "base_request_id": args.base_request_id,
        "delay_ms": args.delay_ms,
        "expect_k": args.expect_k,
        "expect_mode": args.expect_mode,
        "host_burst_submit_ms": round(
            (burst_submit_done - burst_start)
            * 1000.0,
            3,
        ),
        "host_all_replies_ms": round(
            (all_replies_done - burst_start)
            * 1000.0,
            3,
        ),
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
