from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "r3"))

from r3lib.git_state import parse_porcelain_v1_z
from r3lib.evidence import manifest_path, next_phase, require_phase, validate_host_timeout
from r3lib.lifecycle import (
    LifecycleState, start_allowed, stop_allowed,
    complete_current_lease_allowed, new_claim_allowed,
)

class R3HarnessContractTests(unittest.TestCase):
    def test_porcelain_preserves_xy(self):
        self.assertEqual(
            parse_porcelain_v1_z(b" M firmware/a.c\x00?? new.py\x00"),
            [
                {"xy": " M", "path": "firmware/a.c"},
                {"xy": "??", "path": "new.py"},
            ],
        )

    def test_manifest_contract(self):
        self.assertEqual(manifest_path("raw/a.bin"), "raw/a.bin")
        for bad in ("../a.bin", "/tmp/a.bin", r"C:\tmp\a.bin", "raw//a.bin"):
            with self.assertRaises(ValueError):
                manifest_path(bad)

    def test_phase_resume(self):
        self.assertEqual(next_phase(["H0","H1","H2"]), "H3")
        self.assertEqual(
            require_phase("H3", ["H0","H1","H2","H3"]),
            "H3 ALREADY COMPLETE — DO NOT RERUN",
        )
        with self.assertRaises(ValueError):
            require_phase("H4", ["H0","H1"])

    def test_timeout_precheck(self):
        validate_host_timeout(0.5, 1.0)
        with self.assertRaises(ValueError):
            validate_host_timeout(1.024, 1.0)

    def test_quiescing_allows_current_release_not_new_claim(self):
        self.assertTrue(
            complete_current_lease_allowed(LifecycleState.QUIESCING, True)
        )
        self.assertFalse(
            new_claim_allowed(LifecycleState.QUIESCING, True)
        )

    def test_start_stop_state_contract(self):
        self.assertTrue(start_allowed(LifecycleState.IDLE))
        self.assertFalse(start_allowed(LifecycleState.RUNNING))
        self.assertTrue(stop_allowed(LifecycleState.STARTING))
        self.assertTrue(stop_allowed(LifecycleState.RUNNING))
        self.assertFalse(stop_allowed(LifecycleState.IDLE))

if __name__ == "__main__":
    unittest.main()
