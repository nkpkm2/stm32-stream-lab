#!/usr/bin/env python3
"""Create the immutable host-side R6 prediction artifact before a run.

The MCU validates the binding values but never asserts that a PC file exists;
this tool owns that persistence boundary and emits a self-hashing JSON record.
"""
import argparse
import hashlib
import json
from pathlib import Path


def canonical_hash(payload: dict) -> str:
    encoded = json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    binding = {
        "prediction_artifact_id": "0x202609270001",
        "resolved_config_hash": "0x1001", "model_hash": "0x1002",
        "calibration_hash": "0x1003", "instrumentation_profile_hash": "0x1004",
        "platform_profile_hash": "0x1005", "phase_window_hash": "0x1006",
        "result_schema_hash": "0x1007",
    }
    payload = {
        "schema": "stm32-stream-lab.r6.prediction.v1",
        "run_kind": "PERFORMANCE", "purpose": "VALIDATION",
        "prediction_binding": "BOUND", "binding": binding,
        "tie_order": "FREE_VISIBLE_BEFORE_ADMISSION_AT_EQUAL_CYCLE",
        "cost_key": {
            "pipeline_id": "0x10", "coefficient_version": "0x11",
            "window_version": "0x12", "feature_set": "0x13", "datatype": "0x14",
            "compile_flags": "0x15", "library_version": "0x16",
            "dac_profile": "0x17", "instrumentation_profile": "0x18",
        },
        "prediction": {
            "t10": {"nominal": 100, "free_visible": 105, "admission": 110,
                    "lock": 115, "admitted": True},
            "unlock_before_epilogue": {"unlock": 110, "next_admission": 120,
                    "processing_available": 160, "next_processing_start": 160},
        },
    }
    record = {"payload": payload, "payload_sha256": canonical_hash(payload)}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, sort_keys=True, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
