"""Host-only checks for the R4 immutable target-evidence reader."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("r4_evidence", ROOT / "tools/r4/r4_evidence.py")
assert SPEC is not None and SPEC.loader is not None
EVIDENCE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EVIDENCE)


class R4EvidenceTests(unittest.TestCase):
    def test_stable_prefix_accepts_matching_target_result(self) -> None:
        words = [EVIDENCE.MAGIC, 1, EVIDENCE.SCHEMA, 1, 0, 0, 0, 0, 0, 0,
                 EVIDENCE.COMPLETE]
        self.assertEqual(EVIDENCE.evaluate("t12-soak-a", words)["result"], "PASS")

    def test_stable_prefix_rejects_faulted_target_result(self) -> None:
        words = [EVIDENCE.MAGIC, 1, EVIDENCE.SCHEMA, 1, 0x20, 0, 0, 0, 0, 0,
                 EVIDENCE.COMPLETE]
        verdict = EVIDENCE.evaluate("t12-soak-a", words)
        self.assertEqual(verdict["result"], "FAIL")
        self.assertFalse(verdict["checks"]["no_invariant_failure"])

    def test_synthetic_case_requires_machine_readable_extension(self) -> None:
        words = [0] * 139
        words[:11] = [EVIDENCE.MAGIC, 6, EVIDENCE.SCHEMA, 1, 0, 0, 0, 0,
                      0, 0, EVIDENCE.COMPLETE]
        words[EVIDENCE.SYNTHETIC_CREATE_MASK_WORD] = 3
        words[EVIDENCE.SYNTHETIC_SCHEMA_WORD] = 1
        words[EVIDENCE.SYNTHETIC_DONE_MASK_WORD] = 3
        words[EVIDENCE.SYNTHETIC_A_ITERATIONS_WORD] = 50000
        words[EVIDENCE.SYNTHETIC_B_ITERATIONS_WORD] = 10000
        words[EVIDENCE.SYNTHETIC_A_CYCLES_WORD] = 300
        words[EVIDENCE.SYNTHETIC_B_CYCLES_WORD] = 100
        words[EVIDENCE.SYNTHETIC_WINDOW_TASK_WORD] = 1000
        words[EVIDENCE.SYNTHETIC_WINDOW_IDLE_WORD] = 200
        words[19] = 1200
        verdict = EVIDENCE.evaluate("task-synthetic", words)
        self.assertEqual(verdict["result"], "PASS")
        self.assertTrue(verdict["checks"]["synthetic_conservation"])
