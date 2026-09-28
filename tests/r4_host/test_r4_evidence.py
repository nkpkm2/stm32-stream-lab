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
        words = [0] * (EVIDENCE.SYNTHETIC_SYSTICK_IRQ_CYCLES_WORD + 2)
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
        words[EVIDENCE.SYNTHETIC_WINDOW_IRQ_WORD] = 10
        words[EVIDENCE.SYNTHETIC_WINDOW_IDLE_WORD] = 200
        words[EVIDENCE.WINDOW_CYCLES_WORD] = 1210
        words[EVIDENCE.TASK_TRACE_FIRST_IN_WORD] = 1
        words[EVIDENCE.TASK_TRACE_OUT_TOTAL_WORD] = 2
        words[EVIDENCE.TASK_TRACE_IN_TOTAL_WORD] = 3
        words[EVIDENCE.TASK_TRACE_IDLE_IN_WORD] = 1
        words[EVIDENCE.TASK_TRACE_FIRST_IDENTITY_WORD] = 1
        words[EVIDENCE.SYNTHETIC_SYSTICK_IRQ_CYCLES_WORD] = 10
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
        words = [0] * 246
        words[:11] = [EVIDENCE.MAGIC, 9, EVIDENCE.SCHEMA, 1, 0, 0, 0, 0,
                      0, 0, EVIDENCE.COMPLETE]
        words[EVIDENCE.MICROBENCH_SAMPLE_COUNT_WORD] = 33
        words[EVIDENCE.MICROBENCH_FAILURE_COUNT_WORD] = 0
        words[EVIDENCE.MICROBENCH_LAST_STATUS_WORD] = 0
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

    def test_microbenchmark_rejects_a_faulted_sample(self) -> None:
        words = [0] * 246
        words[:11] = [EVIDENCE.MAGIC, 9, EVIDENCE.SCHEMA, 1, 0, 0, 0, 0,
                      0, 0, EVIDENCE.COMPLETE]
        words[EVIDENCE.MICROBENCH_SAMPLE_COUNT_WORD] = 33
        words[EVIDENCE.MICROBENCH_FAILURE_COUNT_WORD] = 1
        words[EVIDENCE.MICROBENCH_LAST_STATUS_WORD] = 9
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
        self.assertEqual(verdict["result"], "FAIL")
        self.assertFalse(verdict["checks"]["runtime_event_microbenchmark"])

    def test_mask_restore_requires_both_primask_entrance_states(self) -> None:
        words = [0] * 206
        words[:11] = [EVIDENCE.MAGIC, 10, EVIDENCE.SCHEMA, 1, 0, 0, 0, 0,
                      0, 0, EVIDENCE.COMPLETE]
        words[EVIDENCE.MASK_MASKED_PRIMASK_BEFORE_WORD] = 1
        words[EVIDENCE.MASK_MASKED_PRIMASK_AFTER_WORD] = 1
        verdict = EVIDENCE.evaluate("mask-restore", words)
        self.assertEqual(verdict["result"], "PASS")
        self.assertTrue(verdict["checks"]["runtime_event_mask_restore"])

    def test_time_regression_requires_latched_rejection(self) -> None:
        words = [0] * 212
        words[:11] = [EVIDENCE.MAGIC, 11, EVIDENCE.SCHEMA, 1, 0, 0, 0, 0,
                      0, 0, EVIDENCE.COMPLETE]
        words[EVIDENCE.TIME_REGRESSION_INJECT_STATUS_WORD] = 5
        words[EVIDENCE.TIME_REGRESSION_POST_STATUS_WORD] = 5
        words[EVIDENCE.TIME_REGRESSION_SERIAL_BEFORE_WORD] = 7
        words[EVIDENCE.TIME_REGRESSION_SERIAL_AFTER_WORD] = 7
        verdict = EVIDENCE.evaluate("time-regression", words)
        self.assertEqual(verdict["result"], "PASS")
        self.assertTrue(verdict["checks"]["time_regression_fail_closed"])

    def test_completion_pending_irq_must_observe_unlock(self) -> None:
        words = [0] * 217
        words[:11] = [EVIDENCE.MAGIC, 12, EVIDENCE.SCHEMA, 1, 0, 0, 0, 0,
                      0, 0, EVIDENCE.COMPLETE]
        words[EVIDENCE.COMMIT_PENDING_ARM_COUNT_WORD] = 1
        words[EVIDENCE.COMMIT_PENDING_IRQ_COUNT_WORD] = 1
        verdict = EVIDENCE.evaluate("commit-pending-irq", words)
        self.assertEqual(verdict["result"], "PASS")
        self.assertTrue(verdict["checks"]["commit_pending_irq_after_unlock"])

    def test_target_window_intersection_requires_partition_and_seal(self) -> None:
        words = [0] * 226
        words[:11] = [EVIDENCE.MAGIC, 13, EVIDENCE.SCHEMA, 1, 0, 0, 0, 0,
                      0, 0, EVIDENCE.COMPLETE]
        words[EVIDENCE.WINDOW_CYCLES_WORD] = 10
        words[EVIDENCE.T17_WINDOW_AT_CLOSE_WORD] = 10
        words[EVIDENCE.T17_WINDOW_AFTER_CLOSE_WORD] = 10
        words[EVIDENCE.TARGET_WINDOW_TASK_WORD] = 4
        words[EVIDENCE.TARGET_WINDOW_IRQ_WORD] = 3
        words[EVIDENCE.TARGET_WINDOW_IDLE_WORD] = 2
        words[EVIDENCE.TARGET_WINDOW_UNCLASSIFIED_WORD] = 1
        verdict = EVIDENCE.evaluate("window-intersection", words)
        self.assertEqual(verdict["result"], "PASS")
        self.assertTrue(verdict["checks"]["target_window_conservation"])

    def test_cutoff_live_requires_nested_preclose_and_live_progress(self) -> None:
        words = [0] * 414
        words[:11] = [EVIDENCE.MAGIC, 20, EVIDENCE.SCHEMA, 1, 0, 0, 0, 0,
                      0, 0, EVIDENCE.COMPLETE]
        words[EVIDENCE.CUTOFF_WINDOW_SEALED_WORD] = 1
        words[EVIDENCE.CUTOFF_FORMAL_WINDOW_BEFORE_WORD] = 100
        words[EVIDENCE.CUTOFF_FORMAL_WINDOW_AFTER_WORD] = 100
        words[EVIDENCE.CUTOFF_LIVE_BEFORE_WORD] = 100
        words[EVIDENCE.CUTOFF_LIVE_AFTER_WORD] = 120
        words[EVIDENCE.CUTOFF_LOW_IRQ_CYCLES_WORD] = 10
        words[EVIDENCE.CUTOFF_HIGH_IRQ_CYCLES_WORD] = 10
        verdict = EVIDENCE.evaluate("cutoff-live", words)
        self.assertEqual(verdict["result"], "PASS")
        self.assertTrue(verdict["checks"]["cutoff_formal_window_frozen_once"])
        self.assertTrue(verdict["checks"]["cutoff_live_continues_after_close"])

    def test_response_synthetic_requires_independent_wall_and_owner_evidence(self) -> None:
        words = [0] * 234
        words[:11] = [EVIDENCE.MAGIC, 14, EVIDENCE.SCHEMA, 1, 0, 0, 0, 0,
                      0, 0, EVIDENCE.COMPLETE]
        words[EVIDENCE.RESPONSE_CREATED_WORD] = 1
        words[EVIDENCE.RESPONSE_DONE_WORD] = 1
        words[EVIDENCE.RESPONSE_RELEASE_RAW_WORD] = 100
        words[EVIDENCE.RESPONSE_START_RAW_WORD] = 120
        words[EVIDENCE.RESPONSE_COMPLETE_RAW_WORD] = 220
        words[EVIDENCE.RESPONSE_WORK_RAW_WORD] = 100
        words[EVIDENCE.RESPONSE_OWNER_CYCLES_WORD] = 110
        verdict = EVIDENCE.evaluate("response-synthetic", words)
        self.assertEqual(verdict["result"], "PASS")
        self.assertTrue(verdict["checks"]["response_direct_wall_endpoints"])
        self.assertTrue(verdict["checks"]["response_owner_known_work_coverage"])

    def test_mask_timing_requires_actual_primask_span_bound(self) -> None:
        words = [0] * 240
        words[:11] = [EVIDENCE.MAGIC, 15, EVIDENCE.SCHEMA, 1, 0, 0, 0, 0,
                      0, 0, EVIDENCE.COMPLETE]
        words[EVIDENCE.MASK_TIMING_SAMPLE_COUNT_WORD] = 33
        words[EVIDENCE.MASK_TIMING_MAX_WORD] = 1200
        words[EVIDENCE.MASK_TIMING_LIMIT_WORD] = 1800
        verdict = EVIDENCE.evaluate("mask-timing", words)
        self.assertEqual(verdict["result"], "PASS")
        self.assertTrue(verdict["checks"]["runtime_event_masked_span_bound"])

    def test_combined_service_requires_real_dma_margin(self) -> None:
        words = [0] * 244
        words[:11] = [EVIDENCE.MAGIC, 16, EVIDENCE.SCHEMA, 1, 0, 0, 0, 0,
                      0, 0, EVIDENCE.COMPLETE]
        words[25:28] = [0, 0, 0]
        words[EVIDENCE.T12_SOAK_CONFIGURED_MS_WORD] = 65000
        words[EVIDENCE.T12_DMA_IRQ_COUNT_WORD] = 100
        words[EVIDENCE.DMA_SERVICE_COUNT_WORD] = 100
        words[EVIDENCE.DMA_SERVICE_MAX_WORD] = 23040
        verdict = EVIDENCE.evaluate("combined-service", words)
        self.assertEqual(verdict["result"], "PASS")
        self.assertTrue(verdict["checks"]["combined_dma_service_margin"])

    def test_perturbation_requires_real_worker_population_and_profile_semantics(self) -> None:
        words = [0] * (EVIDENCE.PERTURBATION_RESPONSE_SAMPLES_WORD + 99)
        words[:11] = [EVIDENCE.MAGIC, 17, EVIDENCE.SCHEMA, 1, 0, 0, 0, 0,
                      0, 0, EVIDENCE.COMPLETE]
        words[25:28] = [0, 0, 0]
        words[EVIDENCE.WINDOW_CYCLES_WORD] = 100
        words[EVIDENCE.TARGET_WINDOW_TASK_WORD] = 40
        words[EVIDENCE.TARGET_WINDOW_IRQ_WORD] = 30
        words[EVIDENCE.TARGET_WINDOW_IDLE_WORD] = 20
        words[EVIDENCE.TARGET_WINDOW_UNCLASSIFIED_WORD] = 10
        words[EVIDENCE.PERTURBATION_PROFILE_WORD] = 1
        words[EVIDENCE.PERTURBATION_ACCOUNTING_WORD] = 1
        words[EVIDENCE.PERTURBATION_DMA_COUNT_WORD] = 50000
        words[EVIDENCE.PERTURBATION_DMA_MAX_WORD] = 23040
        words[EVIDENCE.PERTURBATION_DRIVER_COMPLETIONS_WORD] = 100
        words[EVIDENCE.PERTURBATION_DRIVER_REBIND_WORD] = 100
        words[EVIDENCE.PERTURBATION_PROCESSING_WAKE_WORD] = 100
        words[EVIDENCE.PERTURBATION_PROCESSING_COMPLETE_WORD] = 100
        words[EVIDENCE.PERTURBATION_RESPONSE_RELEASE_COUNT_WORD] = 33
        words[EVIDENCE.PERTURBATION_RESPONSE_COMPLETE_COUNT_WORD] = 33
        for index in range(EVIDENCE.PERTURBATION_RESPONSE_SAMPLE_COUNT):
            offset = EVIDENCE.PERTURBATION_RESPONSE_SAMPLES_WORD + (index * 3)
            words[offset:offset + 3] = [100, 120, 220]
        verdict = EVIDENCE.evaluate("perturbation-ab", words, "R4")
        self.assertEqual(verdict["result"], "PASS")
        self.assertTrue(verdict["checks"]["perturbation_fixed_real_worker_response_population"])
        words[EVIDENCE.PERTURBATION_PROFILE_WORD] = 0
        verdict = EVIDENCE.evaluate("perturbation-ab", words, "R4")
        self.assertEqual(verdict["result"], "FAIL")

    def test_r2_anchor_requires_a_post_stop_ownership_witness(self) -> None:
        words = [0] * (EVIDENCE.R2_POST_STOP_REPORT_VALID_WORD + 1)
        words[:11] = [EVIDENCE.MAGIC, 18, EVIDENCE.SCHEMA, 1, 0, 0, 0, 0,
                      0, 0, EVIDENCE.COMPLETE]
        words[25:28] = [0, 0, 0]
        words[EVIDENCE.T12_SOAK_CONFIGURED_MS_WORD] = 65000
        words[EVIDENCE.T12_DMA_IRQ_COUNT_WORD] = 50000
        words[EVIDENCE.T12_HEALTH_STATUS_WORD] = 0
        words[EVIDENCE.PERTURBATION_DRIVER_COMPLETIONS_WORD] = 50000
        words[EVIDENCE.PERTURBATION_DRIVER_REBIND_WORD] = 1
        words[EVIDENCE.PERTURBATION_PROCESSING_WAKE_WORD] = 1
        words[EVIDENCE.PERTURBATION_PROCESSING_COMPLETE_WORD] = 1
        words[EVIDENCE.R2_POST_STOP_ACK_MASK_WORD] = 0x300
        words[EVIDENCE.R2_POST_STOP_BEGIN_VALID_WORD] = 1
        words[EVIDENCE.R2_POST_STOP_REPORT_VALID_WORD] = 1
        verdict = EVIDENCE.evaluate("r2-normal-anchor", words)
        self.assertEqual(verdict["result"], "PASS")
        self.assertTrue(verdict["checks"]["r2_post_stop_quiescent"])
        words[EVIDENCE.R2_POST_STOP_HARDWARE_OWNED_WORD] = 1
        verdict = EVIDENCE.evaluate("r2-normal-anchor", words)
        self.assertEqual(verdict["result"], "FAIL")
        self.assertFalse(verdict["checks"]["r2_post_stop_quiescent"])
