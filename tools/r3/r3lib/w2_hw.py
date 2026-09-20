from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re

from .cases import CASES
from .git_state import run_git

W2_HW_PROFILE = "STREAM_LAB_R3_W2_HW"
W2_HW_SYNTHETIC_IRQ = "TIM6_DAC_IRQn"
W2_HW_IRQ_PRIORITY = 5
W2_HW_TARGET_BOUND_S = 0.5
W2_HW_HOST_TIMEOUT_S = 2.0
W2_HW_CASE_CACHE_VARIABLE = "STREAM_LAB_R3_W2_HW_CASE"
W2_HW_AUTHORITY_LINK_MODE = "TEST_SHIM_REPLACES_STREAM_RUN_AUTHORITY"
W2_HW_REAL_AUTHORITY_SOURCE_TOKEN = "../runtime/stream_run_authority.c"
W2_HW_RESULT_COMMIT_RULE = "completed_magic_written_last_after_DMB"
W2_HW_FREEZE_DOC = "docs/r3/w2/R3_W2_HW_HARNESS_FREEZE.md"

W2_HW_CASE_SELECTORS = (
    ("W2-T03-A", "T03_A"),
    ("W2-T03-C", "T03_C"),
    ("W2-T03-D", "T03_D"),
    ("W2-T05-A", "T05_A"),
    ("W2-T05-B", "T05_B"),
)

W2_HW_CASES = (
    "W2-T03-A",
    "W2-T03-C",
    "W2-T03-D",
    "W2-T05-A",
    "W2-T05-B",
)

W2_NATIVE_ONLY_CASES = (
    "W2-T03-B",
    "W2-ACK-A",
    "W2-ACK-B",
)

W2_HW_REQUIRED_WORKER_PATHS = (
    "firmware/runtime/r3_worker_contract.c",
    "firmware/runtime/r3_worker_contract.h",
    "firmware/runtime/r3_worker_tasks.c",
    "firmware/runtime/r3_worker_tasks.h",
)

W2_HW_FREEZE_PATHS = (
    "docs/r3/w2/R3_W2_HW_HARNESS_FREEZE.md",
    "tools/r3/r3lib/w2_hw.py",
    "tests/r3_host/test_w2_hw_preflight.py",
)

W2_HW_PLANNED_NEW_PATHS = (
    "firmware/runtime/r3_w2_hw_harness.c",
    "firmware/runtime/r3_w2_hw_harness.h",
    "firmware/runtime/r3_w2_hw_authority_shim.c",
    "firmware/runtime/r3_w2_hw_authority_shim.h",
)

W2_HW_PLANNED_MODIFIED_PATHS = (
    "firmware/cubemx/CMakeLists.txt",
    "firmware/cubemx/Core/Src/main.c",
)


@dataclass(frozen=True)
class W2HardwarePreflight:
    freeze_committed: bool
    worker_impl_present: bool
    case_partition_ok: bool
    target_profile_present: bool
    target_harness_present: bool
    evidence_present: bool
    synthetic_irq_available: bool
    irq_priority_contract_ok: bool
    worker_isr_api_present: bool
    authority_link_contract_ok: bool
    authority_link_substitution_required: bool
    case_selector_contract_ok: bool
    result_commit_protocol_frozen: bool
    strong_irq_definition_paths: tuple[str, ...]
    ready_for_implementation: bool
    next_allowed: str


def _tree_has(repo: Path, rel: str, rev: str = "HEAD") -> bool:
    return run_git(repo, "cat-file", "-e", f"{rev}:{rel}", check=False).returncode == 0


def _tree_has_all(repo: Path, paths: tuple[str, ...], rev: str = "HEAD") -> bool:
    return all(_tree_has(repo, rel, rev) for rel in paths)


def _tree_has_prefix(repo: Path, prefix: str, rev: str = "HEAD") -> bool:
    cp = run_git(repo, "ls-tree", "-r", "--name-only", rev, "--", prefix, check=False)
    return cp.returncode == 0 and bool(cp.stdout.strip())


def _show_text(repo: Path, rel: str, rev: str = "HEAD") -> str:
    cp = run_git(repo, "show", f"{rev}:{rel}", check=False)
    if cp.returncode != 0:
        return ""
    return cp.stdout.decode("utf-8", "replace")


def w2_case_partition(cases=CASES) -> tuple[tuple[str, ...], tuple[str, ...]]:
    hardware = []
    native_only = []
    for cid, work_package, evidence, _description in cases:
        if work_package != "W2":
            continue
        if "hardware" in evidence:
            hardware.append(cid)
        elif evidence == "native":
            native_only.append(cid)
    return tuple(hardware), tuple(native_only)


def case_partition_ok(cases=CASES) -> bool:
    hardware, native_only = w2_case_partition(cases)
    return hardware == W2_HW_CASES and native_only == W2_NATIVE_ONLY_CASES


def case_selector_contract_ok() -> bool:
    formal = tuple(cid for cid, _selector in W2_HW_CASE_SELECTORS)
    selectors = tuple(selector for _cid, selector in W2_HW_CASE_SELECTORS)
    return (
        formal == W2_HW_CASES
        and len(set(selectors)) == len(selectors)
        and all(re.fullmatch(r"[A-Z0-9_]+", selector) for selector in selectors)
    )


