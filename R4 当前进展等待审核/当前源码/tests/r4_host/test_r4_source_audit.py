"""Regression checks for R4's production Clock64 authority boundary."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
RUNTIME = REPO / "firmware" / "runtime"
TARGET = RUNTIME / "r4_runtime_target.c"
CLOCK = RUNTIME / "r4_clock64.c"
CONFIG = REPO / "firmware" / "cubemx" / "Core" / "Inc" / "FreeRTOSConfig.h"
ISR = REPO / "firmware" / "cubemx" / "Core" / "Src" / "stm32f4xx_it.c"
HOOKS = REPO / "firmware" / "cubemx" / "Core" / "Src" / "r0_freertos_smoke.c"
CMAKE = REPO / "firmware" / "cubemx" / "CMakeLists.txt"


def function_body(text: str, name: str) -> str:
    start = text.index(f"void {name}(void)")
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    raise AssertionError(f"unterminated function: {name}")


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

    def test_trace_macros_have_one_runtime_endpoint_per_semantic_event(self) -> None:
        text = CONFIG.read_text(encoding="utf-8")
        self.assertIn("#define traceISR_ENTER() R4_RuntimeTarget_TraceIsrEnter()", text)
        self.assertIn("#define traceISR_EXIT() R4_RuntimeTarget_TraceIsrExit()", text)
        self.assertIn("#define traceISR_EXIT_TO_SCHEDULER() R4_RuntimeTarget_TraceIsrExit()", text)
        self.assertEqual(text.count("R4_RuntimeTarget_TraceIsrEnter()"), 1)
        self.assertEqual(text.count("R4_RuntimeTarget_TraceIsrExit()"), 2)

    def test_tick_hook_does_not_emit_a_second_irq_pair(self) -> None:
        body = function_body(HOOKS.read_text(encoding="utf-8"),
                             "vApplicationTickHook")
        self.assertIn("R4_RuntimeTarget_Checkpoint", body)
        self.assertIn("R4_TickServiceTarget_OnTickHook", body)
        self.assertNotIn("traceISR_ENTER", body)
        self.assertNotIn("traceISR_EXIT", body)
        self.assertNotIn("portYIELD_FROM_ISR", body)

    def test_application_irq_handlers_have_one_entry_and_one_port_tail(self) -> None:
        text = ISR.read_text(encoding="utf-8")
        for handler in ("TIM7_IRQHandler", "TIM6_DAC_IRQHandler",
                        "DMA2_Stream0_IRQHandler"):
            body = function_body(text, handler)
            self.assertEqual(body.count("traceISR_ENTER()"), 1, handler)
            self.assertEqual(body.count("portYIELD_FROM_ISR("), 1, handler)
            self.assertNotIn("traceISR_EXIT()", body, handler)

    def test_reference_profile_compiles_out_every_accounting_hook(self) -> None:
        cmake = CMAKE.read_text(encoding="utf-8")
        config = CONFIG.read_text(encoding="utf-8")
        hooks = HOOKS.read_text(encoding="utf-8")
        self.assertIn("option(STREAM_LAB_R4_ACCOUNTING", cmake)
        self.assertIn("STREAM_LAB_R4_ACCOUNTING=${R4_ACCOUNTING_VALUE}", cmake)
        guard = "#if defined(STREAM_LAB_R4_RUNTIME) && (STREAM_LAB_R4_ACCOUNTING != 0)"
        self.assertIn(guard, config)
        self.assertIn(guard, hooks)
        self.assertIn("#define traceISR_ENTER() R4_RuntimeTarget_TraceIsrEnter()", config)

    def test_dma_perturbation_probe_is_separate_from_irq_accounting(self) -> None:
        target = TARGET.read_text(encoding="utf-8")
        body = function_body(ISR.read_text(encoding="utf-8"),
                             "DMA2_Stream0_IRQHandler")
        self.assertIn("void R4_RuntimeTarget_ObserveDmaServiceEnter(void)", target)
        self.assertIn("void R4_RuntimeTarget_ObserveDmaServiceExit(void)", target)
        self.assertEqual(body.count("R4_RuntimeTarget_ObserveDmaServiceEnter()"), 1)
        self.assertEqual(body.count("R4_RuntimeTarget_ObserveDmaServiceExit()"), 1)
        self.assertEqual(body.count("STREAM_LAB_R4_PERTURBATION_AB"), 2)

    def test_perturbation_response_uses_the_real_r3_publication_and_worker_path(self) -> None:
        runtime = (RUNTIME / "r3_w3_runtime.c").read_text(encoding="utf-8")
        probe = (RUNTIME / "r4_perturbation_target.h").read_text(encoding="utf-8")
        self.assertIn("R4_PerturbationTarget_OnReleaseFromIsr", runtime)
        self.assertIn("R4_PerturbationTarget_OnWorkerStart", runtime)
        self.assertIn("R4_PerturbationTarget_OnWorkerComplete", runtime)
        self.assertIn("STREAM_LAB_R4_PERTURBATION_AB", runtime)
        self.assertIn("R4_PERTURBATION_RESPONSE_SAMPLES 33U", probe)

    def test_perturbation_uses_a_declared_bounded_processing_workload(self) -> None:
        runtime = (RUNTIME / "r3_w3_runtime.c").read_text(encoding="utf-8")
        header = (RUNTIME / "r3_w3_runtime.h").read_text(encoding="utf-8")
        harness = (RUNTIME / "r4_hw_harness.c").read_text(encoding="utf-8")
        self.assertIn("uint32_t processing_work_iterations;", header)
        self.assertIn("runtime.config.processing_work_iterations", runtime)
        self.assertIn("R4_HW_PERTURBATION_WORK_ITERATIONS 5000U", harness)
        self.assertIn("config.processing_work_iterations = R4_HW_PERTURBATION_WORK_ITERATIONS", harness)


if __name__ == "__main__":
    unittest.main()
