from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/r3"))

from r3lib.w2_hardware import COMPLETED_MAGIC, RESULT_MAGIC, WorkflowError, evaluate, parse_words


def target_dump(words: list[int]) -> str:
    rows = []
    for index in range(0, len(words), 4):
        rows.append(f"0x20000000 : " + " ".join(f"{x:08X}" for x in words[index:index + 4]))
    return "\n".join(rows)


class W2HardwareEvidenceTests(unittest.TestCase):
    def test_complete_pass_record(self):
        words = [0] * 37
        words[0], words[1], words[2], words[3], words[-1] = RESULT_MAGIC, 1, 1, 1, COMPLETED_MAGIC
        parsed = parse_words(target_dump(words))
        self.assertEqual(evaluate("W2-T03-A", parsed)["result"], "PASS")

    def test_fault_or_bad_completion_fails(self):
        words = [0] * 37
        words[0], words[1], words[2], words[3], words[4], words[-1] = RESULT_MAGIC, 1, 2, 1, 9, COMPLETED_MAGIC
        self.assertEqual(evaluate("W2-T03-C", parse_words(target_dump(words)))["result"], "FAIL")

    def test_short_read_is_rejected(self):
        with self.assertRaises(WorkflowError):
            parse_words("0x20000000 : 00000000")


if __name__ == "__main__":
    unittest.main()
