# STM32 Stream Lab — Bring-up Log

## Session: 2026-09-10 → 2026-09-11

### 1. Baseline

- Architecture baseline: `Architecture v3.2.2`
- Target platform: `NUCLEO-F446RE / STM32F446RE`
- RTOS baseline: FreeRTOS
- Project repository: `E:\Projects\stm32-stream-lab`
- Current phase: PC development environment bring-up
- Hardware status: NUCLEO-F446RE not yet purchased; no board-level tests have been performed.

## 2. Host Environment

- OS: Windows 11
- Shell used for bring-up: Windows PowerShell
- PowerShell version: `5.1.26100.8115`
- Development environment intentionally kept on native Windows.
- WSL, VM, and Docker are not currently part of the development path.
- Existing global Python installations were not removed or modified.

## 3. Git Repository

Local repository:

`E:\Projects\stm32-stream-lab`

Branch:

`main`

Git:

`2.54.0.windows.1`

Repository-specific author configuration:

`Huaijin Chen <186883426+nkpkm2@users.noreply.github.com>`

GitHub:

- Account: `nkpkm2`
- Repository: `stm32-stream-lab`
- Visibility: Public
- Remote name: `origin`
- Remote: `https://github.com/nkpkm2/stm32-stream-lab.git`

Initial commit:

`39a3978946b6a0f3cd713dd10e369201f2780b68`

Commit message:

`chore: initialize project environment`

The local `main` branch is tracking `origin/main`.

Initial tracked files:

- `.gitattributes`
- `.gitignore`
- `pyproject.toml`
- `uv.lock`

The project virtual environment `.venv/` is ignored by Git.

Repository line endings are explicitly normalized. Relevant repository metadata, source/configuration files, and `uv.lock` use LF.

## 4. Python Environment

Python/project manager:

`uv 0.12.12`

uv executable:

`C:\Users\CHJ\.local\bin\uv.exe`

Managed Python storage:

`E:\DevTools\uv\python`

uv cache:

`E:\DevTools\uv\cache`

Project virtual environment:

`E:\Projects\stm32-stream-lab\.venv`

Project Python:

`CPython 3.12.13, 64-bit`

Verified executable:

`E:\Projects\stm32-stream-lab\.venv\Scripts\python.exe`

Verified:

- `sys.prefix != sys.base_prefix`: `True`
- `include-system-site-packages = false`

`pyproject.toml` currently fixes:

- Python: `==3.12.13`
- uv: `==0.12.12`

`uv.lock` was generated successfully.

Current Python dependency list is still empty. NumPy, SciPy, pyserial, pytest, plotting libraries, etc. have not yet been installed or validated.

## 5. Native Build Tools

### CMake

Version:

`3.31.12`

Location:

`E:\DevTools\cmake-3.31.12-windows-x86_64`

Official download SHA-256 verification:

PASS

Executable launch:

PASS

### Ninja

Version:

`1.13.1`

Location:

`E:\DevTools\ninja-1.13.1-windows-x86_64`

Official download SHA-256 verification:

PASS

Executable launch:

PASS

## 6. ARM Cross Toolchain

Toolchain:

`Arm GNU Toolchain 14.2.Rel1`

Compiler version:

`arm-none-eabi-gcc 14.2.1 20241119`

Build identifier:

`Arm GNU Toolchain 14.2.Rel1 (Build arm-14.52)`

Location:

`E:\DevTools\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi`

Compiler target:

`arm-none-eabi`

Official SHA-256:

`f074615953f76036e9a51b87f6577fdb4ed8e77d3322a6f68214e92e7859888f`

Verification:

PASS

### Download incident

The first download using Windows PowerShell `Invoke-WebRequest` terminated with an unexpected EOF.

A partial ZIP remained.

Partial ZIP size:

`109461504 bytes`

Partial ZIP SHA-256:

`8BE0D497E46E3000B25D95271A353545E63F9434277B679832BA7E9E2EDD4F4D`

This did not match the official checksum. The corrupted ZIP was deleted and was never used as a trusted toolchain package.

The package was then downloaded successfully using `curl.exe`, and its checksum matched the official SHA-256.

### Extraction incident

The first valid extraction was accidentally performed directly into:

`E:\DevTools`

This created duplicate toolchain directories:

- `arm-none-eabi`
- `bin`
- `include`
- `lib`
- `libexec`
- `share`

The package was subsequently extracted into the correct version-specific directory.

Before cleanup, each duplicate directory was compared against the version-specific copy by relative file path and file size:

