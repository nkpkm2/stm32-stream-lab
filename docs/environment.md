# Development Environment

## Scope

This document records the computer-side development environment used by the `stm32-stream-lab` project.

It covers host tooling and reproducible smoke checks only. It does **not** claim successful ST-LINK connection, flashing, on-board debugging, VCP communication, peripheral behavior, or board-level timing validation.

Verified on: 2026-09-11
Host OS: Windows 11
Primary shell: Windows PowerShell 5.1

## Path conventions

Repository documentation and scripts do not depend on one user's absolute filesystem layout.

- `<repo-root>`: local clone of this repository
- `<tools-root>`: local directory containing standalone development tools
- `<user-profile>`: current Windows user profile

Machine-specific paths are supplied at runtime and are not committed to the repository.

## Version-locked host tools

| Component | Verified version | Role |
|---|---:|---|
| Git | 2.54.0.windows.1 | Source control |
| uv | 0.12.12 | Python/project environment management |
| CPython | 3.12.13, 64-bit | Host automation and analysis |
| CMake | 3.31.12 | Build configuration |
| Ninja | 1.13.1 | Build execution |
| Arm GNU Toolchain | 14.2.Rel1 | ARM cross compilation |
| `arm-none-eabi-gcc` | 14.2.1 20241119 | C compiler |
| `arm-none-eabi-g++` | 14.2.1 20241119 | C++ compiler |
| `arm-none-eabi-gdb` | 15.2.90.20241130-git | Target debugger client |
| STM32CubeMX | 6.18.1 | STM32 configuration/code-generation tool |
| STM32CubeF4 | V1.28.3 | STM32F4 HAL/CMSIS firmware package |
| STM32CubeProgrammer | 2.23.0 | STM32 programming CLI/GUI |

The Arm compiler target was verified as:

```text
arm-none-eabi
```

STM32CubeMX `Help -> About` reports version `6.18.1`. Its Windows executable metadata and CLI startup log contain the internal identifier `6.18.1-RC2`; this discrepancy is recorded but did not block the verified command-line smoke check.

The CubeMX bundled Java runtime reported Java `21.0.10` during the command-line smoke check.

## Python environment

The project uses a repository-local virtual environment:

```text
<repo-root>\.venv
```

The repository declares:

```toml
requires-python = "==3.12.13"
```

and locks the project environment with:

```text
uv.lock
```

The current project dependency list is intentionally empty at this stage.

The environment smoke check verifies that:

- the project interpreter is Python 3.12.13;
- the interpreter is running inside a virtual environment;
- `uv sync --locked --offline` succeeds against the existing local cache.

The `.venv/` directory is ignored by Git and must not be committed.

## STM32 firmware package

STM32CubeF4 `V1.28.3` is installed under the local STM32Cube firmware repository beneath `<tools-root>`.

The firmware package is used as the fixed STM32F4 HAL/CMSIS source baseline for this stage. A later change of firmware-package version must be explicit and reviewed rather than silently following the newest available package.

## Repository-local smoke check

The reusable smoke check is:

```text
tools/environment-smoke.ps1
```

Run it from `<repo-root>`:

```powershell
& '.\tools\environment-smoke.ps1' `
    -ToolsRoot '<tools-root>' `
    -UvExe '<user-profile>\.local\bin\uv.exe'
```

`-UvExe` may be omitted when `uv.exe` is already discoverable through `PATH`.

The script verifies:

1. Git can be invoked.
2. `uv` and the repository-local Python environment are valid.
3. `uv sync --locked --offline` succeeds.
4. CMake and Ninja can be invoked.
5. ARM GCC, G++, GDB, and readelf can be invoked.
6. The ARM compiler target is `arm-none-eabi`.
7. STM32CubeMX starts in quiet command-line mode and exits successfully.
8. STM32CubeProgrammer CLI starts and reports version 2.23.0.
9. CMake + Ninja + ARM GCC perform a real Cortex-M4F cross-compilation.
10. The resulting object reports:
    - `Tag_CPU_arch: v7E-M`
    - `Tag_FP_arch: VFPv4-D16`
    - `Tag_ABI_VFP_args: VFP registers`
11. `.venv/` and `build/` are ignored by Git.
12. No files under `.venv/` or `build/` are tracked.
13. Tracked text contains no Windows absolute-path candidates.

Temporary smoke-test files are written below:

```text
<repo-root>\build\environment-smoke
```

and are ignored by Git.

## Verified smoke result — 2026-09-11

The repository-local smoke check completed successfully with:

```text
STM32CubeMX CLI: PASS
STM32CubeProgrammer CLI: PASS
ARM object attributes: PASS
.venv ignore: PASS
build ignore: PASS
tracked generated directories: PASS
tracked Windows absolute paths: PASS

=== ENVIRONMENT SMOKE RESULT: PASS ===
```

The cross-compilation used:

```text
-mcpu=cortex-m4
-mthumb
-mfpu=fpv4-sp-d16
-mfloat-abi=hard
-Wall
-Wextra
-Werror
```

and produced a Cortex-M4F-compatible ARM object through CMake + Ninja + Arm GNU Toolchain.

## Known non-blocking observations

During CubeMX command-line startup, the bundled Java runtime emitted a Windows preferences warning indicating that it could not create the global `Software\JavaSoft\Prefs` registry node due to access-denied error code 5.

CubeMX nevertheless completed the command-line `help` / `exit` smoke check with process exit code `0`. No administrator privilege or registry-permission change was required.

## Evidence boundary

This environment check establishes only the computer-side toolchain and repository workflow.

It does **not** establish:

- ST-LINK detection;
- target-board USB connectivity;
- flashing;
- on-board GDB debugging;
- VCP/UART communication;
- target boot;
- STM32 clock correctness on real hardware;
- ADC, TIM, DMA, DBM, DAC, watchdog, or RTOS behavior;
- any board-level timing or performance budget;
- any R0-R7 board-level validation gate.

Those require the physical NUCLEO-F446RE and appropriate data cable and will be verified separately.
