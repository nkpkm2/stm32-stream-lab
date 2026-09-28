#!/usr/bin/env python3
"""Offline R5 known-truth vector manifest and arithmetic self-check.

This deliberately has no board, serial, flash, or filesystem mutation path.
Its values mirror the native `test_r5_run_metrics` cases; both are required in
W5 so arithmetic cannot silently drift between C and host-side evidence tools.
"""
from __future__ import annotations

import argparse
import json
import sys

D = 160
BIN_WIDTH = D // 16

VECTORS = (
    ("SYN01", "warmup-primary-tail-known", "sealed conservation"),
    ("SYN02", "s2-free", "S2 does not admit"),
    ("SYN03", "s2-empty", "S2 does not capacity-drop"),
    ("SYN04", "cutoff-wins", "unresolved is immutable"),
    ("SYN05", "completion-wins", "serial decides same-timestamp race"),
    ("SYN06", "insufficient-cutoff", "not an infrastructure failure"),
    ("SYN07", "overflow", "latency beyond 8D is overflow"),
    ("SYN08", "unique-outcomes", "drop/late/unresolved conservation"),
    ("SYN09", "sealed-store", "immutable copied result"),
    ("SYN10", "deadline-equality", "t_commit == deadline is on-time"),
    ("SYN11", "no-completed-p99", "P99 is N/A"),
    ("SYN12", "p99-overflow-rank", "P99 is OUT_OF_RANGE"),
    ("SYN13", "tail-minimum", "tail < 2 rejected"),
    ("SYN14", "new-run-isolation", "state starts empty"),
)


def check() -> list[str]:
    errors: list[str] = []
    if len(VECTORS) != 14 or [v[0] for v in VECTORS] != [f"SYN{i:02d}" for i in range(1, 15)]:
        errors.append("vector IDs are not SYN01..SYN14")
    if BIN_WIDTH != 10:
        errors.append("D/16 bin width drifted")
    if 8 * D != 1280:
        errors.append("8D boundary drifted")
    if not (260 - 100 == D):
        errors.append("deadline equality vector drifted")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    errors = check()
    payload = {"schema_version": 1, "deadline_cycles": D,
               "bin_width_cycles": BIN_WIDTH, "overflow_at_cycles": 8 * D,
               "vectors": [{"id": key, "case": case, "assertion": assertion}
                           for key, case, assertion in VECTORS],
               "status": "PASS" if not errors else "FAIL", "errors": errors}
    print(json.dumps(payload, sort_keys=True, indent=2))
    return 0 if not errors else 1


if __name__ == "__main__":
    sys.exit(main())
