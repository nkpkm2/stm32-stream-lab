from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "r3"))

from r3lib.w3_hardware import (
    COMPLETED_MAGIC, RESULT_MAGIC, CASE_TO_SELECTOR, evaluate, manifest, validate_manifest,
)


def start_a_words() -> list[int]:
    words = [0] * 21
    words[0:4] = [RESULT_MAGIC, 1, 1, 1]
    words[5] = 2
    words[7:10] = [1, 1, 1]
    words[10:12] = [3, 1]
    words[12] = 1
    words[20] = COMPLETED_MAGIC
    return words


def rollback_words(case_id: int) -> list[int]:
    words = [0] * 21
    words[0:4] = [RESULT_MAGIC, 1, case_id, 1]
    words[5] = 0
    words[16] = 0x300
    words[19] = 1
    words[20] = COMPLETED_MAGIC
    return words


class W3HardwareResultTests(unittest.TestCase):
    def test_start_a_requires_running_before_pass(self):
        self.assertEqual(evaluate("W3-START-A", start_a_words())["result"], "PASS")
        words = start_a_words()
        words[12] = 0
        self.assertEqual(evaluate("W3-START-A", words)["result"], "FAIL")

    def test_all_rollback_cases_require_quiescence_and_acks(self):
        for case, case_id in (("W3-T06-A", 2), ("W3-T06-B", 3), ("W3-START-C", 4)):
            self.assertEqual(evaluate(case, rollback_words(case_id))["result"], "PASS")
            words = rollback_words(case_id)
            words[11] = 1
            self.assertEqual(evaluate(case, words)["result"], "FAIL")

    def test_manifest_rejects_tampered_attempt(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "H0.json").write_text("{}\n", encoding="utf-8")
            manifest(root)
            validate_manifest(root)
            (root / "H0.json").write_text("{\"changed\":true}\n", encoding="utf-8")
            with self.assertRaises(RuntimeError):
                validate_manifest(root)

    def test_hardware_selector_catalog_is_complete(self):
        self.assertEqual(set(CASE_TO_SELECTOR), {
            "W3-START-A", "W3-T06-A", "W3-T06-B", "W3-START-C",
        })


if __name__ == "__main__":
    unittest.main()
