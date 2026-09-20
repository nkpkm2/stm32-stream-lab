from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS_R3 = ROOT / "tools" / "r3"
if str(TOOLS_R3) not in sys.path:
    sys.path.insert(0, str(TOOLS_R3))

from r3lib.cases import CASES
from r3lib.control_state import derive_progress
from r3lib.w2_hw import (
    W2_HW_CASES,
    W2_NATIVE_ONLY_CASES,
    W2_HW_HOST_TIMEOUT_S,
    W2_HW_TARGET_BOUND_S,
    W2_HW_CASE_CACHE_VARIABLE,
    W2_HW_CASE_SELECTORS,
    case_partition_ok,
    case_selector_contract_ok,
    derive_w2_hw_preflight,
    w2_case_partition,
)
from r3lib.evidence import validate_host_timeout


class W2HardwarePreflightTests(unittest.TestCase):
    def test_case_partition_exact(self):
        hardware, native_only = w2_case_partition(CASES)
        self.assertEqual(hardware, W2_HW_CASES)
        self.assertEqual(native_only, W2_NATIVE_ONLY_CASES)
        self.assertTrue(case_partition_ok())

    def test_progress_before_freeze_stays_preflight(self):
        p = derive_progress(
            w1_sealed=True,
            w2a_sealed=True,
            w2b_sealed=True,
            w2_evidence_present=False,
            w2_hw_freeze_sealed=False,
        )
        self.assertEqual(p.w2_hw_harness_freeze, "NOT_STARTED")
        self.assertEqual(
            p.next_allowed,
            "W2_DIRECTED_HARDWARE_HARNESS_PREFLIGHT",
        )
        self.assertFalse(p.target_firmware_modification_allowed)

    def test_progress_after_freeze_allows_only_harness_implementation(self):
        p = derive_progress(
            w1_sealed=True,
            w2a_sealed=True,
            w2b_sealed=True,
            w2_evidence_present=False,
            w2_hw_freeze_sealed=True,
        )
        self.assertEqual(p.w2_hw_harness_freeze, "SEALED")
        self.assertEqual(p.next_allowed, "W2_HW_HARNESS_IMPLEMENTATION")
        self.assertTrue(p.target_firmware_modification_allowed)

    def test_preflight_ready_only_for_clean_implementation_gap(self):
        result = derive_w2_hw_preflight(
            freeze_committed=True,
            worker_impl_present=True,
            case_partition_valid=True,
            target_profile_present=False,
            target_harness_present=False,
            evidence_present=False,
            synthetic_irq_available=True,
            irq_priority_contract_ok=True,
            worker_isr_api_present=True,
            authority_link_contract_ok=True,
            authority_link_substitution_required=True,
            case_selector_contract_valid=True,
            result_commit_protocol_frozen=True,
        )
        self.assertTrue(result.ready_for_implementation)
        self.assertEqual(result.next_allowed, "W2_HW_HARNESS_IMPLEMENTATION")

    def test_preflight_blocks_if_target_work_started_early(self):
        result = derive_w2_hw_preflight(
            freeze_committed=True,
            worker_impl_present=True,
            case_partition_valid=True,
            target_profile_present=True,
            target_harness_present=False,
            evidence_present=False,
            synthetic_irq_available=True,
            irq_priority_contract_ok=True,
            worker_isr_api_present=True,
            authority_link_contract_ok=True,
            authority_link_substitution_required=True,
            case_selector_contract_valid=True,
            result_commit_protocol_frozen=True,
        )
        self.assertFalse(result.ready_for_implementation)
        self.assertEqual(result.next_allowed, "PRINCIPAL_W2_HW_PREFLIGHT_REVIEW")

    def test_case_selector_contract_exact(self):
        self.assertEqual(W2_HW_CASE_CACHE_VARIABLE, "STREAM_LAB_R3_W2_HW_CASE")
        self.assertTrue(case_selector_contract_ok())
        self.assertEqual(
            tuple(cid for cid, _selector in W2_HW_CASE_SELECTORS),
            W2_HW_CASES,
        )

    def test_preflight_blocks_without_authority_link_contract(self):
        result = derive_w2_hw_preflight(
            freeze_committed=True,
            worker_impl_present=True,
            case_partition_valid=True,
            target_profile_present=False,
            target_harness_present=False,
            evidence_present=False,
            synthetic_irq_available=True,
            irq_priority_contract_ok=True,
            worker_isr_api_present=True,
            authority_link_contract_ok=False,
            authority_link_substitution_required=True,
            case_selector_contract_valid=True,
            result_commit_protocol_frozen=True,
        )
        self.assertFalse(result.ready_for_implementation)

    def test_preflight_blocks_without_result_commit_protocol(self):
        result = derive_w2_hw_preflight(
            freeze_committed=True,
            worker_impl_present=True,
            case_partition_valid=True,
            target_profile_present=False,
            target_harness_present=False,
            evidence_present=False,
            synthetic_irq_available=True,
            irq_priority_contract_ok=True,
            worker_isr_api_present=True,
            authority_link_contract_ok=True,
            authority_link_substitution_required=True,
            case_selector_contract_valid=True,
            result_commit_protocol_frozen=False,
        )
        self.assertFalse(result.ready_for_implementation)

    def test_preflight_blocks_without_case_selector_contract(self):
        result = derive_w2_hw_preflight(
            freeze_committed=True,
            worker_impl_present=True,
            case_partition_valid=True,
            target_profile_present=False,
            target_harness_present=False,
            evidence_present=False,
            synthetic_irq_available=True,
            irq_priority_contract_ok=True,
            worker_isr_api_present=True,
            authority_link_contract_ok=True,
            authority_link_substitution_required=True,
            case_selector_contract_valid=False,
            result_commit_protocol_frozen=True,
        )
        self.assertFalse(result.ready_for_implementation)

    def test_timeout_policy_has_margin(self):
        validate_host_timeout(W2_HW_TARGET_BOUND_S, W2_HW_HOST_TIMEOUT_S)
        self.assertGreaterEqual(W2_HW_HOST_TIMEOUT_S, 2.0 * W2_HW_TARGET_BOUND_S)


if __name__ == "__main__":
    unittest.main()
