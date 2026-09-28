#!/usr/bin/env python3
"""Emit the bounded R4 final-review classification from sealed evidence.

This is intentionally an offline classifier: it never builds, flashes, or
runs target firmware.  It makes the R4 exit decision reproducible from the
already sealed H0--H5 records plus the explicit T04 deviation package.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


PASS_ATTEMPTS = (
    "t12-soak-a/attempt-0002",
    "t12-soak-b/attempt-0002",
    "t15-q0/attempt-0004",
    "t15-release/attempt-0005",
    "t17-atomic/attempt-0005",
    "task-synthetic/attempt-0005",
    "dma-window/attempt-0001",
    "window-intersection/attempt-0001",
    "microbenchmark/attempt-0002",
    "mask-timing/attempt-0001",
    "response-synthetic/attempt-0001",
    "combined-service/attempt-0001",
    "r2-normal-anchor/attempt-0002",
    "r2-drop-anchor/attempt-0002",
)

R3_ANCHORS = (
    "w6/w6-a/attempt-0003",
    "w6/w6-b/attempt-0003",
)


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def classify(repo: Path) -> dict:
    evidence = repo / "docs" / "evidence" / "r4"
    passed = []
    for attempt in PASS_ATTEMPTS:
        acceptance = read_json(evidence / attempt / "acceptance.json")
        if acceptance.get("result") != "PASS":
            raise ValueError(f"sealed attempt is not PASS: {attempt}")
        passed.append(attempt)

    r3_evidence = repo / "docs" / "evidence" / "r3"
    r3_passed = []
    for attempt in R3_ANCHORS:
        acceptance = read_json(r3_evidence / attempt / "acceptance.json")
        if acceptance.get("result") != "PASS":
            raise ValueError(f"R3 representative anchor is not PASS: {attempt}")
        r3_passed.append(attempt)

    witness = read_json(evidence / "t04-rc3-diagnostic" / "attempt-0002" /
                        "classification.json")
    deviation = read_json(evidence / "t04-engineering-deviation" /
                          "closure.json")
    readiness = read_json(evidence / "t04-rc3-readiness" / "readiness.json")

    same_event = witness["offline_decode"]
    if not (same_event["operation"] == "COMPLETE" and
            same_event["full_cycles"] ==
            same_event["prefix_cycles"] + same_event["suffix_cycles"] and
            same_event["consistency_failures"] == 0):
        raise ValueError("same-event diagnostic witness is inconsistent")
    if deviation["formal_t04_status"] != "CONFIRMED_VALID_FAIL_1919_OVER_1800":
        raise ValueError("formal T04 failure must remain explicit")
    if deviation["full_critical_engineering_bound_cycles"] != 8064:
        raise ValueError("unexpected T04 full-critical engineering bound")
    if readiness["production_gating"]["production_witness_symbols_present"]:
        raise ValueError("diagnostic witness leaked into production profile")

    domains = [
        ("CLOCK64", "PASS"),
        ("RUNTIME_EVENT_ATOMICITY", "PASS"),
        ("IRQ_AND_TASK_ATTRIBUTION", "PASS"),
        ("CPU_WINDOW_AND_CUTOFF", "PASS"),
        ("TICKSERVICE_Q0", "PASS"),
        ("MEASUREMENT_OVERHEAD", "PASS"),
        ("FORMAL_RESULT_IMMUTABILITY", "PASS"),
        ("R2_R3_REPRESENTATIVE_NONREGRESSION", "PASS"),
        ("EVIDENCE_PROVENANCE", "PASS"),
        ("COMPLETION_TIMING", "PASS_WITH_EXPLICIT_DEVIATION"),
    ]
    return {
        "schema_version": "r4-final-classifier-v1",
        "result": "R4_PRINCIPAL_REVIEW_READY_WITH_T04_ENGINEERING_DEVIATION",
        "formal_t04_status": deviation["formal_t04_status"],
        "principal_acceptance_required": True,
        "hardware_operation": "NONE",
        "automatic_retry": "NONE",
        "verified_pass_attempts": passed,
        "verified_r3_representative_anchors": r3_passed,
        "t04": {
            "same_event_full_cycles": same_event["full_cycles"],
            "same_event_prefix_cycles": same_event["prefix_cycles"],
            "same_event_suffix_cycles": same_event["suffix_cycles"],
            "full_critical_engineering_bound_cycles":
                deviation["full_critical_engineering_bound_cycles"],
            "r6_cost_rule": deviation["cross_stage_constraints"]
                ["r6_real_cost_input"],
            "r7_revalidation_required": deviation["cross_stage_constraints"]
                ["r7_revalidation_required"],
        },
        "domains": [{"domain": name, "status": status} for name, status in domains],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(classify(args.repo.resolve()), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
