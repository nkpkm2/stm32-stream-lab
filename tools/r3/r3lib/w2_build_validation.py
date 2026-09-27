"""Read-only-to-repository build validation for the reviewed W2-HW candidate.

Not a flash tool, not a firmware acceptance tool, and not a dirty-tree bypass
for hardware. Invoked only through r3_harness.py w2-hw-validate --candidate.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import signal
import struct
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from typing import Any

BASE_HEAD = "f80f774607383d01c516eff0b34fdb828f5610ba"
R2_ANCHOR = "48da792e382e911aad1b7bb43765284aa8b58ea2"
CANDIDATE_SHA256 = {
    "firmware/cubemx/CMakeLists.txt": "10DC99C4B88AAB9470EAD01A6D9AA80573D733B709D21E3C210AE2762CDA7143",
    "firmware/cubemx/Core/Src/main.c": "240113D83147474BDB00A2EF793A295B946E3D3FFB5631E74A815FD084BD476C",
    "firmware/runtime/r3_w2_hw_harness.c": "827E4FCACD2E75B3E8E91ACB6445FEEAF03E473778BDE11127F76BF44EAB5820",
    "firmware/runtime/r3_w2_hw_harness.h": "B929FE0C8016D6D409DCA98388847450EF2D9D43894B875A208F4E17AF620FEF",
    "firmware/runtime/r3_w2_hw_authority_shim.c": "91CB56A2D75604078D5606F14C393B9F8671554A87A7A24A0B26800C0EB23672",
    "firmware/runtime/r3_w2_hw_authority_shim.h": "0F8E33972108114DA51A58D6F47541FBDF8F19E4739BCD80FC396599970DE5A7",
}
HOST_PATHS = (
    "tools/r3/r3_harness.py",
    "tools/r3/r3lib/__init__.py",
    "tools/r3/r3lib/w2_build_validation.py",
    "tests/r3_host/test_w2_build_validation.py",
    "docs/r3/w2/R3_W2_HW_BUILD_VALIDATION.md",
)
MODIFIED_PATHS = {
    "firmware/cubemx/CMakeLists.txt", "firmware/cubemx/Core/Src/main.c",
    "tools/r3/r3_harness.py", "tools/r3/r3lib/__init__.py",
}
EXPECTED_DIRTY = {
    path: (" M" if path in MODIFIED_PATHS else "??")
    for path in (*CANDIDATE_SHA256, *HOST_PATHS)
}
CASES = {"T03_A": 1, "T03_C": 2, "T03_D": 3, "T05_A": 4, "T05_B": 5}
RUNTIME_SOURCES = (
    "r3_w2_hw_harness.c", "r3_w2_hw_authority_shim.c",
    "r3_worker_tasks.c", "r3_worker_contract.c",
)
SHIM_SYMBOLS = (
    "StreamRunAuthority_TakeReady", "StreamRunAuthority_ClaimHeldReady",
    "StreamRunAuthority_CancelHeldReady", "StreamRunAuthority_CompleteAndReleaseBlock",
    "StreamRunAuthority_GetSnapshot",
)
REQUIRED_TEXT_SYMBOLS = (
    *SHIM_SYMBOLS, "R3_W2_HW_Start", "TIM6_DAC_IRQHandler",
    "R3WorkerTasks_Create", "R3WorkerTasks_NotifyProcessingWorkFromISR",
    "vTaskStartScheduler", "xTaskCreateStatic",
)
DEFAULTS = {
    "STREAM_LAB_R2_W3": "OFF", "STREAM_LAB_R2_W4": "OFF",
    "STREAM_LAB_R2_W5": "OFF", "STREAM_LAB_R2_W6": "OFF",
    "STREAM_LAB_R2_CT": "OFF", "STREAM_LAB_R2_CT_EVENTS": "96",
    "STREAM_LAB_R2_CT_RUN_TIMEOUT_MS": "1000", "STREAM_LAB_R2_W6_K": "1",
    "STREAM_LAB_R2_W6_MODE": "NORMAL",
    "STREAM_LAB_FOUNDATION_ADC_DBM_DRIVER": "OFF",
    "STREAM_LAB_FOUNDATION_OWNERSHIP_CORE": "OFF",
    "STREAM_LAB_FOUNDATION_TOKEN_LEDGER": "OFF",
    "STREAM_LAB_FOUNDATION_QUEUE_ADAPTER": "OFF",
    "STREAM_LAB_R3_WORKER_CONTRACT": "OFF",
    "STREAM_LAB_R3_WORKER_TASKS": "OFF", "STREAM_LAB_R3_W2_HW": "OFF",
    "STREAM_LAB_R3_W2_HW_CASE": "",
}
HISTORICAL_HASHES = {
    "default": "BEB4EC27F19F1C09B33C703AE1E8DA1738E2B0AA5B12C580B82D7411367181EE",
    "w3": "69A3F53AF64627D392AD8BF5EBCF815DEF5ED69BA92FC0692AD0BE2E9B3A7BB5",
    "w4": "7406F071CF6027449381DDF84B93558D15D8C9AD90B03F3F1135CCC440CF950D",
    "w5": "1754A7CF2BF24F3CC5729A2F3FD2160A1CA3EDA9569C8F1EE738A3E6B1F456CD",
    "w6_k1_normal": "AFD70C9B4E580AAB475577D3960223B891F4A760705FFA29CA781DA77853D64A",
    "w6_k1_drop": "0242FCFAAD2994B4547C5F9485CB9C5E308A9A36CB75158E3775E3F5736C2B45",
    "w6_k2_normal": "FCCA0ED1004CACB379752B6767CC3DAC4E3F154235C0B2B8F9EEF78C527E5579",
    "w6_k2_drop": "1EC4CFB4D1D0E4273E13B966FEA0983CDC2A73E5B0ECF36EDA24B5D9D66AE879",
    "w6_k4_normal": "4EA3D68FCDACF3976C0592A0BF5AA360B15703765D101BF665CC5AB8F84D2F76",
    "w6_k4_drop": "92D9A41DB004E57766B6EF852DEFC81B747E40A46EF017D606528625CD8A0D4A",
    "w6_k8_normal": "CFE5D02BD66BDEDFFBD25D04A9D29A559477FE546677E2862DC2EA8DF2AFCA10",
    "w6_k8_drop": "57E02134B44EDBD66EA111BD3B3320B3E0A929AB9364A7C13C48F3D7D60784E3",
    "w6_k8_normal_ct800": "D83E2B181F33EB1A5D994D61DA02083D096C23181015A8795E378152DC9C44F7",
}


class ValidationError(RuntimeError):
    def __init__(self, category: str, message: str):
        super().__init__(message)
        self.category = category


def require(condition: bool, message: str, category: str = "VALIDATION_CONTRACT") -> None:
    if not condition:
        raise ValidationError(category, message)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def file_hash(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest().upper()


def write_json_new(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as f:
        json.dump(value, f, ensure_ascii=True, indent=2, allow_nan=False)
        f.write("\n")


def parse_status(data: bytes) -> dict[str, str]:
    """Exact v1 -z shape; reject renames/unmerged/duplicates instead of guessing."""
    if not data:
        return {}
    require(data.endswith(b"\0"), "unterminated porcelain -z record", "GIT_STATE")
    result: dict[str, str] = {}
    for raw in data.split(b"\0")[:-1]:
        require(len(raw) >= 4 and raw[2:3] == b" ", "malformed porcelain record", "GIT_STATE")
        xy = raw[:2].decode("ascii", "strict")
        require(xy in (" M", "??", "M ", "A ", "MM", "AM"),
                f"unsupported/conflicted status: {xy!r}", "GIT_STATE")
        path = raw[3:].decode("utf-8", "strict")
        require(path not in result, f"duplicate status path: {path}", "GIT_STATE")
        result[path] = xy
    return result


def git_read(repo: Path, *args: str) -> bytes:
    """Read-only Git subset; no network, no index refresh, no mutation commands."""
    require(args and args[0] in {"rev-parse", "cat-file", "status", "ls-files", "diff"},
            "Git write/network command is outside validator capability")
    try:
        cp = subprocess.run(
            ["git", "--no-optional-locks", "-C", str(repo), *args],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=90,
            env={**os.environ, "GIT_OPTIONAL_LOCKS": "0", "GIT_TERMINAL_PROMPT": "0"},
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise ValidationError("GIT_STATE", str(exc)) from exc
    require(cp.returncode == 0, cp.stderr.decode("utf-8", "replace"), "GIT_STATE")
    return cp.stdout


def snapshot(repo: Path, *, enforce: bool = True) -> dict[str, Any]:
    head = git_read(repo, "rev-parse", "HEAD").decode("ascii").rstrip("\r\n")
    tag = git_read(repo, "rev-parse", "r2-pass^{}").decode("ascii").rstrip("\r\n")
    tag_type = git_read(repo, "cat-file", "-t", "r2-pass").decode("ascii").rstrip("\r\n")
    states = parse_status(git_read(repo, "status", "--porcelain=v1", "-z", "--untracked-files=all"))
    index = git_read(repo, "ls-files", "--stage", "-z")
    if enforce:
        require(head == BASE_HEAD, f"unexpected HEAD: {head}", "GIT_STATE")
        require(tag == R2_ANCHOR and tag_type == "tag", "historical r2-pass changed", "GIT_STATE")
        require(states == EXPECTED_DIRTY, f"expected exact 11-path unstaged candidate; observed={states!r}", "GIT_STATE")
        require(not git_read(repo, "diff", "--cached", "--name-only", "-z"), "index is not empty", "GIT_STATE")
        for rel, expected in CANDIDATE_SHA256.items():
            require(file_hash(repo / rel) == expected, f"candidate byte mismatch: {rel}", "SOURCE_IDENTITY")
    tracked = git_read(repo, "ls-files", "-z").split(b"\0")
    paths = {p.decode("utf-8", "strict") for p in tracked if p} | set(states)
    hashes = {}
    for rel in sorted(paths):
        path = repo / rel
        require(path.is_file() and not path.is_symlink(), f"unsupported/non-file input: {rel}", "SOURCE_IDENTITY")
        hashes[rel] = file_hash(path)
    return {"head": head, "r2_pass": tag, "r2_tag_type": tag_type,
            "index_sha256": digest(index), "status": states, "file_sha256": hashes}


def validate_snapshot_unchanged(before: dict, after: dict) -> None:
    require(before == after, "source / index / HEAD / anchor changed during validation", "SOURCE_CHANGED")


class CommandLog:
    """Persist stdout, stderr and exit status even when a subprocess fails."""
    def __init__(self, root: Path):
        self.root = root
        self.sequence = 0

    def run(self, args: list[Any], *, name: str, env: dict | None = None,
            cwd: Path | None = None, timeout: float = 600,
            allow_failure: bool = False, category: str = "BUILD") -> tuple[int, str, str]:
        self.sequence += 1
        cmd = [str(arg) for arg in args]
        folder = self.root / "commands" / f"{self.sequence:03d}-{name}"
        folder.mkdir(parents=True, exist_ok=False)
        write_json_new(folder / "request.json", {
            "argv": cmd, "cwd": str(cwd) if cwd else None,
            "timeout_seconds": timeout, "automatic_retry": False,
        })
        timed_out = False
        spawn_error = None
        rc = None
        with (folder / "stdout.bin").open("xb") as stdout, (folder / "stderr.bin").open("xb") as stderr:
            try:
                kwargs: dict[str, Any] = {}
                if os.name == "nt":
                    kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
                else:
                    kwargs["start_new_session"] = True
                proc = subprocess.Popen(cmd, cwd=cwd, env=env, stdout=stdout, stderr=stderr, **kwargs)
                try:
                    rc = proc.wait(timeout=timeout)
                except subprocess.TimeoutExpired:
                    timed_out = True
                    if os.name == "nt":
                        # Terminate only the tree rooted at our own subprocess.  taskkill
                        # can return before inherited log handles are released, so the
                        # direct Popen fallback below is mandatory before evidence files
                        # are read or their temporary directory is removed.
                        killer = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/taskkill.exe"
                        try:
                            subprocess.run([str(killer), "/PID", str(proc.pid), "/T", "/F"],
                                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=20,
                                           check=False)
                        except (OSError, subprocess.SubprocessError):
                            # The timed-out outcome remains authoritative.  Reap the
                            # directly-held process below even if tree cleanup failed.
                            pass
                    else:
                        os.killpg(proc.pid, signal.SIGKILL)
                    try:
                        rc = proc.wait(timeout=20)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        rc = proc.wait(timeout=20)
            except (OSError, subprocess.SubprocessError) as exc:
                spawn_error = repr(exc)
        out = (folder / "stdout.bin").read_bytes().decode("utf-8", "replace")
        err = (folder / "stderr.bin").read_bytes().decode("utf-8", "replace")
        write_json_new(folder / "result.json", {
            "exit_code": rc, "timed_out": timed_out, "spawn_error": spawn_error,
            "stdout_sha256": file_hash(folder / "stdout.bin"),
            "stderr_sha256": file_hash(folder / "stderr.bin"),
        })
        require(not timed_out, f"host command timeout: {name}; logs={folder}", "HOST_ORCHESTRATION_TIMEOUT")
        require(spawn_error is None, f"cannot start {name}: {spawn_error}; logs={folder}", "HOST_ENVIRONMENT")
        if not allow_failure:
            require(rc == 0, f"{name} exit={rc}; logs={folder}\n{(out + err)[-3500:]}", category)
        return int(rc), out, err


def select_tools(log: CommandLog) -> tuple[dict[str, Path], dict[str, str], dict]:
    require(os.name == "nt", "canonical execution requires the reviewed Windows toolchain", "HOST_ENVIRONMENT")
    clt = Path(r"E:\DevTools\STM32CubeCLT-1.22.0")
    arm = Path(r"E:\DevTools\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi\bin")
    tools = {
        "cmake": clt / "CMake/bin/cmake.exe", "ninja": clt / "Ninja/bin/ninja.exe",
        "gcc": arm / "arm-none-eabi-gcc.exe", "objcopy": arm / "arm-none-eabi-objcopy.exe",
        "nm": arm / "arm-none-eabi-nm.exe", "readelf": arm / "arm-none-eabi-readelf.exe",
    }
    for key, path in tools.items():
        require(path.is_file(), f"canonical {key} missing: {path}", "HOST_ENVIRONMENT")
    env = dict(os.environ)
    for key in ("CC", "CXX", "ASM", "CFLAGS", "CPPFLAGS", "CXXFLAGS", "LDFLAGS",
                "CMAKE_GENERATOR", "CMAKE_GENERATOR_PLATFORM", "CMAKE_GENERATOR_TOOLSET",
                "CMAKE_GENERATOR_INSTANCE", "CMAKE_TOOLCHAIN_FILE"):
        env.pop(key, None)
    env["PATH"] = str(arm) + os.pathsep + env.get("PATH", "")
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    env["PYTHONUTF8"] = "1"
    env["LC_ALL"] = "C"
    _, ver, _ = log.run([tools["gcc"], "--version"], name="arm-version", env=env, timeout=30)
    require("14.2.Rel1" in ver and "14.2.1" in ver, f"wrong canonical ARM version: {ver}", "HOST_ENVIRONMENT")
    _, machine, _ = log.run([tools["gcc"], "-dumpmachine"], name="arm-machine", env=env, timeout=30)
    require(machine.strip() == "arm-none-eabi", f"wrong target triple: {machine!r}", "HOST_ENVIRONMENT")
    _, caps, _ = log.run([tools["cmake"], "-E", "capabilities"], name="cmake-capabilities", env=env, timeout=30)
    capabilities = json.loads(caps)
    require(any(g.get("name") == "Ninja" for g in capabilities["generators"]), "CMake has no Ninja generator", "HOST_ENVIRONMENT")
    require((capabilities["version"]["major"], capabilities["version"]["minor"]) >= (3, 22), "CMake too old", "HOST_ENVIRONMENT")
    identity = {"executables": {k: {"path": str(p), "sha256": file_hash(p)} for k, p in tools.items()},
                "arm_version": ver.splitlines()[0], "cmake_version": capabilities["version"],
                "python_version": sys.version, "python_executable": sys.executable}
    return tools, env, identity


def hardware_definitions(selector: str) -> dict[str, str]:
    return {**DEFAULTS, "STREAM_LAB_R3_WORKER_CONTRACT": "ON",
            "STREAM_LAB_R3_WORKER_TASKS": "ON", "STREAM_LAB_R3_W2_HW": "ON",
            "STREAM_LAB_R3_W2_HW_CASE": selector}


def historical_profiles() -> dict[str, dict[str, str]]:
    profiles = {"default": dict(DEFAULTS)}
    for n in (3, 4, 5):
        profiles[f"w{n}"] = {**DEFAULTS, f"STREAM_LAB_R2_W{n}": "ON"}
    for k in (1, 2, 4, 8):
        for mode in ("NORMAL", "DROP"):
            profiles[f"w6_k{k}_{mode.lower()}"] = {**DEFAULTS, "STREAM_LAB_R2_W6": "ON",
                "STREAM_LAB_R2_W6_K": str(k), "STREAM_LAB_R2_W6_MODE": mode}
    profiles["w6_k8_normal_ct800"] = {**profiles["w6_k8_normal"], "STREAM_LAB_R2_CT": "ON",
                "STREAM_LAB_R2_CT_EVENTS": "800", "STREAM_LAB_R2_CT_RUN_TIMEOUT_MS": "1500"}
    require(set(profiles) == set(HISTORICAL_HASHES), "historical profile registry mismatch")
    return profiles


def negative_profiles() -> list[tuple[str, dict[str, str], str]]:
    positive = hardware_definitions("T03_A")
    return [
        ("invalid_selector", {**positive, "STREAM_LAB_R3_W2_HW_CASE": "INVALID"},
         "STREAM_LAB_R3_W2_HW_CASE must be one of T03_A, T03_C, T03_D, T05_A, T05_B."),
        ("empty_selector", {**positive, "STREAM_LAB_R3_W2_HW_CASE": ""},
         "STREAM_LAB_R3_W2_HW_CASE must be one of T03_A, T03_C, T03_D, T05_A, T05_B."),
        ("mixed_authority", {**positive, "STREAM_LAB_FOUNDATION_QUEUE_ADAPTER": "ON"},
         "STREAM_LAB_R3_W2_HW uses its test authority shim and must not link the production queue-adapter/RunAuthority profile."),
        ("historical_mix", {**positive, "STREAM_LAB_R2_W4": "ON"},
         "STREAM_LAB_R3_W2_HW must not be combined with historical R2 profiles."),
        ("selector_without_profile", {**DEFAULTS, "STREAM_LAB_R3_W2_HW_CASE": "T03_A"},
         "STREAM_LAB_R3_W2_HW_CASE requires STREAM_LAB_R3_W2_HW=ON."),
        ("missing_worker", {**positive, "STREAM_LAB_R3_WORKER_TASKS": "OFF"},
         "STREAM_LAB_R3_W2_HW requires the production R3 worker contract and worker tasks."),
    ]


def check_negative(rc: int, output: str, diagnostic: str) -> None:
    normalized = " ".join(output.split())
    require(rc != 0, "illegal configuration was accepted", "NEGATIVE_ORACLE")
    require("CMake Error" in output and diagnostic in normalized,
            "configuration failed for the wrong reason; cannot count as a negative PASS", "NEGATIVE_ORACLE")


def configure(log: CommandLog, repo: Path, build: Path, definitions: dict[str, str],
              tools: dict[str, Path], env: dict, *, expected_failure: bool = False) -> tuple[int, str, str]:
    require(not build.exists(), f"fresh build directory already exists: {build}")
    source = repo / "firmware/cubemx"
    args = [tools["cmake"], "-S", source, "-B", build, "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Debug", f"-DCMAKE_MAKE_PROGRAM={tools['ninja'].as_posix()}",
            f"-DCMAKE_TOOLCHAIN_FILE={(source / 'cmake/gcc-arm-none-eabi.cmake').as_posix()}",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
    args += [f"-D{key}={value}" for key, value in sorted(definitions.items())]
    return log.run(args, name="configure-" + build.name, env=env, timeout=180,
                   allow_failure=expected_failure)


def read_cache(path: Path) -> dict[str, str]:
    cache = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith(("#", "//")):
            continue
        match = re.fullmatch(r"([^:]+):[^=]+=(.*)", line)
        require(match is not None, f"malformed CMake cache entry: {line}")
        if match:
            cache[match[1]] = match[2]
    return cache


def verify_cache(build: Path, definitions: dict[str, str]) -> dict[str, str]:
    cache = read_cache(build / "CMakeCache.txt")
    for key, value in {**definitions, "CMAKE_BUILD_TYPE": "Debug", "CMAKE_GENERATOR": "Ninja"}.items():
        require(cache.get(key) == value, f"cache differs from requested profile: {key}={cache.get(key)!r}, expected={value!r}", "BUILD_IDENTITY")
    return cache


def flag_present(command: str, flag: str) -> bool:
    # These audited flags do not contain whitespace. Support CMake quoting.
    return re.search(r'(?<!\S)["\']?' + re.escape(flag) + r'["\']?(?=\s|$)', command) is not None


def check_compile_database(build: Path, repo: Path, *, selector: str | None,
                           canonical_gcc: Path | None = None) -> dict:
    entries = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    require(isinstance(entries, list) and bool(entries), "empty compilation database", "BUILD_IDENTITY")
    selected: dict[str, list[dict]] = {}
    names = []
    for entry in entries:
        source = Path(entry["file"])
        if not source.is_absolute():
            source = Path(entry["directory"]) / source
        source = source.resolve()
        require(source.is_file(), f"compile source missing: {source}", "BUILD_IDENTITY")
        name = source.name
        names.append(name)
        selected.setdefault(name, []).append(entry)
        if selector is None:
            require(name not in RUNTIME_SOURCES and not name.startswith("r3_w2_hw"),
                    f"historical build compiles W2-HW/R3 worker source: {source}", "R2_NON_REGRESSION")
    if selector is None:
        return {"translation_units": len(entries), "w2_hw_intrusion": 0}
    require(selector in CASES, f"unknown selector {selector}")
    for forbidden in ("stream_run_authority.c", "stream_queue_adapter.c", "adc_dbm_driver.c",
                      "r1_acquisition.c", "r1_bringup.c", "r2_w3_rebind.c", "r2_w4_roundtrip.c",
                      "r2_w5_capacity.c", "r2_w6_matrix.c"):
        require(forbidden not in names, f"unapproved source in W2-HW link input: {forbidden}", "LINK_AUTHORITY")
    for required in (*RUNTIME_SOURCES, "tasks.c", "queue.c", "list.c", "port.c"):
        require(len(selected.get(required, [])) == 1, f"compile source must occur exactly once: {required}", "BUILD_IDENTITY")
    proof = {}
    for name in RUNTIME_SOURCES:
        entry = selected[name][0]
        src = Path(entry["file"])
        if not src.is_absolute():
            src = Path(entry["directory"]) / src
        require(src.resolve() == (repo / "firmware/runtime" / name).resolve(),
                f"wrong runtime source path: {src}", "BUILD_IDENTITY")
        command = entry.get("command", "")
        require(isinstance(command, str) and bool(command), "missing exact compiler command", "BUILD_IDENTITY")
        for flag in ("-Wall", "-Wextra", "-Werror", "-mcpu=cortex-m4", "-mthumb",
                     "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard", "-DSTREAM_LAB_R3_W2_HW=1",
                     f"-DR3_W2_HW_CASE_ID={CASES[selector]}U"):
            require(flag_present(command, flag), f"{name}: required compile flag absent: {flag}", "BUILD_IDENTITY")
        require("R3_WORKER_TASKS_TESTING" not in command, "test-stub define leaked into ARM profile", "LINK_AUTHORITY")
        if canonical_gcc is not None:
            # CMake emits a quoted or bare compiler path as the first token.
            token = re.match(r'^\s*(?:"([^"]+)"|(\S+))', command)
            require(token is not None, "cannot identify compiler", "BUILD_IDENTITY")
            compiler = token.group(1) or token.group(2)
            require(compiler.replace("\\", "/").lower() == canonical_gcc.as_posix().lower(),
                    f"compiler not canonical: {compiler}", "BUILD_IDENTITY")
        proof[name] = {"command": command, "source_sha256": file_hash(src)}
    return {"translation_units": len(entries), "selector": selector,
            "case_id": CASES[selector], "runtime_commands": proof,
            "production_authority_translation_units": 0}


def parse_nm(text: str) -> dict[str, list[tuple[str, int]]]:
    result: dict[str, list[tuple[str, int]]] = {}
    for row in text.splitlines():
        parts = row.split()
        require(len(parts) in (3, 4) and len(parts[1]) == 1 and
                re.fullmatch(r"[0-9a-fA-F]+", parts[2]) is not None,
                f"unexpected POSIX nm row: {row!r}", "LINK_IDENTITY")
        result.setdefault(parts[0], []).append((parts[1], int(parts[2], 16)))
    return result


def check_symbols(symbols: dict, *, object_shim: bool = False) -> None:
    for name in SHIM_SYMBOLS if object_shim else REQUIRED_TEXT_SYMBOLS:
        values = symbols.get(name, [])
        require(len(values) == 1 and values[0][0] == "T",
                f"required strong text symbol absent/weak/duplicate: {name}={values}", "LINK_AUTHORITY")
    if not object_shim:
        values = symbols.get("g_r3_w2_hw_result", [])
        require(len(values) == 1 and values[0][0] in ("B", "D"),
                "result record must have one strong data definition", "LINK_IDENTITY")


def check_vector(data: bytes, irq_number: int, handler_value: int) -> None:
    offset = (16 + irq_number) * 4
    require(0 <= irq_number and len(data) >= offset + 4, "IRQ vector data truncated", "LINK_IDENTITY")
    value = struct.unpack_from("<I", data, offset)[0]
    require(value == (handler_value | 1), "TIM6_DAC vector does not point to the strong Thumb handler", "LINK_IDENTITY")


def audit_elf(log: CommandLog, repo: Path, build: Path, tools: dict, env: dict) -> dict:
    elf = build / "cubemx.elf"
    require(elf.is_file(), f"ELF not produced: {elf}", "BUILD")
    _, header, _ = log.run([tools["readelf"], "-h", elf], name="elf-header-" + build.name, env=env)
    require(re.search(r"Class:\s+ELF32", header) is not None and
            re.search(r"Machine:\s+ARM(?:\s|$)", header) is not None,
            "artifact is not a 32-bit ARM ELF", "LINK_IDENTITY")
    _, listing, _ = log.run([tools["nm"], "-P", "--defined-only", "--extern-only", elf],
                            name="elf-nm-" + build.name, env=env)
    symbols = parse_nm(listing)
    check_symbols(symbols)
    objects = [p for p in build.rglob("*") if p.is_file() and
               p.name in ("r3_w2_hw_authority_shim.c.obj", "r3_w2_hw_authority_shim.c.o")]
    require(len(objects) == 1, "expected one shim object provider", "LINK_AUTHORITY")
    _, obj_symbols, _ = log.run([tools["nm"], "-P", "--defined-only", "--extern-only", objects[0]],
                                name="shim-provider-" + build.name, env=env)
    check_symbols(parse_nm(obj_symbols), object_shim=True)
    header_path = repo / "firmware/cubemx/Drivers/CMSIS/Device/ST/STM32F4xx/Include/stm32f446xx.h"
    irq_matches = re.findall(r"\bTIM6_DAC_IRQn\s*=\s*(\d+)", header_path.read_text(encoding="utf-8"))
    require(len(irq_matches) == 1, "cannot identify the pinned device IRQ number", "LINK_IDENTITY")
    vector = build / "isr_vector.bin"
    log.run([tools["objcopy"], "-O", "binary", "--only-section=.isr_vector", elf, vector],
            name="vector-" + build.name, env=env)
    check_vector(vector.read_bytes(), int(irq_matches[0]), symbols["TIM6_DAC_IRQHandler"][0][1])
    binary = build / "programmed.bin"
    log.run([tools["objcopy"], "-O", "binary", elf, binary], name="binary-" + build.name, env=env)
    return {"elf_sha256": file_hash(elf), "programmed_sha256": file_hash(binary),
            "strong_symbols": {s: symbols[s] for s in (*REQUIRED_TEXT_SYMBOLS, "g_r3_w2_hw_result")},
            "shim_provider": str(objects[0]), "shim_object_sha256": file_hash(objects[0]),
            "vector_to_strong_handler": "PASS", "irq_number": int(irq_matches[0]),
            "runtime_executed": False}


def run_host_tests(log: CommandLog, repo: Path, output: Path) -> dict:
    # unittest's default stderr is captured by Python, never interpreted by PowerShell.
    code = (
        "import sys,unittest,json; from pathlib import Path;"
        "repo=Path(sys.argv[1]); out=Path(sys.argv[2]);"
        "sys.path.insert(0,str(repo/'tools/r3'));"
        "suite=unittest.defaultTestLoader.discover(str(repo/'tests/r3_host'),pattern='test_*.py');"
        "count=suite.countTestCases();"
        "r=unittest.TextTestRunner(stream=sys.stdout,verbosity=1).run(suite);"
        "data={'discovered':count,'run':r.testsRun,'failures':len(r.failures),'errors':len(r.errors),"
        "'skipped':len(r.skipped),'expected_failures':len(r.expectedFailures),"
        "'unexpected_successes':len(r.unexpectedSuccesses),'success':r.wasSuccessful()};"
        "out.write_text(json.dumps(data,indent=2)+'\\n',encoding='utf-8');"
        "sys.exit(0 if r.wasSuccessful() and r.testsRun==count and count>0 and not r.skipped "
        "and not r.expectedFailures else 2)"
    )
    env = {**os.environ, "PYTHONDONTWRITEBYTECODE": "1", "PYTHONUTF8": "1"}
    result_path = output / "host_suite.json"
    log.run([sys.executable, "-B", "-c", code, repo, result_path], name="host-suite", env=env,
            timeout=240, category="HOST_TEST")
    data = json.loads(result_path.read_text(encoding="utf-8"))
    require(data["discovered"] >= 25 and data["run"] == data["discovered"] and data["success"]
            and not any(data[k] for k in ("failures", "errors", "skipped", "expected_failures", "unexpected_successes")),
            "host suite result incomplete", "HOST_TEST")
    log.run([sys.executable, "-B", repo / "tools/r3/r3_harness.py", "--repo", repo, "selftest"],
            name="harness-selftest", env=env, timeout=120, category="HOST_TEST")
    return data


def create_attempt(parent: Path, repo: Path) -> Path:
    parent = parent.expanduser().resolve()
    require(not parent.is_relative_to(repo.resolve()), "validation output must be outside repository")
    parent.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ-")
    return Path(tempfile.mkdtemp(prefix="run-" + stamp, dir=parent))


def manifest_attempt(root: Path) -> None:
    rows = []
    for path in sorted(root.rglob("*")):
        if path.is_file() and path.name != "MANIFEST.sha256":
            rows.append(f"{file_hash(path)}  {path.relative_to(root).as_posix()}\n")
    with (root / "MANIFEST.sha256").open("x", encoding="utf-8", newline="\n") as f:
        f.writelines(rows)


def validate(repo: Path, *, candidate: bool, output_parent: Path, phase: str = "all") -> int:
    """No arbitrary dirty acceptance, no staging/committing/flashing, no auto retry."""
    require(candidate, "explicit --candidate required; no formal H1/hardware authority granted")
    require(phase in ("all", "host", "arm", "negative", "history"), "unknown phase")
    repo = repo.resolve()
    root = create_attempt(output_parent, repo)
    log = CommandLog(root)
    report: dict[str, Any] = {"schema": "r3-w2-build-validation-v1", "requested_phase": phase,
        "kind": "CANDIDATE_BUILD_REVIEW", "result": "NOT_RUN", "gates": {},
        "hardware": "NONE", "formal_hardware_acceptance": False, "automatic_retry": False,
        "repository_write_requested": False, "attempt_root": str(root)}
    before = None
    try:
        before = snapshot(repo)
        report["identity"] = {k: v for k, v in before.items() if k != "file_sha256"}
        write_json_new(root / "source_before.json", before)
        write_json_new(root / "tooling_identity.json", {
            "files": {p: before["file_sha256"][p] for p in before["file_sha256"]
                      if p.startswith(("tools/r3/", "tests/r3_host/"))},
            "committed": False, "source_commit": before["head"],
        })
        for rel in (*CANDIDATE_SHA256, *HOST_PATHS):
            p = root / "review_source" / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            with p.open("xb") as f:
                f.write((repo / rel).read_bytes())
        with (root / "tracked.diff").open("xb") as f:
            f.write(git_read(repo, "diff", "--no-ext-diff", "--binary", "HEAD"))
        git_read(repo, "diff", "--no-ext-diff", "--check")
        # git diff does not include untracked additions; inspect those too.
        for rel, xy in before["status"].items():
            if xy == "??":
                for number, line in enumerate((repo / rel).read_bytes().splitlines(), 1):
                    require(not line.endswith((b" ", b"\t")),
                            f"new-file trailing whitespace: {rel}:{number}", "DIFF_HYGIENE")
        report["gates"]["input_identity"] = {"result": "PASS", "candidate_files": 6, "host_files": 5}
        print("INPUT_IDENTITY: PASS (6 candidate + 5 host/docs)", flush=True)
        if phase in ("all", "host"):
            report["gates"]["host"] = run_host_tests(log, repo, root)
            print(f"HOST_SUITE: {report['gates']['host']['run']} / {report['gates']['host']['discovered']} PASS", flush=True)
        if phase != "host":
            tools, env, tools_identity = select_tools(log)
            write_json_new(root / "toolchain.json", tools_identity)
            print("CANONICAL_TOOLCHAIN: PASS", flush=True)
            # Environment-positive build precedes every expected-failure configure test.
            selectors = list(CASES) if phase in ("all", "arm") else ["T03_A"] if phase == "negative" else []
            positive = {}
            report["gates"]["arm"] = positive
            for selector in selectors:
                build = root / "build" / selector
                definitions = hardware_definitions(selector)
                configure(log, repo, build, definitions, tools, env)
                verify_cache(build, definitions)
                commands = check_compile_database(build, repo, selector=selector, canonical_gcc=tools["gcc"])
                log.run([tools["cmake"], "--build", build, "--parallel", "4", "--verbose"],
                        name="build-" + selector, env=env, timeout=600)
                link = audit_elf(log, repo, build, tools, env)
                positive[selector] = {"result": "PASS", "compile_audit": commands, "link_audit": link}
                write_json_new(root / "audits" / (selector + ".json"), positive[selector])
                print(f"ARM_LINK {selector}: PASS", flush=True)
            if positive:
                report["gates"]["arm"] = positive
            if phase in ("all", "negative"):
                negatives = {}
                report["gates"]["negative"] = negatives
                for name, definitions, diagnostic in negative_profiles():
                    build = root / "negative" / name
                    rc, out, err = configure(log, repo, build, definitions, tools, env, expected_failure=True)
                    check_negative(rc, out + "\n" + err, diagnostic)
                    require(not (build / "cubemx.elf").exists(), "negative config unexpectedly emitted an ELF", "NEGATIVE_ORACLE")
                    negatives[name] = {"result": "PASS", "exit_code": rc, "expected_rejection": diagnostic}
                    print(f"NEGATIVE {name}: PASS (expected rejection)", flush=True)
                report["gates"]["negative"] = negatives
            if phase in ("all", "history"):
                rows = {}
                report["gates"]["historical"] = rows
                for name, definitions in historical_profiles().items():
                    build = root / "historical" / name
                    configure(log, repo, build, definitions, tools, env)
                    verify_cache(build, definitions)
                    source_audit = check_compile_database(build, repo, selector=None)
                    log.run([tools["cmake"], "--build", build, "--parallel", "4", "--verbose"],
                            name="history-build-" + name, env=env, timeout=600)
                    elf, binary = build / "cubemx.elf", build / "programmed.bin"
                    require(elf.is_file(), f"missing historical ELF: {name}", "R2_NON_REGRESSION")
                    log.run([tools["objcopy"], "-O", "binary", elf, binary], name="history-binary-" + name, env=env)
                    actual = file_hash(binary)
                    require(actual == HISTORICAL_HASHES[name],
                            f"{name}: programmed bytes differ; expected={HISTORICAL_HASHES[name]}, actual={actual}", "R2_NON_REGRESSION")
                    rows[name] = {"result": "PASS", "programmed_sha256": actual,
                                  "elf_sha256": file_hash(elf), "compile_audit": source_audit}
                    write_json_new(root / "audits" / ("historical-" + name + ".json"), rows[name])
                    print(f"HISTORICAL {name}: PASS {actual}", flush=True)
                report["gates"]["historical"] = rows
        after = snapshot(repo)
        validate_snapshot_unchanged(before, after)
        write_json_new(root / "source_after.json", after)
        report["result"] = "PASS" if phase == "all" else "SELECTED_PHASE_PASSED"
        report["repository_preserved"] = True
        report["next_allowed"] = "PRINCIPAL_IMPLEMENTATION_REVIEW" if phase == "all" else "COMPLETE_REMAINING_BUILD_VALIDATION"
    except Exception as exc:
        report["result"] = "FAIL"
        report["first_failure"] = str(exc)
        report["failure_class"] = getattr(exc, "category", "HOST_TOOL")
        report["next_allowed"] = "RETURN_REPORT_PRESERVE_STATE"
        if before is not None:
            try:
                after = snapshot(repo, enforce=False)
                write_json_new(root / "source_after_failure.json", after)
                report["repository_preserved"] = before == after
            except Exception as state_exc:
                report["repository_preserved"] = "UNKNOWN"
                report["post_failure_state_error"] = str(state_exc)
    write_json_new(root / "report.json", report)
    # The review zip is small: reports + exact source + command logs, not object caches.
    import zipfile
    with zipfile.ZipFile(root / "review_bundle.zip", "x", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(root.rglob("*")):
            rel = path.relative_to(root)
            if path.is_file() and rel.parts[0] not in ("build", "historical", "negative") and path.name != "review_bundle.zip":
                archive.write(path, rel.as_posix())
    manifest_attempt(root)
    print("\n=== R3 RETURN PACKET ===", flush=True)
    print("STEP: R3-W2-HW-BUILD-LINK-VALIDATION")
    print("RESULT: " + report["result"])
    print("PHASE: " + phase)
    if "first_failure" in report:
        print("FAIL_CLASS: " + report["failure_class"])
        print("FIRST_FAILURE: " + report["first_failure"].replace("\n", " | "))
    print("REPOSITORY_PRESERVED: " + str(report.get("repository_preserved", "NOT_YET_VERIFIED")))
    print("STAGE_COMMIT_PUSH: NOT_PERFORMED")
    print("HARDWARE: NONE")
    print("FORMAL_HARDWARE_ACCEPTANCE: NOT_CLAIMED")
    for group in ("arm", "negative", "historical"):
        rows = report["gates"].get(group)
        print(f"{group.upper()}: {str(len(rows)) + ' passed' if rows else 'NOT_COMPLETED'}")
    print("REPORT: " + str(root / "report.json"))
    print("REPORT_SHA256: " + file_hash(root / "report.json"))
    print("REVIEW_BUNDLE: " + str(root / "review_bundle.zip"))
    print("REVIEW_BUNDLE_SHA256: " + file_hash(root / "review_bundle.zip"))
    print("NEXT_ALLOWED: " + report["next_allowed"])
    print("=== END R3 RETURN PACKET ===", flush=True)
    return 2 if report["result"] == "FAIL" else 0
