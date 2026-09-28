#!/usr/bin/env python3
"""Read-only R5 closure classifier.

It deliberately does not build, flash, write evidence, or make Git changes.
It reports the current hard-gate state from versioned source/evidence only.
"""
from __future__ import annotations

import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def present(relative: str) -> bool:
    return (ROOT / relative).is_file()


def git_head() -> str | None:
    result = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                            text=True, capture_output=True, check=False)
    return result.stdout.strip() if result.returncode == 0 else None


def main() -> int:
    checks = {
        "schema_contract": present("docs/r5/R5_SEMANTIC_CONTRACT.md"),
        "current_state": present("docs/r5/R5_CURRENT_STATE.md"),
        "known_truth_evidence": present("docs/evidence/r5/W5_HOST_KNOWN_TRUTH.md"),
        "target_build_evidence": present("docs/evidence/r5/W5_TARGET_BUILD.md"),
        "shared_stack_build_evidence": present("docs/evidence/r5/W6_STACK_SOURCE_AUDIT.md"),
        "action_packet": present("docs/r5/R5_W6_ACTION_PACKET.md"),
        "native_core": present("firmware/runtime/r5_run_metrics.c"),
        "result_store": present("firmware/runtime/r5_result_store.c"),
        "golden_vectors": present("tools/r5/r5_golden_vectors.py"),
        "real_dma_bridge": present("firmware/runtime/r5_runtime_bridge.c"),
        "hardware_attempt": (ROOT / "docs/evidence/r5/hardware").is_dir(),
    }
    missing_integration = not checks["real_dma_bridge"]
    hardware_blocked = not checks["hardware_attempt"]
    result = {
        "head": git_head(),
        "checks": checks,
        "formal_r5_acceptance": "BLOCKED",
        "principal_review_ready": False,
        "blockers": [
            *(["real DMA/CompletionAdapter/lifecycle bridge is absent"] if missing_integration else []),
            *(["no authorized formal hardware attempt"] if hardware_blocked else []),
            "R4 formal closure remains a prerequisite for R5 formal hardware acceptance",
        ],
    }
    print(json.dumps(result, sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
