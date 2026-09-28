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
    ("SYN01", "all_on_time", "all primary outcomes are ON_TIME"),
    ("SYN02", "all_drop", "all primary outcomes are CAPACITY_DROP"),
    ("SYN03", "outcomes", "drop/on-time/late/unresolved are conserved"),
    ("SYN04", "s2", "S0 capacity-drop still opens the window"),
    ("SYN05", "s1_empty", "S1 tail drop closes window, not cohort"),
    ("SYN06", "s2", "S2 with FREE is cutoff-only"),
    ("SYN07", "s2_empty", "S2 without FREE is cutoff-only"),
    ("SYN08", "completion_wins", "same-time completion serial wins"),
    ("SYN09", "cutoff_wins_same_time", "same-time cutoff serial wins"),
    ("SYN10", "insufficient", "observation is insufficient, not invalid"),
    ("SYN11", "histogram_edges", "overflow and exact 8D convention"),
    ("SYN12", "outcomes", "overflow plus unresolved gives censored P99"),
    ("SYN13", "cutoff", "post-close completion leaves outcome unchanged"),
    ("SYN14", "store", "repeated immutable result reads"),
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
