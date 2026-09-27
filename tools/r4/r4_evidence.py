#!/usr/bin/env python3
"""Run, import, and verify sealed R4 target-evidence attempts.

An attempt is first created outside the repository from a clean, pushed
source commit.  The tool records every external command without retrying,
then only a sealed PASS attempt can be copied into ``docs/evidence/r4``.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
from statistics import median

PHASES = ("H0", "H1", "H2", "H3", "H4", "H5")
MAGIC, COMPLETE, SCHEMA = 0x52344857, 0x5234444E, 1
PREFIX_WORDS, READ_BYTES = 11, 2048
SYNTHETIC_CREATE_MASK_WORD = 122
SYNTHETIC_SCHEMA_WORD = 123
SYNTHETIC_DONE_MASK_WORD = 124
SYNTHETIC_A_ITERATIONS_WORD = 125
SYNTHETIC_B_ITERATIONS_WORD = 126
WINDOW_CYCLES_WORD = 20
SYNTHETIC_A_CYCLES_WORD = 128
SYNTHETIC_B_CYCLES_WORD = 130
SYNTHETIC_WINDOW_TASK_WORD = 132
SYNTHETIC_WINDOW_IRQ_WORD = 134
SYNTHETIC_WINDOW_IDLE_WORD = 136
SYNTHETIC_WINDOW_UNCLASSIFIED_WORD = 138
DMA_WINDOW_SNAPSHOT_WORD = 140
T17_ARM_STATUS_WORD = 40
T17_NESTED_ARM_STATUS_WORD = 41
T17_CHECKPOINT_STATUS_WORD = 42
T17_POST_CLOSE_STATUS_WORD = 43
T17_DUPLICATE_EXIT_STATUS_WORD = 44
T17_SERIAL_BEFORE_WORD = 46
T17_SERIAL_AFTER_PENDING_WORD = 48
T17_LOW_IRQ_BEFORE_WORD = 50
T17_HIGH_IRQ_BEFORE_WORD = 52
T17_LOW_IRQ_AFTER_WORD = 54
T17_HIGH_IRQ_AFTER_WORD = 56
T17_WINDOW_AT_CLOSE_WORD = 58
T17_WINDOW_AFTER_CLOSE_WORD = 60
T12_SOAK_CONFIGURED_MS_WORD = 88
T12_SOAK_START_HIGH_WORD = 89
T12_SOAK_END_HIGH_WORD = 90
T12_DMA_TAIL_STATUS_WORD = 91
T12_DMA_IRQ_COUNT_WORD = 96
T12_DMA_YIELD_COUNT_WORD = 98
T12_DMA_NO_YIELD_COUNT_WORD = 100
T12_NO_EVENT_STATUS_WORD = 102
T12_NO_EVENT_IRQ_BEFORE_WORD = 104
T12_NO_EVENT_IRQ_AFTER_WORD = 106
T12_NO_EVENT_NO_YIELD_BEFORE_WORD = 108
T12_NO_EVENT_NO_YIELD_AFTER_WORD = 110
T12_HEALTH_STATUS_WORD = 112
T12_HEALTH_FIRST_FAULT_WORD = 113
T12_HEALTH_FAIL_CLOSED_WORD = 114
T12_HEALTH_SERVICE_COUNT_WORD = 116
T12_HEALTH_MAX_INTERVAL_WORD = 118
T12_HEALTH_INTERVAL_LIMIT_WORD = 120
T15_REGISTER_STATUS_WORD = 62
T15_ARM_STATUS_WORD = 63
T15_SNAPSHOT_STATUS_WORD = 64
T15_START_CALLBACK_WORD = 65
T15_RELEASE_CALLBACK_WORD = 66
T15_SERVICE_SEQ_WORD = 68
T15_START_COUNT_WORD = 70
T15_RELEASE_COUNT_WORD = 72
T15_SKIPPED_COUNT_WORD = 74
T15_SEQUENCE_AFTER_SUSPENSION_WORD = 76
T15_TIMING_CONFIGURE_WORD = 78
T15_TIMING_SNAPSHOT_WORD = 79
T15_TIMING_SERVICE_COUNT_WORD = 80
T15_TIMING_MAX_INTERVAL_WORD = 82
T15_TIMING_PHASE_ERROR_WORD = 84
T15_TIMING_OVER_LIMIT_WORD = 86
T15_SYSTICK_SNAPSHOT_STATUS_WORD = 151
T15_SYSTICK_ENTER_WORD = 152
T15_SYSTICK_EXIT_WORD = 154
T15_PHASE_Q0_WORD = 156
T15_PHASE_TIM2_BEFORE_WORD = 158
T15_PHASE_TIM2_AFTER_WORD = 160
T15_PHASE_TIM2_CEN_WORD = 162
TICK_GAP_HEALTH_STATUS_WORD = 163
TICK_GAP_FIRST_FAULT_WORD = 164
TICK_GAP_FAIL_CLOSED_WORD = 165
TICK_GAP_OVER_LIMIT_WORD = 166
TICK_GAP_MAX_INTERVAL_WORD = 168
TICK_GAP_INTERVAL_LIMIT_WORD = 170
MICROBENCH_SAMPLE_COUNT_WORD = 172
MICROBENCH_TASK_MIN_WORD = 174
MICROBENCH_TASK_MEDIAN_WORD = 176
MICROBENCH_TASK_MAX_WORD = 178
MICROBENCH_IRQ_MIN_WORD = 180
MICROBENCH_IRQ_MEDIAN_WORD = 182
MICROBENCH_IRQ_MAX_WORD = 184
MICROBENCH_WINDOW_MIN_WORD = 186
MICROBENCH_WINDOW_MEDIAN_WORD = 188
MICROBENCH_WINDOW_MAX_WORD = 190
MICROBENCH_NESTED_MIN_WORD = 192
MICROBENCH_NESTED_MEDIAN_WORD = 194
MICROBENCH_NESTED_MAX_WORD = 196
MASK_NORMAL_STATUS_WORD = 198
MASK_NORMAL_PRIMASK_BEFORE_WORD = 199
MASK_NORMAL_PRIMASK_AFTER_WORD = 200
MASK_MASKED_STATUS_WORD = 201
MASK_MASKED_PRIMASK_BEFORE_WORD = 202
MASK_MASKED_PRIMASK_AFTER_WORD = 203
MASK_BASEPRI_BEFORE_WORD = 204
MASK_BASEPRI_AFTER_WORD = 205
TIME_REGRESSION_INJECT_STATUS_WORD = 206
TIME_REGRESSION_POST_STATUS_WORD = 207
TIME_REGRESSION_SERIAL_BEFORE_WORD = 208
TIME_REGRESSION_SERIAL_AFTER_WORD = 210
COMMIT_PENDING_ARM_STATUS_WORD = 212
COMMIT_PENDING_SNAPSHOT_STATUS_WORD = 213
COMMIT_PENDING_ARM_COUNT_WORD = 214
COMMIT_PENDING_IRQ_COUNT_WORD = 215
COMMIT_PENDING_ACTIVE_AT_IRQ_WORD = 216
TARGET_WINDOW_TASK_WORD = 218
TARGET_WINDOW_IRQ_WORD = 220
TARGET_WINDOW_IDLE_WORD = 222
TARGET_WINDOW_UNCLASSIFIED_WORD = 224
RESPONSE_CREATED_WORD = 226
RESPONSE_DONE_WORD = 227
RESPONSE_RELEASE_RAW_WORD = 228
RESPONSE_START_RAW_WORD = 229
RESPONSE_COMPLETE_RAW_WORD = 230
RESPONSE_WORK_RAW_WORD = 231
RESPONSE_OWNER_CYCLES_WORD = 232
MASK_TIMING_RESET_WORD = 234
MASK_TIMING_SAMPLE_COUNT_WORD = 235
MASK_TIMING_MAX_WORD = 236
MASK_TIMING_LIMIT_WORD = 238
DMA_SERVICE_COUNT_WORD = 240
DMA_SERVICE_MAX_WORD = 242
MICROBENCH_FAILURE_COUNT_WORD = 244
MICROBENCH_LAST_STATUS_WORD = 245
PERTURBATION_PROFILE_WORD = 246
PERTURBATION_ACCOUNTING_WORD = 247
PERTURBATION_DMA_COUNT_WORD = 248
PERTURBATION_DMA_MAX_WORD = 250
PERTURBATION_DRIVER_COMPLETIONS_WORD = 252
PERTURBATION_DRIVER_KEEP_WORD = 253
PERTURBATION_DRIVER_REBIND_WORD = 254
PERTURBATION_DRIVER_FAILURE_WORD = 255
PERTURBATION_PROCESSING_WAKE_WORD = 256
PERTURBATION_PROCESSING_COMPLETE_WORD = 257
PERTURBATION_PROCESSING_CANCEL_WORD = 258
PERTURBATION_RUNTIME_FAULT_WORD = 259
PERTURBATION_RESPONSE_RELEASE_COUNT_WORD = 260
PERTURBATION_RESPONSE_COMPLETE_COUNT_WORD = 261
PERTURBATION_RESPONSE_OVERFLOW_COUNT_WORD = 262
PERTURBATION_RESPONSE_SAMPLES_WORD = 263
PERTURBATION_RESPONSE_SAMPLE_COUNT = 33
CASES = {
    "t12-soak-a": ("T12_SOAK", 1, 65000, 75),
    "t12-soak-b": ("T12_SOAK", 1, 65000, 75),
    "t15-q0": ("T15_Q0", 2, 0, 8),
    "t15-release": ("T15_RELEASE", 3, 0, 8),
    "t17-atomic": ("T17_ATOMIC", 4, 0, 8),
    "t04-commit-budget-a": ("COMMIT_BUDGET", 5, 65000, 75),
    "t04-commit-budget-b": ("COMMIT_BUDGET", 5, 65000, 75),
    "task-synthetic": ("SYNTHETIC_TASKS", 6, 0, 8),
    "dma-window": ("DMA_WINDOW", 7, 0, 8),
    "tick-gap": ("TICK_GAP", 8, 0, 8),
    "microbenchmark": ("MICROBENCH", 9, 0, 8),
    "mask-restore": ("MASK_RESTORE", 10, 0, 8),
    "time-regression": ("TIME_REGRESSION", 11, 0, 8),
    # This composes the directed IRQ witness with the released full-lock
    # budget gate, which requires the representative completion population.
    "commit-pending-irq": ("COMMIT_PENDING_IRQ", 12, 65000, 75),
    "window-intersection": ("WINDOW_INTERSECTION", 13, 0, 8),
    "response-synthetic": ("RESPONSE_SYNTHETIC", 14, 0, 8),
    "mask-timing": ("MASK_TIMING", 15, 0, 8),
    "combined-service": ("COMBINED_SERVICE", 16, 65000, 75),
    "perturbation-ab": ("PERTURBATION_AB", 17, 65000, 75),
}
PROGRAMMER = Path(r"E:\DevTools\STM32CubeProgrammer-2.23.0\bin\STM32_Programmer_CLI.exe")
CMAKE = Path(r"E:\DevTools\STM32CubeCLT-1.22.0\CMake\bin\cmake.exe")
NINJA_DIR = Path(r"E:\DevTools\STM32CubeCLT-1.22.0\Ninja\bin")
ARM_DIR = Path(r"E:\DevTools\STM32CubeCLT-1.22.0\GNU-tools-for-STM32\bin")


class EvidenceError(RuntimeError):
    pass


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest().upper()


def write_new(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=True, indent=2, sort_keys=True,
                  allow_nan=False)
        stream.write("\n")


def manifest(root: Path) -> None:
    rows = []
    for path in sorted(root.rglob("*")):
        if path.is_file() and path.name != "MANIFEST.sha256":
            rows.append(f"{digest(path)}  {path.relative_to(root).as_posix()}\n")
    with (root / "MANIFEST.sha256").open("x", encoding="utf-8", newline="\n") as stream:
        stream.writelines(rows)


def validate_manifest(root: Path) -> None:
    manifest_file = root / "MANIFEST.sha256"
    if not manifest_file.is_file():
        raise EvidenceError("missing MANIFEST.sha256")
    expected: dict[str, str] = {}
    for row in manifest_file.read_text(encoding="utf-8").splitlines():
        matched = re.fullmatch(r"([0-9A-F]{64})  ([^\\/]+(?:/[^\\/]+)*)", row)
        if matched is None or matched.group(2) == "MANIFEST.sha256":
            raise EvidenceError("malformed manifest row")
        expected[matched.group(2)] = matched.group(1)
    actual = {path.relative_to(root).as_posix(): digest(path) for path in root.rglob("*")
              if path.is_file() and path.name != "MANIFEST.sha256"}
    if actual != expected:
        raise EvidenceError("manifest does not exactly cover the attempt")


class CommandLog:
    def __init__(self, root: Path) -> None:
        self.root, self.number = root, 0

    def run(self, argv: list[Path | str], name: str, *, env: dict[str, str] | None = None,
            timeout: int = 120) -> tuple[int, str, str]:
        self.number += 1
        command_root = self.root / "commands" / f"{self.number:03d}-{name}"
        command_root.mkdir(parents=True)
        values = [str(item) for item in argv]
        write_new(command_root / "request.json", {"argv": values, "cwd": None,
                  "timeout_seconds": timeout, "automatic_retry": False})
        completed = subprocess.run(values, capture_output=True, text=True,
                                   encoding="utf-8", errors="replace", env=env,
                                   timeout=timeout, check=False)
        (command_root / "stdout.txt").write_text(completed.stdout, encoding="utf-8", newline="\n")
        (command_root / "stderr.txt").write_text(completed.stderr, encoding="utf-8", newline="\n")
        write_new(command_root / "result.json", {"returncode": completed.returncode})
        if completed.returncode != 0:
            raise EvidenceError(f"command failed: {name} (exit {completed.returncode})")
        return completed.returncode, completed.stdout, completed.stderr


def git(repo: Path, *args: str) -> str:
    completed = subprocess.run(["git", "-C", str(repo), *args], capture_output=True,
                               text=True, encoding="utf-8", errors="replace", check=False)
    if completed.returncode:
        raise EvidenceError(f"git {' '.join(args)} failed: {completed.stderr.strip()}")
    return completed.stdout.strip()


def definitions(selector: str, soak_ms: int, accounting: str | None = None) -> dict[str, str]:
    result = {
        "STREAM_LAB_FOUNDATION_ADC_DBM_DRIVER": "ON",
        "STREAM_LAB_FOUNDATION_OWNERSHIP_CORE": "ON",
        "STREAM_LAB_FOUNDATION_TOKEN_LEDGER": "ON",
        "STREAM_LAB_FOUNDATION_QUEUE_ADAPTER": "ON",
        "STREAM_LAB_R3_WORKER_CONTRACT": "ON",
        "STREAM_LAB_R3_WORKER_TASKS": "ON",
        "STREAM_LAB_R3_LIFECYCLE": "ON",
        "STREAM_LAB_R4_RUNTIME": "ON",
        "STREAM_LAB_R4_HW": "ON",
        "STREAM_LAB_R4_HW_CASE": selector,
        "STREAM_LAB_R4_HW_SOAK_MS": str(soak_ms),
        # R4's 1800-cycle gate is a released target benchmark, not a
        # debug-symbol profile.  The CMake R4 profile additionally applies
        # LTO and explicit O3 to the measured queue/accounting path.
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON",
    }
    if accounting is not None:
        if accounting not in ("MINIMAL", "R4"):
            raise EvidenceError("unknown accounting profile")
        result["STREAM_LAB_R4_ACCOUNTING"] = "OFF" if accounting == "MINIMAL" else "ON"
    return result


def parse_words(text: str) -> list[int]:
    words: list[int] = []
    for line in text.splitlines():
        if not re.match(r"^0x[0-9A-Fa-f]{8}\s*:", line):
            continue
        for token in line.split(":", 1)[1].split():
            if not re.fullmatch(r"[0-9A-Fa-f]{8}", token):
                raise EvidenceError(f"malformed target word: {token!r}")
            words.append(int(token, 16))
    if len(words) < PREFIX_WORDS:
        raise EvidenceError(f"target read is too short: {len(words)} words")
    return words


def evaluate(case: str, words: list[int], accounting: str | None = None) -> dict:
    selector, case_id, _, _ = CASES[case]
    checks = {
        "result_magic": words[0] == MAGIC,
        "case_identity": words[1] == case_id,
        "schema_version": words[2] == SCHEMA,
        "terminal_pass": words[3] == 1,
        "no_invariant_failure": words[4] == 0,
        "init_status": words[5] == 0,
        "completed_magic": words[10] == COMPLETE,
    }
    if case == "task-synthetic":
        def word64(index: int) -> int:
            return words[index] | (words[index + 1] << 32)

        if len(words) <= SYNTHETIC_WINDOW_UNCLASSIFIED_WORD + 1:
            checks["synthetic_extension_present"] = False
        else:
            task_a = word64(SYNTHETIC_A_CYCLES_WORD)
            task_b = word64(SYNTHETIC_B_CYCLES_WORD)
            window_task = word64(SYNTHETIC_WINDOW_TASK_WORD)
            window_irq = word64(SYNTHETIC_WINDOW_IRQ_WORD)
            window_idle = word64(SYNTHETIC_WINDOW_IDLE_WORD)
            window_unclassified = word64(SYNTHETIC_WINDOW_UNCLASSIFIED_WORD)
            checks.update({
                "synthetic_schema": words[SYNTHETIC_SCHEMA_WORD] == 1,
                "synthetic_created": words[SYNTHETIC_CREATE_MASK_WORD] == 3,
                "synthetic_done": words[SYNTHETIC_DONE_MASK_WORD] == 3,
                "synthetic_iterations": (
                    words[SYNTHETIC_A_ITERATIONS_WORD] == 50000 and
                    words[SYNTHETIC_B_ITERATIONS_WORD] == 10000),
                "synthetic_owner_ratio": task_a > (task_b * 2),
                "synthetic_idle": window_idle > 0,
                "synthetic_conservation": (
                    word64(WINDOW_CYCLES_WORD) == window_task + window_irq + window_idle +
                    window_unclassified),
            })
    if case == "dma-window":
        if len(words) <= DMA_WINDOW_SNAPSHOT_WORD + 10:
            checks["dma_window_extension_present"] = False
        else:
            snapshot = DMA_WINDOW_SNAPSHOT_WORD
            checks.update({
                "dma_window_snapshot": words[snapshot] == 0,
                "dma_window_configured": words[snapshot + 1] == 1,
                "dma_window_opened": words[snapshot + 2] == 1,
                "dma_window_closed": words[snapshot + 3] == 1,
                "dma_window_sequences": (
                    words[snapshot + 4] == 1 and words[snapshot + 5] == 4 and
                    words[snapshot + 6] >= 5 and words[snapshot + 7] == 0),
                "dma_window_statuses": (
                    words[snapshot + 8] == 0 and words[snapshot + 9] == 0 and
                    words[snapshot + 10] == 0),
            })
    if case == "t17-atomic":
        def word64(index: int) -> int:
            return words[index] | (words[index + 1] << 32)

        if len(words) <= T17_WINDOW_AFTER_CLOSE_WORD + 1:
            checks["t17_extension_present"] = False
        else:
            checks.update({
                "t17_atomic_statuses": (
                    words[T17_ARM_STATUS_WORD] == 0 and
                    words[T17_CHECKPOINT_STATUS_WORD] == 0 and
                    words[T17_POST_CLOSE_STATUS_WORD] == 0),
                "t17_pending_irq_ordered": (
                    word64(T17_SERIAL_AFTER_PENDING_WORD) >
                    word64(T17_SERIAL_BEFORE_WORD)),
                "t17_duplicate_exit_rejected": (
                    words[T17_DUPLICATE_EXIT_STATUS_WORD] == 7),
                "t17_real_nested_irq": (
                    words[T17_NESTED_ARM_STATUS_WORD] == 0 and
                    word64(T17_LOW_IRQ_AFTER_WORD) >
                    word64(T17_LOW_IRQ_BEFORE_WORD) and
                    word64(T17_HIGH_IRQ_AFTER_WORD) >
                    word64(T17_HIGH_IRQ_BEFORE_WORD)),
                "t17_sealed_window": (
                    word64(T17_WINDOW_AT_CLOSE_WORD) ==
                    word64(T17_WINDOW_AFTER_CLOSE_WORD)),
            })
    if case in ("t12-soak-a", "t12-soak-b"):
        def word64(index: int) -> int:
            return words[index] | (words[index + 1] << 32)

        if len(words) <= T12_HEALTH_INTERVAL_LIMIT_WORD + 1:
            checks["t12_extension_present"] = False
        else:
            checks.update({
                "t12_lifecycle": (
                    words[25] == 0 and words[26] == 0 and words[27] == 0),
                "t12_wrap_and_duration": (
                    words[T12_SOAK_CONFIGURED_MS_WORD] >= 60000 and
                    words[T12_SOAK_END_HIGH_WORD] >
                    words[T12_SOAK_START_HIGH_WORD]),
                "t12_dma_yield_path": (
                    words[T12_DMA_TAIL_STATUS_WORD] == 0 and
                    word64(T12_DMA_IRQ_COUNT_WORD) > 0 and
                    word64(T12_DMA_YIELD_COUNT_WORD) > 0),
                "t12_dma_no_event_path": (
                    words[T12_NO_EVENT_STATUS_WORD] == 0 and
                    word64(T12_NO_EVENT_IRQ_AFTER_WORD) ==
                    word64(T12_NO_EVENT_IRQ_BEFORE_WORD) + 1 and
                    word64(T12_NO_EVENT_NO_YIELD_AFTER_WORD) ==
                    word64(T12_NO_EVENT_NO_YIELD_BEFORE_WORD) + 1),
                "t12_health_service": (
                    words[T12_HEALTH_STATUS_WORD] == 0 and
                    words[T12_HEALTH_FIRST_FAULT_WORD] == 0 and
                    words[T12_HEALTH_FAIL_CLOSED_WORD] == 0 and
                    word64(T12_HEALTH_SERVICE_COUNT_WORD) >= 60 and
                    word64(T12_HEALTH_MAX_INTERVAL_WORD) <=
                    word64(T12_HEALTH_INTERVAL_LIMIT_WORD)),
            })
    if case in ("t15-q0", "t15-release"):
        def word64(index: int) -> int:
            return words[index] | (words[index + 1] << 32)

        if len(words) <= T15_PHASE_TIM2_CEN_WORD:
            checks["t15_extension_present"] = False
        else:
            expected_skips = 0 if case == "t15-q0" else 1
            checks.update({
                "t15_service_registration": (
                    words[T15_REGISTER_STATUS_WORD] == 0 and
                    words[T15_ARM_STATUS_WORD] == 0 and
                    words[T15_SNAPSHOT_STATUS_WORD] == 0),
                "t15_exact_callbacks": (
                    words[T15_START_CALLBACK_WORD] == 1 and
                    words[T15_RELEASE_CALLBACK_WORD] == 1 and
                    word64(T15_START_COUNT_WORD) == 1 and
                    word64(T15_RELEASE_COUNT_WORD) == 1 and
                    word64(T15_SKIPPED_COUNT_WORD) == expected_skips),
                "t15_tick_history": (
                    word64(T15_SERVICE_SEQ_WORD) > 0 and
                    word64(T15_SEQUENCE_AFTER_SUSPENSION_WORD) > 0 and
                    words[T15_TIMING_CONFIGURE_WORD] == 0 and
                    words[T15_TIMING_SNAPSHOT_WORD] == 0 and
                    word64(T15_TIMING_SERVICE_COUNT_WORD) > 0 and
                    word64(T15_TIMING_MAX_INTERVAL_WORD) > 0 and
                    word64(T15_TIMING_OVER_LIMIT_WORD) == 0),
                "t15_systick_single_pair": (
                    words[T15_SYSTICK_SNAPSHOT_STATUS_WORD] == 0 and
                    word64(T15_SYSTICK_ENTER_WORD) > 0 and
                    word64(T15_SYSTICK_ENTER_WORD) ==
                    word64(T15_SYSTICK_EXIT_WORD) and
                    word64(T15_SYSTICK_ENTER_WORD) ==
                    word64(T15_TIMING_SERVICE_COUNT_WORD)),
                "t15_q0_to_tim2_phase": (
                    word64(T15_PHASE_Q0_WORD) > 0 and
                    words[T15_PHASE_TIM2_CEN_WORD] == 1 and
                    word64(T15_PHASE_TIM2_AFTER_WORD) >=
                    word64(T15_PHASE_TIM2_BEFORE_WORD) and
                    (word64(T15_PHASE_TIM2_AFTER_WORD) -
                     word64(T15_PHASE_TIM2_BEFORE_WORD)) <= 1800),
            })
    if case == "tick-gap":
        def word64(index: int) -> int:
            return words[index] | (words[index + 1] << 32)

        if len(words) <= TICK_GAP_INTERVAL_LIMIT_WORD + 1:
            checks["tick_gap_extension_present"] = False
        else:
            checks["tick_gap_fail_closed"] = (
                words[TICK_GAP_HEALTH_STATUS_WORD] == 0 and
                words[TICK_GAP_FIRST_FAULT_WORD] == 1 and
                words[TICK_GAP_FAIL_CLOSED_WORD] == 1 and
                word64(TICK_GAP_OVER_LIMIT_WORD) >= 1 and
                word64(TICK_GAP_MAX_INTERVAL_WORD) >
                word64(TICK_GAP_INTERVAL_LIMIT_WORD))
    if case == "microbenchmark":
        def word64(index: int) -> int:
            return words[index] | (words[index + 1] << 32)

        if len(words) <= MICROBENCH_LAST_STATUS_WORD:
            checks["microbenchmark_extension_present"] = False
        else:
            paths = (
                (MICROBENCH_TASK_MIN_WORD, MICROBENCH_TASK_MEDIAN_WORD,
                 MICROBENCH_TASK_MAX_WORD),
                (MICROBENCH_IRQ_MIN_WORD, MICROBENCH_IRQ_MEDIAN_WORD,
                 MICROBENCH_IRQ_MAX_WORD),
                (MICROBENCH_WINDOW_MIN_WORD, MICROBENCH_WINDOW_MEDIAN_WORD,
                 MICROBENCH_WINDOW_MAX_WORD),
                (MICROBENCH_NESTED_MIN_WORD, MICROBENCH_NESTED_MEDIAN_WORD,
                 MICROBENCH_NESTED_MAX_WORD),
            )
            checks["runtime_event_microbenchmark"] = (
                words[MICROBENCH_SAMPLE_COUNT_WORD] >= 17 and
                words[MICROBENCH_FAILURE_COUNT_WORD] == 0 and
                words[MICROBENCH_LAST_STATUS_WORD] == 0 and
                all(0 < word64(low) <= word64(mid) <= word64(high)
                    for low, mid, high in paths))
    if case == "mask-restore":
        if len(words) <= MASK_BASEPRI_AFTER_WORD:
            checks["mask_restore_extension_present"] = False
        else:
            checks["runtime_event_mask_restore"] = (
                words[MASK_NORMAL_STATUS_WORD] == 0 and
                words[MASK_NORMAL_PRIMASK_BEFORE_WORD] == 0 and
                words[MASK_NORMAL_PRIMASK_AFTER_WORD] == 0 and
                words[MASK_MASKED_STATUS_WORD] == 0 and
                words[MASK_MASKED_PRIMASK_BEFORE_WORD] == 1 and
                words[MASK_MASKED_PRIMASK_AFTER_WORD] == 1 and
                words[MASK_BASEPRI_BEFORE_WORD] == words[MASK_BASEPRI_AFTER_WORD])
    if case == "time-regression":
        def word64(index: int) -> int:
            return words[index] | (words[index + 1] << 32)

        if len(words) <= TIME_REGRESSION_SERIAL_AFTER_WORD + 1:
            checks["time_regression_extension_present"] = False
        else:
            checks["time_regression_fail_closed"] = (
                words[TIME_REGRESSION_INJECT_STATUS_WORD] == 5 and
                words[TIME_REGRESSION_POST_STATUS_WORD] == 5 and
                word64(TIME_REGRESSION_SERIAL_AFTER_WORD) ==
                word64(TIME_REGRESSION_SERIAL_BEFORE_WORD))
    if case == "commit-pending-irq":
        if len(words) <= COMMIT_PENDING_ACTIVE_AT_IRQ_WORD:
            checks["commit_pending_irq_extension_present"] = False
        else:
            checks["commit_pending_irq_after_unlock"] = (
                words[COMMIT_PENDING_ARM_STATUS_WORD] == 0 and
                words[COMMIT_PENDING_SNAPSHOT_STATUS_WORD] == 0 and
                words[COMMIT_PENDING_ARM_COUNT_WORD] == 1 and
                words[COMMIT_PENDING_IRQ_COUNT_WORD] == 1 and
                words[COMMIT_PENDING_ACTIVE_AT_IRQ_WORD] == 0)
    if case == "window-intersection":
        def word64(index: int) -> int:
            return words[index] | (words[index + 1] << 32)

        if len(words) <= TARGET_WINDOW_UNCLASSIFIED_WORD + 1:
            checks["target_window_extension_present"] = False
        else:
            checks["target_window_conservation"] = (
                word64(WINDOW_CYCLES_WORD) > 0 and
                word64(WINDOW_CYCLES_WORD) ==
                word64(TARGET_WINDOW_TASK_WORD) + word64(TARGET_WINDOW_IRQ_WORD) +
                word64(TARGET_WINDOW_IDLE_WORD) +
                word64(TARGET_WINDOW_UNCLASSIFIED_WORD))
            checks["target_window_sealed_after_real_activity"] = (
                word64(T17_WINDOW_AT_CLOSE_WORD) > 0 and
                word64(T17_WINDOW_AFTER_CLOSE_WORD) ==
                word64(T17_WINDOW_AT_CLOSE_WORD))
    if case == "response-synthetic":
        def word64(index: int) -> int:
            return words[index] | (words[index + 1] << 32)

        if len(words) <= RESPONSE_OWNER_CYCLES_WORD + 1:
            checks["response_extension_present"] = False
        else:
            response = (words[RESPONSE_COMPLETE_RAW_WORD] -
                        words[RESPONSE_RELEASE_RAW_WORD]) & 0xFFFFFFFF
            work = words[RESPONSE_WORK_RAW_WORD]
            checks["response_direct_wall_endpoints"] = (
                words[RESPONSE_CREATED_WORD] == 1 and
                words[RESPONSE_DONE_WORD] == 1 and
                words[RESPONSE_START_RAW_WORD] != 0 and
                words[RESPONSE_COMPLETE_RAW_WORD] != 0 and
                0 < work <= response <= 5000000)
            # Direct DWT endpoints and RuntimeEvent samples are distinct
            # boundary observations.  Require >=99.0% sealed-owner coverage,
            # preserving a bounded C-level/transaction-boundary blind zone.
            checks["response_owner_known_work_coverage"] = (
                word64(RESPONSE_OWNER_CYCLES_WORD) * 1000 >= work * 990)
    if case == "mask-timing":
        def word64(index: int) -> int:
            return words[index] | (words[index + 1] << 32)

        if len(words) <= MASK_TIMING_LIMIT_WORD + 1:
            checks["mask_timing_extension_present"] = False
        else:
            checks["runtime_event_masked_span_bound"] = (
                words[MASK_TIMING_RESET_WORD] == 0 and
                words[MASK_TIMING_SAMPLE_COUNT_WORD] >= 33 and
                0 < word64(MASK_TIMING_MAX_WORD) <=
                word64(MASK_TIMING_LIMIT_WORD) == 1800)
    if case == "combined-service":
        def word64(index: int) -> int:
            return words[index] | (words[index + 1] << 32)

        if len(words) <= DMA_SERVICE_MAX_WORD + 1:
            checks["combined_service_extension_present"] = False
        else:
            checks["combined_dma_service_margin"] = (
                words[25] == 0 and words[26] == 0 and words[27] == 0 and
                words[T12_SOAK_CONFIGURED_MS_WORD] >= 60000 and
                word64(DMA_SERVICE_COUNT_WORD) >= 100 and
                word64(DMA_SERVICE_COUNT_WORD) == word64(T12_DMA_IRQ_COUNT_WORD) and
                0 < word64(DMA_SERVICE_MAX_WORD) <= 23040)
    if case == "perturbation-ab":
        def word64(index: int) -> int:
            return words[index] | (words[index + 1] << 32)

        if len(words) < PERTURBATION_RESPONSE_SAMPLES_WORD + (3 * PERTURBATION_RESPONSE_SAMPLE_COUNT):
            checks["perturbation_extension_present"] = False
        else:
            expected_profile = 0 if accounting == "MINIMAL" else 1
            response_samples = [
                tuple(words[PERTURBATION_RESPONSE_SAMPLES_WORD + (3 * index):
                            PERTURBATION_RESPONSE_SAMPLES_WORD + (3 * index) + 3])
                for index in range(PERTURBATION_RESPONSE_SAMPLE_COUNT)
            ]
            checks.update({
                "perturbation_profile_identity": (
                    accounting in ("MINIMAL", "R4") and
                    words[PERTURBATION_PROFILE_WORD] == expected_profile and
                    words[PERTURBATION_ACCOUNTING_WORD] == expected_profile),
                "perturbation_dma_margin": (
                    word64(PERTURBATION_DMA_COUNT_WORD) >= 50000 and
                    0 < word64(PERTURBATION_DMA_MAX_WORD) <= 23040),
                "perturbation_lifecycle_data_plane": (
                    words[25] == 0 and words[26] == 0 and words[27] == 0 and
                    words[PERTURBATION_DRIVER_COMPLETIONS_WORD] > 0 and
                    words[PERTURBATION_DRIVER_REBIND_WORD] > 0 and
                    words[PERTURBATION_DRIVER_FAILURE_WORD] == 0 and
                    words[PERTURBATION_RUNTIME_FAULT_WORD] == 0 and
                    words[PERTURBATION_PROCESSING_WAKE_WORD] > 0 and
                    words[PERTURBATION_PROCESSING_COMPLETE_WORD] > 0),
                "perturbation_fixed_real_worker_response_population": (
                    words[PERTURBATION_RESPONSE_RELEASE_COUNT_WORD] ==
                    PERTURBATION_RESPONSE_SAMPLE_COUNT and
                    words[PERTURBATION_RESPONSE_COMPLETE_COUNT_WORD] ==
                    PERTURBATION_RESPONSE_SAMPLE_COUNT and
                    words[PERTURBATION_RESPONSE_OVERFLOW_COUNT_WORD] == 0 and
                    all(release != 0 and start != 0 and complete != 0 and
                        0 < ((start - release) & 0xFFFFFFFF) <= 5000000 and
                        0 < ((complete - start) & 0xFFFFFFFF) <= 5000000
                        for release, start, complete in response_samples)),
                "perturbation_cpu_profile_semantics": (
                    (accounting == "R4" and word64(WINDOW_CYCLES_WORD) > 0 and
                     word64(WINDOW_CYCLES_WORD) ==
                     word64(TARGET_WINDOW_TASK_WORD) + word64(TARGET_WINDOW_IRQ_WORD) +
                     word64(TARGET_WINDOW_IDLE_WORD) + word64(TARGET_WINDOW_UNCLASSIFIED_WORD)) or
                    (accounting == "MINIMAL" and
                     all(word64(index) == 0xFFFFFFFFFFFFFFFF for index in (
                         TARGET_WINDOW_TASK_WORD, TARGET_WINDOW_IRQ_WORD,
                         TARGET_WINDOW_IDLE_WORD, TARGET_WINDOW_UNCLASSIFIED_WORD))))
            })
    return {"result": "PASS" if all(checks.values()) else "FAIL", "checks": checks,
            "selector": selector, "prefix_words": [f"0x{word:08X}" for word in words[:PREFIX_WORDS]]}


def result_address(elf: Path, log: CommandLog, env: dict[str, str]) -> int:
    nm = ARM_DIR / "arm-none-eabi-nm.exe"
    _, listing, _ = log.run([nm, "-n", elf], "result-symbol", env=env, timeout=30)
    rows = re.findall(r"^\s*([0-9A-Fa-f]+)\s+[A-Za-z]\s+g_r4_hw_result\s*$", listing, re.M)
    if len(rows) != 1:
        raise EvidenceError("expected exactly one g_r4_hw_result symbol")
    return int(rows[0], 16)


def assert_tools() -> None:
    for path in (PROGRAMMER, CMAKE, ARM_DIR / "arm-none-eabi-gcc.exe",
                 ARM_DIR / "arm-none-eabi-nm.exe", ARM_DIR / "arm-none-eabi-objcopy.exe",
                 NINJA_DIR / "ninja.exe"):
        if not path.is_file():
            raise EvidenceError(f"required tool unavailable: {path}")


def run_attempt(args: argparse.Namespace) -> int:
    if args.case not in CASES:
        raise EvidenceError(f"unknown formal R4 case: {args.case}")
    repo = args.repo.resolve()
    if git(repo, "status", "--porcelain"):
        raise EvidenceError("formal attempt requires a clean worktree")
    head = git(repo, "rev-parse", "HEAD")
    if head != git(repo, "rev-parse", "origin/main"):
        raise EvidenceError("formal attempt requires HEAD pushed to origin/main")
    parent = args.output_parent.resolve()
    if parent.is_relative_to(repo):
        raise EvidenceError("attempt output must be outside the repository")
    assert_tools()
    parent.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix=f"r4-{args.case}-", dir=parent))
    log, completed = CommandLog(root), []
    selector, case_id, soak_ms, wait_seconds = CASES[args.case]
    accounting = args.accounting if args.case == "perturbation-ab" else None
    if args.case == "perturbation-ab" and accounting is None:
        raise EvidenceError("perturbation-ab requires --accounting MINIMAL or R4")
    env = {**os.environ, "PATH": str(ARM_DIR) + os.pathsep + str(NINJA_DIR) + os.pathsep + os.environ.get("PATH", "")}
    try:
        _, probe, _ = log.run([PROGRAMMER, "-l"], "probe-list", timeout=30)
        write_new(root / "H0.json", {"phase": "H0", "result": "PASS", "clean_tree": True,
                  "git_head": head, "origin_main": head, "case": args.case,
                  "probe_list_sha256": hashlib.sha256(probe.encode()).hexdigest().upper()})
        completed.append("H0")
        build = root / "build"
        toolchain = repo / "firmware" / "cubemx" / "cmake" / "gcc-arm-none-eabi.cmake"
        command = [CMAKE, "-S", repo / "firmware" / "cubemx", "-B", build, "-G", "Ninja",
                   f"-DCMAKE_TOOLCHAIN_FILE={toolchain}"] + [f"-D{k}={v}" for k, v in definitions(selector, soak_ms, accounting).items()]
        log.run(command, "configure", env=env, timeout=180)
        log.run([CMAKE, "--build", build, "--parallel", "4"], "build", env=env, timeout=600)
        elf, binary = build / "cubemx.elf", build / "programmed.bin"
        if not elf.is_file():
            raise EvidenceError("target ELF missing")
        log.run([ARM_DIR / "arm-none-eabi-objcopy.exe", "-O", "binary", elf, binary], "programmed-binary", env=env, timeout=30)
        write_new(root / "H1.json", {"phase": "H1", "selector": selector, "case_id": case_id,
                  "definitions": definitions(selector, soak_ms, accounting), "elf_sha256": digest(elf),
                  "programmed_image_sha256": digest(binary)})
        completed.append("H1")
        log.run([PROGRAMMER, "-c", "port=SWD", "mode=UR", "-w", elf, "-v", "-rst"], "flash-verify", timeout=120)
        write_new(root / "H2.json", {"phase": "H2", "verified": True, "programmed_image_sha256": digest(binary)})
        completed.append("H2")
        time.sleep(wait_seconds)
        write_new(root / "H3.json", {"phase": "H3", "single_execution": True, "wait_seconds": wait_seconds,
                  "configured_soak_ms": soak_ms, "result": "COMPLETE"})
        completed.append("H3")
        address = result_address(elf, log, env)
        _, readout, _ = log.run([PROGRAMMER, "-c", "port=SWD", "mode=UR", "-r32", f"0x{address:08X}", str(READ_BYTES)], "read-result", timeout=60)
        (root / "raw").mkdir(exist_ok=True)
        (root / "raw" / "target-result.txt").write_text(readout, encoding="utf-8", newline="\n")
        checked = evaluate(args.case, parse_words(readout), accounting)
        write_new(root / "H4.json", {"phase": "H4", "result_address": f"0x{address:08X}", **checked})
        completed.append("H4")
        write_new(root / "identity.json", {"schema_version": "r4-evidence-v2", "git_head": head,
                  "firmware_source_commit": head, "board_profile": "NUCLEO-F446RE/STM32F446xx",
                  "programmer": str(PROGRAMMER), "harness_sha256": digest(repo / "tools" / "r4" / "r4_evidence.py"),
                  "elf_sha256": digest(elf), "programmed_image_sha256": digest(binary)})
        write_new(root / "config.json", {"work_package": "R4", "case_id": args.case, "selector": selector,
                  "case_numeric_id": case_id, "configured_soak_ms": soak_ms,
                  "accounting_profile": accounting})
        write_new(root / "state.json", {"work_package": "R4", "case_id": args.case, "completed_phases": list(PHASES),
                  "next_allowed": "COMPLETE", "hardware_state": "SAFE"})
        write_new(root / "acceptance.json", {"result": checked["result"], "first_failure_class": None if checked["result"] == "PASS" else "TARGET_FIRMWARE", "invariants": checked["checks"]})
        write_new(root / "H5.json", {"phase": "H5", "result": checked["result"], "sealed": True})
        manifest(root)
        print(f"ATTEMPT: {root}\nRESULT: {checked['result']}")
        return 0 if checked["result"] == "PASS" else 2
    except Exception as error:
        write_new(root / "failure.json", {"error": str(error), "completed_phases": completed})
        manifest(root)
        print(f"ATTEMPT: {root}\nRESULT: FAIL: {error}")
        return 2


def verify_root(root: Path, *, require_pass: bool = True) -> None:
    required = [root / name for name in ("identity.json", "config.json", "state.json", "acceptance.json", "MANIFEST.sha256", *[f"{p}.json" for p in PHASES])]
    if any(not path.is_file() for path in required):
        raise EvidenceError("attempt is missing a required formal record")
    config = json.loads((root / "config.json").read_text(encoding="utf-8"))
    state = json.loads((root / "state.json").read_text(encoding="utf-8"))
    acceptance = json.loads((root / "acceptance.json").read_text(encoding="utf-8"))
    if config.get("case_id") not in CASES or state.get("completed_phases") != list(PHASES) or state.get("next_allowed") != "COMPLETE":
        raise EvidenceError("attempt state/configuration is not terminal")
    if acceptance.get("result") not in ("PASS", "FAIL"):
        raise EvidenceError("attempt acceptance has no terminal verdict")
    if require_pass and acceptance.get("result") != "PASS":
        raise EvidenceError("attempt acceptance is not PASS")
    validate_manifest(root)


def import_attempt(args: argparse.Namespace) -> int:
    repo, source = args.repo.resolve(), args.attempt.resolve()
    dirty = git(repo, "status", "--porcelain").splitlines()
    if any(not row[3:].replace("\\", "/").startswith("docs/evidence/r4/perturbation-ab/")
           for row in dirty):
        raise EvidenceError("import requires a clean worktree outside perturbation evidence")
    verify_root(source, require_pass=False)
    case = json.loads((source / "config.json").read_text(encoding="utf-8"))["case_id"]
    parent = repo / "docs" / "evidence" / "r4" / case
    number = 1
    while (parent / f"attempt-{number:04d}").exists():
        number += 1
    destination = parent / f"attempt-{number:04d}"
    shutil.copytree(source, destination)
    verify_root(destination, require_pass=False)
    print(destination)
    return 0


def word64(words: list[int], index: int) -> int:
    return words[index] | (words[index + 1] << 32)


def perturbation_metrics(root: Path, profile: str) -> tuple[dict, dict, dict]:
    """Load one sealed individual profile and derive only raw-record metrics."""
    verify_root(root)
    config = json.loads((root / "config.json").read_text(encoding="utf-8"))
    identity = json.loads((root / "identity.json").read_text(encoding="utf-8"))
    h1 = json.loads((root / "H1.json").read_text(encoding="utf-8"))
    if (config.get("case_id") != "perturbation-ab" or
            config.get("accounting_profile") != profile or
            config.get("configured_soak_ms") != 65000 or
            h1.get("selector") != "PERTURBATION_AB" or h1.get("case_id") != 17):
        raise EvidenceError("attempt is not the required perturbation profile")
    words = parse_words((root / "raw" / "target-result.txt").read_text(encoding="utf-8"))
    verdict = evaluate("perturbation-ab", words, profile)
    if verdict["result"] != "PASS":
        raise EvidenceError("raw perturbation record does not meet individual gates")
    samples = [
        tuple(words[PERTURBATION_RESPONSE_SAMPLES_WORD + (3 * index):
                    PERTURBATION_RESPONSE_SAMPLES_WORD + (3 * index) + 3])
        for index in range(PERTURBATION_RESPONSE_SAMPLE_COUNT)
    ]
    response = sorted(((complete - release) & 0xFFFFFFFF
                       for release, _start, complete in samples))
    completions = words[PERTURBATION_DRIVER_COMPLETIONS_WORD]
    rebind = words[PERTURBATION_DRIVER_REBIND_WORD]
    return ({
        "profile": profile,
        "attempt": str(root),
        "dma_count": word64(words, PERTURBATION_DMA_COUNT_WORD),
        "dma_max_cycles": word64(words, PERTURBATION_DMA_MAX_WORD),
        "completion_count": completions,
        "rebind_count": rebind,
        "keep_count": words[PERTURBATION_DRIVER_KEEP_WORD],
        "processing_complete_count": words[PERTURBATION_PROCESSING_COMPLETE_WORD],
        "response_sample_count": len(response),
        "response_median_cycles": int(median(response)),
        "response_max_cycles": response[-1],
    }, identity, h1)


def verify_perturbation_pair(minimal_root: Path, r4_root: Path) -> dict:
    minimal, minimal_identity, minimal_h1 = perturbation_metrics(minimal_root, "MINIMAL")
    r4, r4_identity, r4_h1 = perturbation_metrics(r4_root, "R4")
    common_minimal = dict(minimal_h1["definitions"])
    common_r4 = dict(r4_h1["definitions"])
    common_minimal.pop("STREAM_LAB_R4_ACCOUNTING", None)
    common_r4.pop("STREAM_LAB_R4_ACCOUNTING", None)
    identity_match = (
        minimal_identity.get("firmware_source_commit") == r4_identity.get("firmware_source_commit") and
        minimal_identity.get("harness_sha256") == r4_identity.get("harness_sha256") and
        minimal_identity.get("board_profile") == r4_identity.get("board_profile") and
        common_minimal == common_r4)
    have_denominators = (minimal["dma_count"] > 0 and
                         minimal["completion_count"] > 0 and
                         minimal["rebind_count"] > 0 and r4["rebind_count"] > 0 and
                         minimal["response_median_cycles"] > 0 and
                         minimal["response_max_cycles"] > 0)
    dma_rate_delta = abs(r4["dma_count"] - minimal["dma_count"]) / max(minimal["dma_count"], 1)
    completion_delta = abs(r4["completion_count"] - minimal["completion_count"])
    completion_rate_delta = completion_delta / max(minimal["completion_count"], 1)
    admission_minimal = minimal["rebind_count"] / max(minimal["completion_count"], 1)
    admission_r4 = r4["rebind_count"] / max(r4["completion_count"], 1)
    checks = {
        "matched_immutable_identity": identity_match,
        "nonzero_common_denominators": have_denominators,
        "dma_max_relative_delta": (
            minimal["dma_max_cycles"] <= 23040 and r4["dma_max_cycles"] <= 23040 and
            (r4["dma_max_cycles"] - minimal["dma_max_cycles"]) / minimal["dma_max_cycles"] <= 0.25),
        "dma_observation_rate_delta": dma_rate_delta <= 0.10,
        "completion_throughput_delta": (
            completion_rate_delta <= 0.05 or completion_delta <= 1),
        "admission_delta": abs(admission_r4 - admission_minimal) <= 0.05,
        "processing_completion_coverage": (
            minimal["processing_complete_count"] / max(minimal["rebind_count"], 1) >= 0.99 and
            r4["processing_complete_count"] / max(r4["rebind_count"], 1) >= 0.99),
        "response_median_delta": (
            r4["response_median_cycles"] / max(minimal["response_median_cycles"], 1) <= 1.25),
        "response_max_delta": r4["response_max_cycles"] / max(minimal["response_max_cycles"], 1) <= 1.25,
    }
    return {"result": "PASS" if all(checks.values()) else "FAIL", "checks": checks,
            "minimal": minimal, "r4": r4,
            "deltas": {"dma_observation_rate": dma_rate_delta,
                       "completion_rate": completion_rate_delta,
                       "admission_absolute": abs(admission_r4 - admission_minimal)}}


def verify_perturbation_pair_command(args: argparse.Namespace) -> int:
    result = verify_perturbation_pair(args.minimal.resolve(), args.r4.resolve())
    if args.output is not None:
        output = args.output.resolve()
        if output.exists():
            raise EvidenceError("pair-verifier output must be a new file")
        write_new(output, result)
    print(json.dumps(result, ensure_ascii=True, sort_keys=True))
    return 0 if result["result"] == "PASS" else 2


def verify_perturbation_suite(attempts: list[Path]) -> dict:
    """Validate the frozen six-run anti-cherry-pick order and all pairs."""
    if len(attempts) != 6 or len({path.resolve() for path in attempts}) != 6:
        raise EvidenceError("perturbation suite requires six distinct attempts")
    expected = ("MINIMAL", "R4", "R4", "MINIMAL", "MINIMAL", "R4")
    actual = []
    for root, profile in zip(attempts, expected):
        metrics, identity, h1 = perturbation_metrics(root.resolve(), profile)
        actual.append({"profile": profile, "attempt": str(root.resolve()),
                       "identity": identity, "definitions": h1["definitions"],
                       "metrics": metrics})
    reference = actual[0]
    suite_identity = all(
        entry["identity"].get("firmware_source_commit") ==
        reference["identity"].get("firmware_source_commit") and
        entry["identity"].get("harness_sha256") == reference["identity"].get("harness_sha256")
        for entry in actual)
    pairs = [
        verify_perturbation_pair(attempts[0].resolve(), attempts[1].resolve()),
        verify_perturbation_pair(attempts[3].resolve(), attempts[2].resolve()),
        verify_perturbation_pair(attempts[4].resolve(), attempts[5].resolve()),
    ]
    checks = {"frozen_run_order": True, "suite_identity": suite_identity,
              "all_three_pairs_pass": all(pair["result"] == "PASS" for pair in pairs)}
    return {"result": "PASS" if all(checks.values()) else "FAIL", "checks": checks,
            "attempts": actual, "pairs": pairs}


def verify_perturbation_suite_command(args: argparse.Namespace) -> int:
    result = verify_perturbation_suite(args.attempt)
    output = args.output.resolve()
    if output.exists():
        raise EvidenceError("suite-verifier output must be a new file")
    write_new(output, result)
    print(json.dumps(result, ensure_ascii=True, sort_keys=True))
    return 0 if result["result"] == "PASS" else 2


def import_perturbation_suite(args: argparse.Namespace) -> int:
    repo = args.repo.resolve()
    dirty = git(repo, "status", "--porcelain").splitlines()
    if any(not row[3:].replace("\\", "/").startswith("docs/evidence/r4/perturbation-ab/")
           for row in dirty):
        raise EvidenceError("suite import requires a clean worktree outside perturbation evidence")
    result = verify_perturbation_suite(args.attempt)
    if result["result"] != "PASS":
        raise EvidenceError("only a passing six-run perturbation suite may be imported")
    source = args.suite.resolve()
    recorded = json.loads(source.read_text(encoding="utf-8"))
    if recorded != result:
        raise EvidenceError("suite record is not the exact current verifier result")
    parent = repo / "docs" / "evidence" / "r4" / "perturbation-ab"
    number = 1
    while (parent / f"suite-{number:04d}.json").exists():
        number += 1
    write_new(parent / f"suite-{number:04d}.json", result)
    print(parent / f"suite-{number:04d}.json")
    return 0


def selftest(_: argparse.Namespace) -> int:
    words = [MAGIC, 1, SCHEMA, 1, 0, 0, 0, 0, 0, 0, COMPLETE]
    if evaluate("t12-soak-a", words)["result"] != "PASS":
        raise EvidenceError("positive parser self-test failed")
    words[4] = 1
    if evaluate("t12-soak-a", words)["result"] != "FAIL":
        raise EvidenceError("negative parser self-test failed")
    print("PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    sub = parser.add_subparsers(dest="command", required=True)
    attempt = sub.add_parser("run-attempt", help="run one fresh, external formal board attempt")
    attempt.add_argument("--case", required=True, choices=sorted(CASES))
    attempt.add_argument("--accounting", choices=("MINIMAL", "R4"))
    attempt.add_argument("--output-parent", type=Path, required=True)
    attempt.set_defaults(handler=run_attempt)
    imported = sub.add_parser("import-attempt", help="copy one sealed PASS attempt into Git evidence")
    imported.add_argument("--attempt", type=Path, required=True)
    imported.set_defaults(handler=import_attempt)
    verify = sub.add_parser("verify", help="verify a sealed attempt")
    verify.add_argument("--attempt", type=Path, required=True)
    verify.set_defaults(handler=lambda args: (verify_root(args.attempt.resolve()), print("PASS"), 0)[2])
    pair = sub.add_parser("verify-perturbation-pair", help="verify one immutable MINIMAL/R4 A/B pair")
    pair.add_argument("--minimal", type=Path, required=True)
    pair.add_argument("--r4", type=Path, required=True)
    pair.add_argument("--output", type=Path)
    pair.set_defaults(handler=verify_perturbation_pair_command)
    suite = sub.add_parser("verify-perturbation-suite", help="verify frozen six-run A/B perturbation closure")
    suite.add_argument("--attempt", type=Path, nargs=6, required=True)
    suite.add_argument("--output", type=Path, required=True)
    suite.set_defaults(handler=verify_perturbation_suite_command)
    imported_suite = sub.add_parser("import-perturbation-suite", help="import an exact passing six-run A/B aggregate")
    imported_suite.add_argument("--attempt", type=Path, nargs=6, required=True)
    imported_suite.add_argument("--suite", type=Path, required=True)
    imported_suite.set_defaults(handler=import_perturbation_suite)
    test = sub.add_parser("selftest", help="exercise the target-record parser without hardware")
    test.set_defaults(handler=selftest)
    args = parser.parse_args()
    try:
        return args.handler(args)
    except (EvidenceError, OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
