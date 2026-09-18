#!/usr/bin/env python3
"""R2 CT-W6 remaining K/mode matrix build gate.

Architecture-level role:
- freeze the six still-uncovered final-control K/mode cells;
- prove every corresponding CT-OFF W6 cell still rebuilds to its sealed
  hardware-tested programmed bytes;
- build every CT-ON 800-event / 1500-ms adverse-burst candidate;
- freeze artifact identities, RAM use, request-ID ranges, and acceptance
  semantics before any additional hardware operation.

No hardware access.
No Git modification.
No source modification.
No commit/push/tag.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import subprocess
import tempfile
from pathlib import Path


REPO = Path(r"E:\Projects\stm32-stream-lab")

CHECKPOINT = "94ca103c27f508f97c38645883a712e0c16a76e8"
FIRMWARE_BASELINE = "48da792e382e911aad1b7bb43765284aa8b58ea2"

EXPECTED_HOST_SHA256 = (
    "C70A794F61980DF6DBB0F4E6B61775C22E3AE51D37B2D32EB50BEE38717B143B"
)

CMAKE = Path(
    r"E:\DevTools\cmake-3.31.12-windows-x86_64\bin\cmake.exe"
)
NINJA = Path(
    r"E:\DevTools\ninja-1.13.1-windows-x86_64\ninja.exe"
)
ARM_BIN = Path(
    r"E:\DevTools\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi\bin"
)
OBJCOPY = ARM_BIN / "arm-none-eabi-objcopy.exe"
SIZE_TOOL = ARM_BIN / "arm-none-eabi-size.exe"

SOURCE = REPO / r"firmware\cubemx"
TOOLCHAIN = SOURCE / r"cmake\gcc-arm-none-eabi.cmake"
HOST_TOOL = REPO / r"tools\r2\r2_ct_burst.py"

W6_CELLS_ROOT = REPO / r"docs\evidence\r2\w6\cells"

ROOT = REPO / r"build\r2-ct-w6-matrix-94ca103c"
SUMMARY = ROOT / "ct-w6-matrix-build-gate.json"

RAM_TOTAL = 131072
RAM_GATE = 102400

EVENTS = 800
RUN_TIMEOUT_MS = 1500

# The two already-closed real-control cells are deliberately excluded:
#   K8/NORMAL -> CT-W3 uniform + CT-W4 adverse burst
#   K1/DROP   -> CT-W5 adverse burst + recovery intersection
#
# CT-W6 only fills the six remaining K/mode holes.
CELLS = (
    {"k": 1, "mode": "NORMAL", "base_request_id": 0x0600},
    {"k": 2, "mode": "NORMAL", "base_request_id": 0x0610},
    {"k": 2, "mode": "DROP",   "base_request_id": 0x0620},
    {"k": 4, "mode": "NORMAL", "base_request_id": 0x0630},
    {"k": 4, "mode": "DROP",   "base_request_id": 0x0640},
    {"k": 8, "mode": "DROP",   "base_request_id": 0x0650},
)


def run(
    args: list[str],
    *,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        args,
        cwd=str(cwd) if cwd else None,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        errors="replace",
        check=False,
    )

    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {args!r}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}"
        )

    return result


def git(*args: str) -> str:
    return run(
        ["git", "-C", str(REPO), *args]
    ).stdout.strip()


def sha256(path: Path) -> str:
    h = hashlib.sha256()

    with path.open("rb") as handle:
        for chunk in iter(
            lambda: handle.read(1024 * 1024),
            b"",
        ):
            h.update(chunk)

    return h.hexdigest().upper()


def require_file(path: Path) -> None:
    if not path.is_file():
        raise RuntimeError(
            f"required file missing: {path}"
        )


def clean_env() -> dict[str, str]:
    env = dict(os.environ)
    env["PATH"] = (
        str(ARM_BIN)
        + os.pathsep
        + env.get("PATH", "")
    )
    return env


def manifest_digest_for(
    manifest: Path,
    filename: str,
) -> str:
    pattern = re.compile(
        r"^([0-9A-Fa-f]{64})  (.+)$"
    )

    normalized_name = filename.replace("\\", "/")
    suffix = "/" + normalized_name

    for raw in manifest.read_text(
        encoding="utf-8",
        errors="replace",
    ).splitlines():
        if not raw:
            continue

        match = pattern.fullmatch(raw)

        if not match:
            raise RuntimeError(
                f"malformed manifest line in {manifest}: {raw!r}"
            )

        path_text = (
            match.group(2)
            .replace("\\", "/")
        )

        if (
            path_text == normalized_name
            or path_text.endswith(suffix)
        ):
            return match.group(1).upper()

    raise RuntimeError(
        f"{filename!r} not found in {manifest}"
    )


def cell_slug(k: int, mode: str) -> str:
    return f"k{k}-{mode.lower()}"


def sealed_cell_inputs(
    k: int,
    mode: str,
) -> tuple[Path, Path, str]:
    cell_dir = W6_CELLS_ROOT / cell_slug(k, mode)
    elf = cell_dir / "cubemx.elf"
    manifest = cell_dir / "manifest.sha256"

    require_file(elf)
    require_file(manifest)

    expected_elf = manifest_digest_for(
        manifest,
        "cubemx.elf",
    )
    actual_elf = sha256(elf)

    if actual_elf != expected_elf:
        raise RuntimeError(
            f"sealed {cell_slug(k, mode)} ELF does not match its manifest"
        )

    return elf, manifest, actual_elf


def run_logged(
    args: list[str],
    log_path: Path,
    *,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
) -> None:
    result = subprocess.run(
        args,
        cwd=str(cwd) if cwd else None,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        errors="replace",
        check=False,
    )

    log_path.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    log_path.write_text(
        result.stdout
        + (
            "\n--- STDERR ---\n"
            + result.stderr
            if result.stderr
            else ""
        ),
        encoding="utf-8",
        newline="\n",
    )

    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}); "
            f"see {log_path}\n"
            f"argv={args!r}"
        )


def configure_and_build(
    build_dir: Path,
    *,
    k: int,
    mode: str,
    ct_enabled: bool,
) -> tuple[Path, Path]:
    build_dir.mkdir(
        parents=True,
        exist_ok=False,
    )

    args = [
        str(CMAKE),
        "-S",
        str(SOURCE),
        "-B",
        str(build_dir),
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Debug",
        f"-DCMAKE_MAKE_PROGRAM={NINJA}",
        f"-DCMAKE_TOOLCHAIN_FILE={TOOLCHAIN}",
        "-DSTREAM_LAB_R2_W3=OFF",
        "-DSTREAM_LAB_R2_W4=OFF",
        "-DSTREAM_LAB_R2_W5=OFF",
        "-DSTREAM_LAB_R2_W6=ON",
        f"-DSTREAM_LAB_R2_W6_K={k}",
        f"-DSTREAM_LAB_R2_W6_MODE={mode}",
        (
            "-DSTREAM_LAB_R2_CT=ON"
            if ct_enabled
            else "-DSTREAM_LAB_R2_CT=OFF"
        ),
    ]

    if ct_enabled:
        args.extend(
            [
                f"-DSTREAM_LAB_R2_CT_EVENTS={EVENTS}",
                (
                    "-DSTREAM_LAB_R2_CT_RUN_TIMEOUT_MS="
                    f"{RUN_TIMEOUT_MS}"
                ),
            ]
        )

    run_logged(
        args,
        build_dir / "configure.txt",
        cwd=REPO,
        env=clean_env(),
    )

    run_logged(
        [
            str(CMAKE),
            "--build",
            str(build_dir),
            "--parallel",
        ],
        build_dir / "build.txt",
        cwd=REPO,
        env=clean_env(),
    )

    elf = build_dir / "cubemx.elf"
    bin_file = build_dir / "cubemx.bin"

    require_file(elf)

    run_logged(
        [
            str(OBJCOPY),
            "-O",
            "binary",
            str(elf),
            str(bin_file),
        ],
        build_dir / "objcopy.txt",
        cwd=REPO,
        env=clean_env(),
    )

    require_file(bin_file)

    return elf, bin_file


def parse_size(elf: Path) -> dict[str, int | float]:
    result = run(
        [
            str(SIZE_TOOL),
            str(elf),
        ],
        cwd=REPO,
        env=clean_env(),
    )

    match = re.search(
        r"(?m)^\s*"
        r"(\d+)\s+"
        r"(\d+)\s+"
        r"(\d+)\s+"
        r"(\d+)\s+"
        r"[0-9A-Fa-f]+\s+"
        r".+$",
        result.stdout,
    )

    if not match:
        raise RuntimeError(
            "could not parse arm-none-eabi-size output\n"
            + result.stdout
        )

    text_bytes = int(match.group(1))
    data_bytes = int(match.group(2))
    bss_bytes = int(match.group(3))
    dec_bytes = int(match.group(4))

    ram_used = data_bytes + bss_bytes
    ram_free = RAM_TOTAL - ram_used

    return {
        "text_bytes": text_bytes,
        "data_bytes": data_bytes,
        "bss_bytes": bss_bytes,
        "dec_bytes": dec_bytes,
        "ram_used_bytes": ram_used,
        "ram_free_bytes": ram_free,
        "ram_used_percent": round(
            100.0 * ram_used / RAM_TOTAL,
            2,
        ),
    }


def verify_ct_definitions(
    build_dir: Path,
    *,
    k: int,
    mode: str,
) -> None:
    ninja = build_dir / "build.ninja"
    require_file(ninja)

    text = ninja.read_text(
        encoding="utf-8",
        errors="replace",
    )

    expected_mode = 0 if mode == "NORMAL" else 1

    required = (
        "STREAM_LAB_R2_CT=1",
        f"R2_W6_K={k}U",
        f"R2_W6_DROP_MODE={expected_mode}U",
        f"R2_W6_EVENTS={EVENTS}U",
        f"R2_W6_RUN_TIMEOUT_MS={RUN_TIMEOUT_MS}U",
    )

    missing = [
        item
        for item in required
        if item not in text
    ]

    if missing:
        raise RuntimeError(
            f"{cell_slug(k, mode)} missing compile definitions: "
            + ", ".join(missing)
        )


def acceptance_semantics(
    k: int,
    mode: str,
) -> dict:
    if mode == "NORMAL":
        return {
            "input_count": EVENTS,
            "admitted_count": EVENTS,
            "capacity_drop_count": 0,
            "processed_count": EVENTS,
            "released_count": EVENTS,
            "recovery_count": 0,
            "max_drop_streak": 0,
            "traffic_profile": "ADVERSE_BURST",
            "requests": 10,
            "payload_bytes_each": 64,
            "single_write_bytes": 760,
        }

    return {
        "input_count": EVENTS,
        "admitted_count_min": k + 3,
        "capacity_drop_count_min": 8,
        "processed_equals_admitted": True,
        "released_equals_admitted": True,
        "recovery_count_min": 3,
        "max_drop_streak_min": 3,
        "traffic_profile": "ADVERSE_BURST",
        "requests": 10,
        "payload_bytes_each": 64,
        "single_write_bytes": 760,
        "require_drop_then_later_admit_during_control_interval": True,
    }


def main() -> int:
    print("=== R2 CT-W6 REMAINING MATRIX BUILD GATE ===")
    print("Hardware operations: NONE")
    print("Source/Git modification: NONE")
    print("Matrix size: 6 cells")

    # ------------------------------------------------------------
    # 1. Repository / architecture-level provenance
    # ------------------------------------------------------------

    head = git("rev-parse", "HEAD")
    origin = git("rev-parse", "origin/main")
    status = git(
        "status",
        "--porcelain=v1",
        "-uall",
    )

    if head != CHECKPOINT or origin != CHECKPOINT:
        raise RuntimeError(
            f"wrong baseline: HEAD={head}, origin/main={origin}"
        )

    if status:
        raise RuntimeError(
            f"working tree is not clean:\n{status}"
        )

    if git("tag", "--list", "r2-pass"):
        raise RuntimeError(
            "r2-pass exists unexpectedly"
        )

    firmware_delta = git(
        "diff",
        "--name-only",
        f"{FIRMWARE_BASELINE}..HEAD",
        "--",
        "firmware",
    )

    if firmware_delta:
        raise RuntimeError(
            "firmware changed since accepted CT target baseline:\n"
            + firmware_delta
        )

    print("Repository/source provenance: PASS")

    # ------------------------------------------------------------
    # 2. Tool and host identity
    # ------------------------------------------------------------

    for path in (
        CMAKE,
        NINJA,
        OBJCOPY,
        SIZE_TOOL,
        TOOLCHAIN,
        HOST_TOOL,
    ):
        require_file(path)

    host_hash = sha256(HOST_TOOL)

    if host_hash != EXPECTED_HOST_SHA256:
        raise RuntimeError(
            f"adverse-burst host identity mismatch: {host_hash}"
        )

    print("Sealed adverse-burst host identity: PASS")

    # ------------------------------------------------------------
    # 3. Validate all sealed cell inputs before creating build dirs
    # ------------------------------------------------------------

    sealed_inputs: dict[str, tuple[Path, str, str]] = {}

    for cell in CELLS:
        k = cell["k"]
        mode = cell["mode"]
        slug = cell_slug(k, mode)

        elf, _, elf_hash = sealed_cell_inputs(k, mode)

        with tempfile.TemporaryDirectory(
            prefix=f"r2-ct-w6-{slug}-"
        ) as td:
            sealed_bin = Path(td) / "sealed.bin"

            run(
                [
                    str(OBJCOPY),
                    "-O",
                    "binary",
                    str(elf),
                    str(sealed_bin),
                ],
                cwd=REPO,
                env=clean_env(),
            )

            programmed_hash = sha256(
                sealed_bin
            )

        sealed_inputs[slug] = (
            elf,
            elf_hash,
            programmed_hash,
        )

        print(
            f"Sealed input {slug}: "
            f"ELF {elf_hash[:12]}..., "
            f"programmed {programmed_hash[:12]}... PASS"
        )

    # ------------------------------------------------------------
    # 4. Refuse build-root reuse
    # ------------------------------------------------------------

    if ROOT.exists():
        raise RuntimeError(
            f"refusing to reuse existing CT-W6 build root: {ROOT}"
        )

    ROOT.mkdir(
        parents=True,
        exist_ok=False,
    )

    results: list[dict] = []

    # ------------------------------------------------------------
    # 5. Per-cell CT-OFF regression + CT-ON candidate
    # ------------------------------------------------------------

    for index, cell in enumerate(CELLS, start=1):
        k = cell["k"]
        mode = cell["mode"]
        base_id = cell["base_request_id"]
        slug = cell_slug(k, mode)

        print()
        print(
            f"--- [{index}/6] {slug}: "
            "sealed regression + CT candidate ---"
        )

        sealed_elf, sealed_elf_hash, sealed_programmed = (
            sealed_inputs[slug]
        )

        off_dir = ROOT / f"{slug}-ct-off"
        ct_dir = ROOT / f"{slug}-ct-on-800-t1500"

        off_elf, off_bin = configure_and_build(
            off_dir,
            k=k,
            mode=mode,
            ct_enabled=False,
        )

        off_programmed = sha256(
            off_bin
        )

        if off_programmed != sealed_programmed:
            raise RuntimeError(
                f"{slug} CT-OFF programmed regression failed\n"
                f"sealed:  {sealed_programmed}\n"
                f"rebuilt: {off_programmed}"
            )

        print(
            f"{slug} CT-OFF programmed-byte regression: PASS"
        )

        ct_elf, ct_bin = configure_and_build(
            ct_dir,
            k=k,
            mode=mode,
            ct_enabled=True,
        )

        verify_ct_definitions(
            ct_dir,
            k=k,
            mode=mode,
        )

        size = parse_size(
            ct_elf
        )

        ram_used = int(
            size["ram_used_bytes"]
        )

        if ram_used > RAM_GATE:
            raise RuntimeError(
                f"{slug} RAM gate failed: "
                f"{ram_used} > {RAM_GATE}"
            )

        ct_elf_hash = sha256(
            ct_elf
        )
        ct_programmed_hash = sha256(
            ct_bin
        )

        print(
            f"{slug} CT-ON: RAM "
            f"{size['ram_used_bytes']} B "
            f"({size['ram_used_percent']}%), "
            f"ELF {ct_elf_hash[:12]}..., "
            f"programmed {ct_programmed_hash[:12]}... PASS"
        )

        results.append(
            {
                "cell": slug,
                "k": k,
                "mode": mode,
                "base_request_id": base_id,
                "last_request_id": base_id + 9,
                "sealed_w6": {
                    "elf_sha256": sealed_elf_hash,
                    "programmed_sha256": sealed_programmed,
                },
                "ct_off_regression": {
                    "elf_sha256": sha256(off_elf),
                    "programmed_sha256": off_programmed,
                    "programmed_identity": "PASS",
                    "build_directory": str(off_dir),
                },
                "ct_on_candidate": {
                    "elf_sha256": ct_elf_hash,
                    "programmed_sha256": ct_programmed_hash,
                    "build_directory": str(ct_dir),
                    **size,
                },
                "acceptance": acceptance_semantics(
                    k,
                    mode,
                ),
            }
        )

    # ------------------------------------------------------------
    # 6. Final repo invariance and freeze record
    # ------------------------------------------------------------

    if git("rev-parse", "HEAD") != CHECKPOINT:
        raise RuntimeError(
            "HEAD changed during build gate"
        )

    if git("rev-parse", "origin/main") != CHECKPOINT:
        raise RuntimeError(
            "origin/main changed during build gate"
        )

    if git("status", "--porcelain=v1", "-uall"):
        raise RuntimeError(
            "repository changed during build gate"
        )

    coverage_before = {
        "k8-normal": (
            "CLOSED by CT-W3 worst-permitted uniform + "
            "CT-W4 adverse burst"
        ),
        "k1-drop": (
            "CLOSED by CT-W5 adverse burst + "
            "in-window DROP->ADMIT recovery"
        ),
    }

    output = {
        "result": "PASS",
        "checkpoint": CHECKPOINT,
        "firmware_baseline": FIRMWARE_BASELINE,
        "host_tool_sha256": host_hash,
        "traffic_profile_for_remaining_matrix": {
            "name": "ADVERSE_BURST",
            "requests": 10,
            "payload_bytes_each": 64,
            "request_frame_bytes_each": 76,
            "single_write_bytes": 760,
            "intentional_inter_request_delay_ms": 0,
            "wait_for_reply_before_burst_complete": False,
        },
        "events": EVENTS,
        "run_timeout_ms": RUN_TIMEOUT_MS,
        "coverage_already_closed": coverage_before,
        "remaining_cells": results,
        "cell_count": len(results),
        "hardware_operations": False,
        "git_modification": False,
    }

    SUMMARY.write_text(
        json.dumps(
            output,
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
        newline="\n",
    )

    print()
    print("=== CT-W6 MATRIX BUILD GATE RESULT ===")
    print("Build gate:             PASS")
    print("Remaining cells:        6 / 6 BUILT")
    print("CT-OFF regressions:     6 / 6 PROGRAMMED-BYTE PASS")
    print("CT-ON profiles:         6 / 6 K/MODE/800/1500 PASS")
    print("RAM gate:               6 / 6 PASS")
    print("Traffic profile:        SEALED ADVERSE BURST")
    print("Requests per cell:      10 x 64 B")
    print("Submission per cell:    one 760 B serial write")
    print(f"Build root:             {ROOT}")
    print(f"Matrix JSON:            {SUMMARY}")
    print()
    print("Hardware:               NOT RUN")
    print("Repository:             CLEAN")
    print("r2-pass:                NOT CREATED")

    print()
    print("=== FROZEN HARDWARE MATRIX ===")

    for item in results:
        print(
            f"{item['cell']}: "
            f"IDs 0x{item['base_request_id']:04X}"
            f"..0x{item['last_request_id']:04X}, "
            f"ELF={item['ct_on_candidate']['elf_sha256']}, "
            f"BIN={item['ct_on_candidate']['programmed_sha256']}"
        )

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print()
        print("=== CT-W6 MATRIX BUILD GATE RESULT ===")
        print("Build gate: FAIL")
        print(str(exc))
        print("Hardware: NOT TOUCHED")
        print("No automatic cleanup/retry was performed.")
        raise SystemExit(1)
