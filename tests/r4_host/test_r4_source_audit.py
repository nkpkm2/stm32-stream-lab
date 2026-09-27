"""Regression checks for R4's production Clock64 authority boundary."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
RUNTIME = REPO / "firmware" / "runtime"
TARGET = RUNTIME / "r4_runtime_target.c"
CLOCK = RUNTIME / "r4_clock64.c"


class R4ClockAuthorityAuditTests(unittest.TestCase):
    def test_only_target_owns_a_production_clock_instance(self) -> None:
        declarations: list[tuple[Path, str]] = []
        for source in RUNTIME.glob("r4_*.c"):
            for match in re.finditer(r"\b(?:static\s+)?R4_Clock64\s+(\w+)\s*;",
                                     source.read_text(encoding="utf-8")):
                declarations.append((source, match.group(1)))
        self.assertEqual(declarations, [(TARGET, "target_clock")])

    def test_target_clock_has_one_boot_initialization_and_no_reset_writer(self) -> None:
        text = TARGET.read_text(encoding="utf-8")
        calls = re.findall(
            r"R4_Clock64_InitializeLocked\s*\(\s*&target_clock\s*,", text)
        self.assertEqual(len(calls), 1)
        self.assertIn("if (target_initialized != 0U)", text)
        self.assertNotRegex(text, r"target_clock\.(?:high_word|last_raw|last_time|"
                           r"read_count|wrap_count|initialized)\s*=")

    def test_clock_extension_state_is_written_only_by_clock_module(self) -> None:
        forbidden = re.compile(r"\btarget_clock\.(?:high_word|last_raw|last_time|"
                               r"read_count|wrap_count|first_error)\s*(?:=|\+\+|--)")
        for source in RUNTIME.glob("r4_*.c"):
            if source == CLOCK:
                continue
            self.assertNotRegex(source.read_text(encoding="utf-8"), forbidden,
                                msg=f"Clock64 extension state writer in {source}")

    def test_only_target_initializes_production_clock64(self) -> None:
        initializers: list[Path] = []
        for source in RUNTIME.glob("*.c"):
            if re.search(r"R4_Clock64_InitializeLocked\s*\(",
                         source.read_text(encoding="utf-8")) and source != CLOCK:
                initializers.append(source)
        self.assertEqual(initializers, [TARGET])


if __name__ == "__main__":
    unittest.main()
