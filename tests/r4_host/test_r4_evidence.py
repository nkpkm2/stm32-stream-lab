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
        words = [EVIDENCE.MAGIC, 5, EVIDENCE.SCHEMA, 1, 0, 0, 0, 0, 0, 0,
                 EVIDENCE.COMPLETE]
        self.assertEqual(EVIDENCE.evaluate("t04-commit-budget-a", words)["result"], "PASS")

    def test_stable_prefix_rejects_faulted_target_result(self) -> None:
        words = [EVIDENCE.MAGIC, 1, EVIDENCE.SCHEMA, 1, 0x20, 0, 0, 0, 0, 0,
                 EVIDENCE.COMPLETE]
        verdict = EVIDENCE.evaluate("t12-soak-a", words)
        self.assertEqual(verdict["result"], "FAIL")
        self.assertFalse(verdict["checks"]["no_invariant_failure"])

    def test_synthetic_case_requires_machine_readable_extension(self) -> None:
        words = [0] * 140
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
        words[EVIDENCE.WINDOW_CYCLES_WORD] = 1200
        verdict = EVIDENCE.evaluate("task-synthetic", words)
        self.assertEqual(verdict["result"], "PASS")
        self.assertTrue(verdict["checks"]["synthetic_conservation"])

    def test_t17_case_requires_atomicity_and_nesting_witnesses(self) -> None:
        words = [0] * 62
        words[:11] = [EVIDENCE.MAGIC, 4, EVIDENCE.SCHEMA, 1, 0, 0, 0, 0,
                      0, 0, EVIDENCE.COMPLETE]
        words[EVIDENCE.T17_SERIAL_BEFORE_WORD] = 2
        words[EVIDENCE.T17_SERIAL_AFTER_PENDING_WORD] = 5
        words[EVIDENCE.T17_HIGH_IRQ_AFTER_WORD] = 1
        words[EVIDENCE.T17_LOW_IRQ_AFTER_WORD] = 1
        words[EVIDENCE.T17_DUPLICATE_EXIT_STATUS_WORD] = 7
        words[EVIDENCE.T17_WINDOW_AT_CLOSE_WORD] = 10
        words[EVIDENCE.T17_WINDOW_AFTER_CLOSE_WORD] = 10
        verdict = EVIDENCE.evaluate("t17-atomic", words)
        self.assertEqual(verdict["result"], "PASS")
        self.assertTrue(verdict["checks"]["t17_real_nested_irq"])

    def test_t12_case_requires_real_dma_paths_and_wrap_service(self) -> None:
        words = [0] * 122
        words[:11] = [EVIDENCE.MAGIC, 1, EVIDENCE.SCHEMA, 1, 0, 0, 0, 0,
                      0, 0, EVIDENCE.COMPLETE]
        words[25:28] = [0, 0, 0]
        words[EVIDENCE.T12_SOAK_CONFIGURED_MS_WORD] = 60000
        words[EVIDENCE.T12_SOAK_END_HIGH_WORD] = 2
        words[EVIDENCE.T12_DMA_IRQ_COUNT_WORD] = 2
        words[EVIDENCE.T12_DMA_YIELD_COUNT_WORD] = 1
        words[EVIDENCE.T12_NO_EVENT_IRQ_AFTER_WORD] = 1
        words[EVIDENCE.T12_NO_EVENT_NO_YIELD_AFTER_WORD] = 1
        words[EVIDENCE.T12_HEALTH_SERVICE_COUNT_WORD] = 60
        words[EVIDENCE.T12_HEALTH_INTERVAL_LIMIT_WORD] = 100
        verdict = EVIDENCE.evaluate("t12-soak-a", words)
        self.assertEqual(verdict["result"], "PASS")
        self.assertTrue(verdict["checks"]["t12_dma_no_event_path"])

    def test_t15_release_requires_one_skip_not_catchup(self) -> None:
        words = [0] * 163
        words[:11] = [EVIDENCE.MAGIC, 3, EVIDENCE.SCHEMA, 1, 0, 0, 0, 0,
                      0, 0, EVIDENCE.COMPLETE]
        words[EVIDENCE.T15_START_CALLBACK_WORD] = 1
        words[EVIDENCE.T15_RELEASE_CALLBACK_WORD] = 1
        words[EVIDENCE.T15_SERVICE_SEQ_WORD] = 1
        words[EVIDENCE.T15_SEQUENCE_AFTER_SUSPENSION_WORD] = 1
        words[EVIDENCE.T15_START_COUNT_WORD] = 1
        words[EVIDENCE.T15_RELEASE_COUNT_WORD] = 1
        words[EVIDENCE.T15_SKIPPED_COUNT_WORD] = 1
        words[EVIDENCE.T15_TIMING_SERVICE_COUNT_WORD] = 1
        words[EVIDENCE.T15_TIMING_MAX_INTERVAL_WORD] = 1
        words[EVIDENCE.T15_SYSTICK_ENTER_WORD] = 1
        words[EVIDENCE.T15_SYSTICK_EXIT_WORD] = 1
        words[EVIDENCE.T15_PHASE_Q0_WORD] = 1
        words[EVIDENCE.T15_PHASE_TIM2_AFTER_WORD] = 1
        words[EVIDENCE.T15_PHASE_TIM2_CEN_WORD] = 1
        verdict = EVIDENCE.evaluate("t15-release", words)
        self.assertEqual(verdict["result"], "PASS")
        self.assertTrue(verdict["checks"]["t15_exact_callbacks"])
        self.assertTrue(verdict["checks"]["t15_systick_single_pair"])
        self.assertTrue(verdict["checks"]["t15_q0_to_tim2_phase"])

    def test_tick_gap_requires_real_fault_latch(self) -> None:
        words = [0] * 172
        words[:11] = [EVIDENCE.MAGIC, 8, EVIDENCE.SCHEMA, 1, 0, 0, 0, 0,
                      0, 0, EVIDENCE.COMPLETE]
        words[EVIDENCE.TICK_GAP_FIRST_FAULT_WORD] = 1
        words[EVIDENCE.TICK_GAP_FAIL_CLOSED_WORD] = 1
        words[EVIDENCE.TICK_GAP_OVER_LIMIT_WORD] = 1
        words[EVIDENCE.TICK_GAP_MAX_INTERVAL_WORD] = 271
        words[EVIDENCE.TICK_GAP_INTERVAL_LIMIT_WORD] = 270
        verdict = EVIDENCE.evaluate("tick-gap", words)
        self.assertEqual(verdict["result"], "PASS")
        self.assertTrue(verdict["checks"]["tick_gap_fail_closed"])

    def test_microbenchmark_requires_all_runtime_event_paths(self) -> None:
        words = [0] * 198
        words[:11] = [EVIDENCE.MAGIC, 9, EVIDENCE.SCHEMA, 1, 0, 0, 0, 0,
                      0, 0, EVIDENCE.COMPLETE]
        words[EVIDENCE.MICROBENCH_SAMPLE_COUNT_WORD] = 33
        for low, median, high in (
                (EVIDENCE.MICROBENCH_TASK_MIN_WORD,
                 EVIDENCE.MICROBENCH_TASK_MEDIAN_WORD,
                 EVIDENCE.MICROBENCH_TASK_MAX_WORD),
                (EVIDENCE.MICROBENCH_IRQ_MIN_WORD,
                 EVIDENCE.MICROBENCH_IRQ_MEDIAN_WORD,
                 EVIDENCE.MICROBENCH_IRQ_MAX_WORD),
                (EVIDENCE.MICROBENCH_WINDOW_MIN_WORD,
                 EVIDENCE.MICROBENCH_WINDOW_MEDIAN_WORD,
                 EVIDENCE.MICROBENCH_WINDOW_MAX_WORD),
                (EVIDENCE.MICROBENCH_NESTED_MIN_WORD,
                 EVIDENCE.MICROBENCH_NESTED_MEDIAN_WORD,
                 EVIDENCE.MICROBENCH_NESTED_MAX_WORD)):
            words[low], words[median], words[high] = 1, 2, 3
        verdict = EVIDENCE.evaluate("microbenchmark", words)
        self.assertEqual(verdict["result"], "PASS")
        self.assertTrue(verdict["checks"]["runtime_event_microbenchmark"])