- `arm-none-eabi`: 3041 / 3041 files, 0 differences
- `bin`: 31 / 31 files, 0 differences
- `include`: 1 / 1 file, 0 differences
- `lib`: 2734 / 2734 files, 0 differences
- `libexec`: 13 / 13 files, 0 differences
- `share`: 2510 / 2510 files, 0 differences

The duplicate root directories were removed.

The compiler in the version-specific directory was tested again after cleanup and remained operational.

## 7. CMake + Ninja + ARM GCC Smoke Test

Test directory:

`E:\DevTools\SmokeTests\arm-cmake-ninja`

Purpose:

Verify that CMake, Ninja, and ARM GCC can actually work together rather than only verifying their individual version commands.

Result:

PASS

CMake detected:

`GNU 14.2.1`

A C11 source file was successfully compiled with:

- `-mcpu=cortex-m4`
- `-mthumb`
- `-mfpu=fpv4-sp-d16`
- `-mfloat-abi=hard`
- `-Wall`
- `-Wextra`
- `-Werror`

Ninja successfully produced:

`libarm_smoke.a`

The generated object file was inspected using `arm-none-eabi-readelf`.

Verified ARM attributes:

- `Tag_CPU_name: "7E-M"`
- `Tag_CPU_arch: v7E-M`
- `Tag_CPU_arch_profile: Microcontroller`
- `Tag_FP_arch: VFPv4-D16`
- `Tag_ABI_VFP_args: VFP registers`

This proves that the installed CMake, Ninja, and ARM GCC can jointly produce Cortex-M4F-compatible ARM object code.

It does not prove that the STM32 firmware currently compiles, links, flashes, boots, or passes any R0-R7 board-level risk gate.

## 8. STM32CubeMX

Installed version according to STM32CubeMX Help → About:

`6.18.1`

Installation directory:

`E:\DevTools\STM32CubeMX-6.18.1`

Executable:

`E:\DevTools\STM32CubeMX-6.18.1\STM32CubeMX.exe`

Observed Windows executable metadata:

- FileVersion: `6.18.1-RC2`
- ProductVersion: `6.18.1-RC2`

STM32CubeMX itself reports:

`Version 6.18.1`

This metadata discrepancy is recorded but currently considered non-blocking.

## 9. STM32Cube Firmware Repository

Configured CubeMX firmware repository:

`E:\DevTools\STM32Cube\Repository`

Installed STM32F4 package:

`STM32Cube_FW_F4_V1.28.3`

Verified on disk at:

`E:\DevTools\STM32Cube\Repository\STM32Cube_FW_F4_V1.28.3`

Observed top-level contents:

- `Documentation`
- `Drivers`
- `Middlewares`
- `Projects`
- `Utilities`
- `_htmresc`
- `package.xml`
- `Package_license.html`
- `Package_license.md`
- `Release_Notes.html`
- `sbom_cdx.json`

CubeMX package manager shows the package as installed.

No CubeMX STM32F446 project has yet been created.

## 10. Current Evidence Boundary

Confirmed:

- Windows-native development path established.
- Local and public Git repositories established.
- Git identity and remote tracking established.
- Project-specific Python environment established.
- Python version and uv version locked.
- CMake installed and executable.
- Ninja installed and executable.
- ARM GNU cross compiler installed, checksum verified, executable, and targeting `arm-none-eabi`.
- CMake + Ninja + ARM GCC successfully completed a Cortex-M4F cross-compilation smoke test.
- STM32CubeMX installed and launches as version 6.18.1.
- STM32CubeF4 V1.28.3 installed in the intended E: firmware repository.

Not yet confirmed:

- STM32CubeProgrammer installation.
- ST-LINK/USB driver behavior.
- Physical NUCLEO-F446RE detection.
- Actual STM32F446RE firmware compilation and linking.
- Startup code and linker script.
- CubeMX-generated project configuration.
- FreeRTOS integration.
- CMSIS-DSP integration.
- Flashing.
- Target boot.
- UART/VCP communication.
- ADC, timers, DMA, DBM, DAC, watchdog, or any hardware peripheral behavior.
- Any R0-R7 risk gate.
- Any T01-T20 board-level test.
- Any timing or performance budget.

No board-level claim is currently justified.

## 11. Current Failure Point

None.

Environment bring-up is paused voluntarily after successful installation and verification of STM32CubeF4 V1.28.3.

## 12. Next Session — Single Next Action

Continue environment and STM32 bring-up from this recorded state.

Do not repeat already verified installations unless new evidence indicates a problem.