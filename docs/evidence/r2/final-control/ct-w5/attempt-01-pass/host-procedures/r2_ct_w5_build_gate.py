#!/usr/bin/env python3
"""CT-W5 K1/DROP target build gate.

Purpose:
1. Prove the current source tree has no firmware delta since the accepted
   CT-long-run target checkpoint.
2. Rebuild the sealed W6 K1/DROP CT-OFF profile and prove programmed-byte
   identity against the sealed hardware cell.
3. Build the CT-W5 K1/DROP/800-event/1500-ms candidate.
4. Freeze candidate ELF/programmed hashes and RAM usage.

No hardware access. No Git modification. No commit/push/tag.
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

CHECKPOINT = "b3aa0b926a49182e99095241a7939195073aa545"
FIRMWARE_BASELINE = "48da792e382e911aad1b7bb43765284aa8b58ea2"

EXPECTED_BURST_HOST_SHA256 = (
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

BURST_HOST = REPO / r"tools\r2\r2_ct_burst.py"

SEALED_CELL = REPO / r"docs\evidence\r2\w6\cells\k1-drop"
SEALED_ELF = SEALED_CELL / "cubemx.elf"
SEALED_MANIFEST = SEALED_CELL / "manifest.sha256"

OFF_BUILD = REPO / r"build\r2-ct-w5-off-k1-drop-b3aa0b92"
CT_BUILD = REPO / r"build\r2-ct-w5-k1-drop-800-t1500-b3aa0b92"

SUMMARY_JSON = CT_BUILD / "ct-w5-build-gate.json"

RAM_TOTAL = 131072
RAM_GATE = 102400


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
            "command failed\n"
            f"argv: {args!r}\n"
            f"exit: {result.returncode}\n"
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


def require_tool(path: Path) -> None:
    if not path.is_file():
        raise RuntimeError(
            f"required tool missing: {path}"
        )


def manifest_digest_for(
    manifest: Path,
    filename: str,
) -> str:
    suffix = "/" + filename.replace("\\", "/")

    for raw in manifest.read_text(
        encoding="utf-8",
        errors="replace",
    ).splitlines():
        line = raw.strip()

        if not line:
            continue

        match = re.fullmatch(
            r"([0-9A-Fa-f]{64})  (.+)",
            line,
        )

        if not match:
            raise RuntimeError(
                f"malformed manifest line: {raw!r}"
            )

        path_text = match.group(2).replace("\\", "/")

        if (
            path_text == filename.replace("\\", "/")
            or path_text.endswith(suffix)
        ):
            return match.group(1).upper()

    raise RuntimeError(
        f"{filename!r} not found in {manifest}"
    )


def clean_env() -> dict[str, str]:
    env = dict(os.environ)

    old_path = env.get("PATH", "")
    env["PATH"] = (
        str(ARM_BIN)
        + os.pathsep
        + old_path
    )

    return env


def configure_and_build(
    build_dir: Path,
    *,
    ct_enabled: bool,
    events: int = 96,
    timeout_ms: int = 1000,
) -> tuple[Path, Path]:
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
        "-DSTREAM_LAB_R2_W6_K=1",
        "-DSTREAM_LAB_R2_W6_MODE=DROP",
        (
            "-DSTREAM_LAB_R2_CT=ON"
            if ct_enabled
            else "-DSTREAM_LAB_R2_CT=OFF"
        ),
    ]

    if ct_enabled:
        args.extend(
            [
                f"-DSTREAM_LAB_R2_CT_EVENTS={events}",
                (
                    "-DSTREAM_LAB_R2_CT_RUN_TIMEOUT_MS="
                    f"{timeout_ms}"
                ),
            ]
        )

    run(
        args,
        cwd=REPO,
        env=clean_env(),
    )

    run(
        [
            str(CMAKE),
            "--build",
            str(build_dir),
            "--parallel",
        ],
        cwd=REPO,
        env=clean_env(),
    )

    elf = build_dir / "cubemx.elf"
    bin_file = build_dir / "cubemx.bin"

    require_file(elf)

    run(
        [
            str(OBJCOPY),
            "-O",
            "binary",
            str(elf),
            str(bin_file),
        ],
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


def require_ct_profile(
    build_dir: Path,
) -> None:
    ninja_file = build_dir / "build.ninja"
    require_file(ninja_file)

    text = ninja_file.read_text(
        encoding="utf-8",
        errors="replace",
    )

    required = (
        "STREAM_LAB_R2_CT=1",
        "R2_W6_K=1U",
        "R2_W6_DROP_MODE=1U",
        "R2_W6_EVENTS=800U",
        "R2_W6_RUN_TIMEOUT_MS=1500U",
    )

    missing = [
        item
        for item in required
        if item not in text
    ]

    if missing:
        raise RuntimeError(
            "CT-W5 compile definition(s) missing: "
            + ", ".join(missing)
        )


def main() -> int:
    print("=== CT-W5 K1/DROP TARGET BUILD GATE ===")
    print("Hardware operations: NONE")
    print("Git modification: NONE")

    # ------------------------------------------------------------
    # 1. Repository and architecture-level provenance
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
            f"wrong baseline: HEAD={head}, "
            f"origin/main={origin}"
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
            "firmware changed since accepted "
            "CT-long-run target checkpoint:\n"
            + firmware_delta
        )

    print("Firmware unchanged since CT-W3 target: PASS")

    # ------------------------------------------------------------
    # 2. Required tool / evidence identity
    # ------------------------------------------------------------

    for tool in (
        CMAKE,
        NINJA,
        OBJCOPY,
        SIZE_TOOL,
    ):
        require_tool(tool)

    for path in (
        TOOLCHAIN,
        BURST_HOST,
        SEALED_ELF,
        SEALED_MANIFEST,
    ):
        require_file(path)

    burst_host_hash = sha256(BURST_HOST)

    if (
        burst_host_hash
        != EXPECTED_BURST_HOST_SHA256
    ):
        raise RuntimeError(
            "burst host SHA256 mismatch: "
            + burst_host_hash
        )

    sealed_manifest_elf_hash = (
        manifest_digest_for(
            SEALED_MANIFEST,
            "cubemx.elf",
        )
    )

    sealed_actual_elf_hash = sha256(
        SEALED_ELF
    )

    if (
        sealed_actual_elf_hash
        != sealed_manifest_elf_hash
    ):
        raise RuntimeError(
            "sealed K1/DROP ELF does not match "
            "its cell manifest"
        )

    print("Burst host identity: PASS")
    print("Sealed K1/DROP ELF manifest identity: PASS")

    # ------------------------------------------------------------
    # 3. Refuse build reuse
    # ------------------------------------------------------------

    existing = [
        str(path)
        for path in (OFF_BUILD, CT_BUILD)
        if path.exists()
    ]

    if existing:
        raise RuntimeError(
            "refusing to reuse existing build directory:\n"
            + "\n".join(existing)
        )

    # ------------------------------------------------------------
    # 4. Reconstruct sealed programmed bytes in a temporary dir
    # ------------------------------------------------------------

    with tempfile.TemporaryDirectory(
        prefix="r2-ct-w5-sealed-"
    ) as td:
        sealed_bin = (
            Path(td)
            / "sealed-k1-drop.bin"
        )

        run(
            [
                str(OBJCOPY),
                "-O",
                "binary",
                str(SEALED_ELF),
                str(sealed_bin),
            ],
            cwd=REPO,
            env=clean_env(),
        )

        sealed_programmed_hash = sha256(
            sealed_bin
        )

    # ------------------------------------------------------------
    # 5. CT-OFF K1/DROP sealed regression
    # ------------------------------------------------------------

    off_elf, off_bin = configure_and_build(
        OFF_BUILD,
        ct_enabled=False,
    )

    off_programmed_hash = sha256(off_bin)

    if (
        off_programmed_hash
        != sealed_programmed_hash
    ):
        raise RuntimeError(
            "CT-OFF K1/DROP programmed-byte "
            "regression failed\n"
            f"sealed:  {sealed_programmed_hash}\n"
            f"rebuilt: {off_programmed_hash}"
        )

    print(
        "Sealed K1/DROP programmed-byte "
        "regression: PASS"
    )

    # ------------------------------------------------------------
    # 6. CT-W5 candidate build
    # ------------------------------------------------------------

    ct_elf, ct_bin = configure_and_build(
        CT_BUILD,
        ct_enabled=True,
        events=800,
        timeout_ms=1500,
    )

    require_ct_profile(
        CT_BUILD
    )

    size = parse_size(
        ct_elf
    )

    ram_used = int(
        size["ram_used_bytes"]
    )

    if ram_used > RAM_GATE:
        raise RuntimeError(
            f"CT-W5 RAM gate failed: "
            f"{ram_used} > {RAM_GATE}"
        )

    ct_elf_hash = sha256(
        ct_elf
    )
    ct_programmed_hash = sha256(
        ct_bin
    )

    # ------------------------------------------------------------
    # 7. Repository must remain untouched
    # ------------------------------------------------------------

    final_head = git(
        "rev-parse",
        "HEAD",
    )
    final_status = git(
        "status",
        "--porcelain=v1",
        "-uall",
    )

    if final_head != CHECKPOINT:
        raise RuntimeError(
            "HEAD changed during build gate"
        )

    if final_status:
        raise RuntimeError(
            "repository changed during build gate:\n"
            + final_status
        )

    # ------------------------------------------------------------
    # 8. Machine-readable gate record in the build directory
    # ------------------------------------------------------------

    result = {
        "result": "PASS",
        "checkpoint": CHECKPOINT,
        "firmware_baseline": FIRMWARE_BASELINE,
        "profile": {
            "k": 1,
            "mode": "DROP",
            "events": 800,
            "run_timeout_ms": 1500,
            "process_hold_blocks": 6,
        },
        "burst_host_sha256": burst_host_hash,
        "sealed_k1_drop": {
            "elf_sha256": sealed_actual_elf_hash,
            "programmed_sha256": (
                sealed_programmed_hash
            ),
        },
        "ct_off_regression": {
            "elf_sha256": sha256(off_elf),
            "programmed_sha256": (
                off_programmed_hash
            ),
            "programmed_identity": "PASS",
        },
        "ct_w5_candidate": {
            "elf_sha256": ct_elf_hash,
            "programmed_sha256": (
                ct_programmed_hash
            ),
            "build_directory": str(
                CT_BUILD
            ),
            **size,
        },
        "hardware_operations": False,
        "git_modification": False,
    }

    SUMMARY_JSON.write_text(
        json.dumps(
            result,
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
        newline="\n",
    )

    print()
    print("=== CT-W5 BUILD GATE RESULT ===")
    print("Target build gate:         PASS")
    print(
        "Profile:                   "
        "K1 / DROP / 800 events / 1500 ms"
    )
    print(
        "DROP process hold:         "
        "6 block periods"
    )
    print(
        "Sealed K1/DROP regression: PASS"
    )
    print(
        "Sealed programmed SHA256:  "
        + sealed_programmed_hash
    )
    print(
        "CT-W5 ELF SHA256:          "
        + ct_elf_hash
    )
    print(
        "CT-W5 programmed SHA256:   "
        + ct_programmed_hash
    )
    print(
        "RAM used:                  "
        f"{size['ram_used_bytes']} B "
        f"({size['ram_used_percent']}%)"
    )
    print(
        "RAM free:                  "
        f"{size['ram_free_bytes']} B"
    )
    print(
        "Build directory:           "
        + str(CT_BUILD)
    )
    print(
        "Gate JSON:                 "
        + str(SUMMARY_JSON)
    )
    print()
    print("Hardware:                  NOT RUN")
    print("Repository:                CLEAN")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print()
        print("=== CT-W5 BUILD GATE RESULT ===")
        print("Target build gate: FAIL")
        print(str(exc))
        print("Hardware: NOT TOUCHED")
        raise SystemExit(1)
