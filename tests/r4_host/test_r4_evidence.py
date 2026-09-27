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