def _strong_irq_definition_paths(repo: Path) -> tuple[str, ...]:
    cp = run_git(
        repo, "ls-tree", "-r", "--name-only", "HEAD", "--", "firmware",
        check=False,
    )
    if cp.returncode != 0:
        return ()
    rx = re.compile(
        r"\bTIM6_DAC_IRQHandler\s*\([^;{}]*\)\s*\{",
        re.MULTILINE,
    )
    matches = []
    for rel in cp.stdout.decode("utf-8", "replace").splitlines():
        if not rel.endswith(".c"):
            continue
        if rx.search(_show_text(repo, rel)):
            matches.append(rel)
    return tuple(matches)


def _freeze_contracts(repo: Path) -> tuple[bool, bool]:
    doc = _show_text(repo, W2_HW_FREEZE_DOC)
    authority = (
        f"AUTHORITY_LINK_MODE: {W2_HW_AUTHORITY_LINK_MODE}" in doc
        and f"REAL_AUTHORITY_SOURCE_TOKEN: {W2_HW_REAL_AUTHORITY_SOURCE_TOKEN}" in doc
    )
    result = (
        f"RESULT_COMMIT_RULE: {W2_HW_RESULT_COMMIT_RULE}" in doc
        and "completed_magic" in doc
    )
    return authority, result


def derive_w2_hw_preflight(
    *,
    freeze_committed: bool,
    worker_impl_present: bool,
    case_partition_valid: bool,
    target_profile_present: bool,
    target_harness_present: bool,
    evidence_present: bool,
    synthetic_irq_available: bool,
    irq_priority_contract_ok: bool,
    worker_isr_api_present: bool,
    authority_link_contract_ok: bool,
    authority_link_substitution_required: bool,
    case_selector_contract_valid: bool,
    result_commit_protocol_frozen: bool,
    strong_irq_definition_paths: tuple[str, ...] = (),
) -> W2HardwarePreflight:
    ready = all((
        freeze_committed,
        worker_impl_present,
        case_partition_valid,
        synthetic_irq_available,
        irq_priority_contract_ok,
        worker_isr_api_present,
        authority_link_contract_ok,
        authority_link_substitution_required,
        case_selector_contract_valid,
        result_commit_protocol_frozen,
        not target_profile_present,
        not target_harness_present,
        not evidence_present,
    ))
    return W2HardwarePreflight(
        freeze_committed=freeze_committed,
        worker_impl_present=worker_impl_present,
        case_partition_ok=case_partition_valid,
        target_profile_present=target_profile_present,
        target_harness_present=target_harness_present,
        evidence_present=evidence_present,
        synthetic_irq_available=synthetic_irq_available,
        irq_priority_contract_ok=irq_priority_contract_ok,
        worker_isr_api_present=worker_isr_api_present,
        authority_link_contract_ok=authority_link_contract_ok,
        authority_link_substitution_required=authority_link_substitution_required,
        case_selector_contract_ok=case_selector_contract_valid,
        result_commit_protocol_frozen=result_commit_protocol_frozen,
        strong_irq_definition_paths=strong_irq_definition_paths,
        ready_for_implementation=ready,
        next_allowed=(
            "W2_HW_HARNESS_IMPLEMENTATION"
            if ready else "PRINCIPAL_W2_HW_PREFLIGHT_REVIEW"
        ),
    )


def inspect_w2_hw_preflight(repo: Path) -> W2HardwarePreflight:
    cmake = _show_text(repo, "firmware/cubemx/CMakeLists.txt")
    main_c = _show_text(repo, "firmware/cubemx/Core/Src/main.c")
    freertos_config = _show_text(repo, "firmware/cubemx/Core/Inc/FreeRTOSConfig.h")
    worker_h = _show_text(repo, "firmware/runtime/r3_worker_tasks.h")

    profile_present = W2_HW_PROFILE in cmake or W2_HW_PROFILE in main_c
    harness_present = any(_tree_has(repo, rel) for rel in W2_HW_PLANNED_NEW_PATHS)
    evidence_present = _tree_has_prefix(repo, "docs/evidence/r3/w2/")
    strong_irq_paths = _strong_irq_definition_paths(repo)
    irq_available = len(strong_irq_paths) == 0
    priority_match = re.search(
        r"#define\s+configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY\s+(\d+)",
        freertos_config,
    )
    priority_ok = bool(priority_match and int(priority_match.group(1)) == W2_HW_IRQ_PRIORITY)
    worker_isr_api_present = (
        "R3WorkerTasks_Create" in worker_h
        and "R3WorkerTasks_NotifyProcessingWorkFromISR" in worker_h
    )
    authority_contract_ok, result_protocol_frozen = _freeze_contracts(repo)
    link_substitution_required = W2_HW_REAL_AUTHORITY_SOURCE_TOKEN in cmake

    return derive_w2_hw_preflight(
        freeze_committed=_tree_has_all(repo, W2_HW_FREEZE_PATHS),
        worker_impl_present=_tree_has_all(repo, W2_HW_REQUIRED_WORKER_PATHS),
        case_partition_valid=case_partition_ok(),
        target_profile_present=profile_present,
        target_harness_present=harness_present,
        evidence_present=evidence_present,
        synthetic_irq_available=irq_available,
        irq_priority_contract_ok=priority_ok,
        worker_isr_api_present=worker_isr_api_present,
        authority_link_contract_ok=authority_contract_ok,
        authority_link_substitution_required=link_substitution_required,
        case_selector_contract_valid=case_selector_contract_ok(),
        result_commit_protocol_frozen=result_protocol_frozen,
        strong_irq_definition_paths=strong_irq_paths,
    )
