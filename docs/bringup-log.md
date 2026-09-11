# STM32 Stream Lab — Bring-up Log

## Session: 2026-09-10 → 2026-09-11

### 1. Baseline

- Architecture baseline: `Architecture v3.2.2`
- Target platform: `NUCLEO-F446RE / STM32F446RE`
- RTOS baseline: FreeRTOS
- Project repository: `<repo-root>`
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

`<repo-root>`

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

`<user-profile>\.local\bin\uv.exe`

Managed Python storage:

`<tools-root>\uv\python`

uv cache:

`<tools-root>\uv\cache`

Project virtual environment:

`<repo-root>\.venv`

Project Python:

`CPython 3.12.13, 64-bit`

Verified executable:

`<repo-root>\.venv\Scripts\python.exe`

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

`<tools-root>\cmake-3.31.12-windows-x86_64`

Official download SHA-256 verification:

PASS

Executable launch:

PASS

### Ninja

Version:

`1.13.1`

Location:

`<tools-root>\ninja-1.13.1-windows-x86_64`

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

`<tools-root>\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi`

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

`<tools-root>`

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

`<tools-root>\SmokeTests\arm-cmake-ninja`

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

`<tools-root>\STM32CubeMX-6.18.1`

Executable:

`<tools-root>\STM32CubeMX-6.18.1\STM32CubeMX.exe`

Observed Windows executable metadata:

- FileVersion: `6.18.1-RC2`
- ProductVersion: `6.18.1-RC2`

STM32CubeMX itself reports:

`Version 6.18.1`

This metadata discrepancy is recorded but currently considered non-blocking.

## 9. STM32Cube Firmware Repository

Configured CubeMX firmware repository:

`<tools-root>\STM32Cube\Repository`

Installed STM32F4 package:

`STM32Cube_FW_F4_V1.28.3`

Verified on disk at:

`<tools-root>\STM32Cube\Repository\STM32Cube_FW_F4_V1.28.3`

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

## Session: 2026-09-11 — Operations Work Package P0-A1

### 1. Session scope

The physical NUCLEO-F446RE board and USB data cable were not available during this session.

Therefore, the session was intentionally restricted to computer-side work that could produce real, reproducible evidence without pretending to perform hardware validation.

Explicitly excluded from this session:

- ST-LINK connection
- flashing
- on-board debugging
- VCP/UART validation
- ADC validation
- TIM validation
- DMA/DBM validation
- board-level timing measurements
- R0-R7 hardware validation

### 2. Starting repository state

The session resumed from a clean synchronized repository state after the previous day's project-journal commit.

The initial CubeMX project shell was created for:

- Board: `NUCLEO-F446RE`
- MCU: `STM32F446RETx`
- Package: `LQFP64`
- Firmware package: `STM32Cube FW_F4 V1.28.3`
- Toolchain target: `CMake`
- Compiler family: `GCC`

The initial CubeMX configuration was committed and pushed as:

```text
66800bf  chore: add initial NUCLEO-F446RE CubeMX configuration
```

### 3. Limited static clock configuration work

A limited software-only clock configuration was performed before the session scope was narrowed to P0-A1.

The CubeMX configuration was adjusted toward the approved static target:

```text
HSE input               8 MHz, bypass source
PLL source              HSE
PLLM                    4
PLLN                    180
PLLP                    /2
SYSCLK                  180 MHz
HCLK                    180 MHz
APB1                    45 MHz
APB1 timer clock        90 MHz
APB2                    90 MHz
APB2 timer clock        180 MHz
Voltage Scale           Scale 1
Power Over-Drive        Enabled
Flash latency           5 WS
TIM prescaler selection Disabled
```

This configuration remains a **software-side static configuration only**.

It is not treated as proof that the real board runs correctly at 180 MHz. Actual HSE routing, board revision, physical clock behavior, supply assumptions, and startup behavior remain pending board-level validation.

The resulting modification to:

```text
firmware/cubemx/cubemx.ioc
```

was intentionally kept outside the P0-A1 environment commit and remained as a separate unstaged working-tree change at P0-A1 closure.

### 4. P0-A1 objective

Operations Work Package P0-A1 required the computer-side development environment to be reproducible and inspectable from the existing repository.

Required work included:

1. verify existing host tools;
2. install only missing tools needed at this stage;
3. verify the intended command-line workflow;
4. establish repository-local Python environment/dependency definitions;
5. provide a computer-side reproducible smoke check;
6. create `docs/environment.md`;
7. verify repository hygiene;
8. issue a PASS / PARTIAL / FAIL handoff.

### 5. Repository audit and documentation hygiene

The repository was inspected before further modification.

Tracked files were small and no large generated artifacts were present.

Existing project configuration included:

- `.gitignore`
- `.gitattributes`
- `pyproject.toml`
- `uv.lock`
- `docs/bringup-log.md`
- the initial CubeMX `.ioc`
- the previous project journal

The existing bring-up log still contained machine-specific absolute Windows paths.

Those paths were normalized to portable documentation placeholders:

```text
<repo-root>
<tools-root>
<user-profile>
```

A search of tracked text subsequently found no remaining Windows absolute-path candidates.

### 6. Tool verification

Previously installed tools were re-used rather than reinstalled.

Verified tools included:

```text
Git                     2.54.0.windows.1
uv                      0.12.12
CPython                 3.12.13
CMake                   3.31.12
Ninja                   1.13.1
Arm GNU Toolchain       14.2.Rel1
arm-none-eabi-gcc       14.2.1 20241119
arm-none-eabi-g++       14.2.1 20241119
arm-none-eabi-gdb       15.2.90.20241130-git
STM32CubeMX             6.18.1
STM32CubeF4             V1.28.3
```

The ARM compiler target was verified as:

```text
arm-none-eabi
```

### 7. STM32CubeProgrammer installation

`STM32_Programmer_CLI.exe` was not found during the initial host-tool audit.

STM32CubeProgrammer `2.23.0` was therefore installed under the local tools root.

The command-line interface was then invoked successfully and reported:

```text
STM32CubeProgrammer v2.23.0
```

No ST-LINK device was connected and no programming operation was attempted.

### 8. Repository-local environment smoke script

A reusable repository-local environment check was added at:

```text
tools/environment-smoke.ps1
```

The script accepts the machine-specific tools root at runtime instead of embedding one user's absolute path in the repository.

It verifies:

- Git invocation
- uv invocation
- project-local Python 3.12.13
- virtual-environment status
- `uv sync --locked --offline`
- CMake
- Ninja
- ARM GCC
- ARM G++
- ARM GDB
- ARM readelf
- ARM target `arm-none-eabi`
- STM32CubeMX command-line startup
- STM32CubeProgrammer CLI startup
- real Cortex-M4F cross-compilation
- ARM object attributes
- `.venv/` ignore behavior
- `build/` ignore behavior
- absence of tracked files under generated directories
- absence of Windows absolute-path candidates in tracked text

### 9. Smoke-script debugging history

#### 9.1 Initial script-construction failure

The first attempt to create the smoke script directly inside an interactive PowerShell here-string was invalid.

A nested here-string terminator prematurely ended the outer string. The remaining text was then interpreted interactively by PowerShell, producing cascading null-variable and missing-function errors.

No valid smoke script was written to the repository during this failed attempt.

The repository was checked afterward and contained no generated script or additional junk from the failure.

#### 9.2 Downloaded script execution policy

A subsequently generated script was downloaded as an actual `.ps1` file.

Windows attached a `Zone.Identifier` Mark-of-the-Web stream to the file.

The active PowerShell execution policy was:

```text
CurrentUser = RemoteSigned
```

The script was therefore blocked because it was considered downloaded and unsigned.

The script's SHA-256 and PowerShell syntax were verified first. Only that verified file was then unblocked with `Unblock-File`; no global execution policy was weakened.

#### 9.3 Windows PowerShell 5.1 / CubeMX stderr behavior

The first executable smoke-script revision reached the CubeMX command-line check but stopped when the CubeMX Java runtime wrote a warning to stderr.

The warning was:

```text
WARNING: Could not open/create prefs root node Software\JavaSoft\Prefs ...
Windows RegCreateKeyEx(...) returned error code 5.
```

Windows PowerShell 5.1 converted the native stderr stream into a PowerShell error record while `$ErrorActionPreference = 'Stop'` was active.

A dedicated CubeMX diagnostic was then run using `Start-Process` with separate stdout/stderr capture.

The diagnostic executed:

```text
help
exit
```

and returned:

```text
ExitCode: 0
```

CubeMX also successfully initialized its database and reported its installation path and Java runtime.

Therefore, the Java preferences warning was classified as non-blocking for the command-line smoke test.

The final smoke script was revised to:

- launch CubeMX as a child process;
- redirect stdout and stderr separately;
- enforce a timeout;
- evaluate the real process exit code;
- preserve stderr as diagnostic output rather than treating any stderr text as automatic failure.

No administrator privilege or registry-permission change was required.

### 10. Final repository-local smoke result

The corrected smoke check completed successfully.

Verified output included:

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

The Cortex-M4F smoke compilation used:

```text
-mcpu=cortex-m4
-mthumb
-mfpu=fpv4-sp-d16
-mfloat-abi=hard
-Wall
-Wextra
-Werror
```

The resulting object file reported:

```text
Tag_CPU_arch: v7E-M
Tag_FP_arch: VFPv4-D16
Tag_ABI_VFP_args: VFP registers
```

This proves that CMake, Ninja, and the selected Arm GNU toolchain can jointly produce the expected Cortex-M4F object format on the host computer.

It does not prove target-board execution.

### 11. Environment documentation

A dedicated environment reference was added:

```text
docs/environment.md
```

It records:

- verified host-tool versions;
- path conventions;
- Python environment policy;
- STM32CubeF4 package version;
- repository-local smoke invocation;
- expected smoke evidence;
- CubeMX Java warning as a known non-blocking observation;
- explicit hardware evidence boundaries.

The bring-up log was also normalized to remove user-specific absolute paths.

PowerShell scripts were explicitly assigned LF line endings in `.gitattributes`.

### 12. Repository hygiene at P0-A1 closure

Before commit:

- staged whitespace checks passed;
- staged absolute-path checks passed;
- `tools/environment-smoke.ps1` was confirmed as `eol=lf`;
- generated smoke output remained under ignored `build/`;
- `.venv/` remained ignored;
- no generated junk was tracked.

The P0-A1 deliverables were committed as:

```text
6337c44  chore: establish reproducible development environment
```

and successfully pushed to `origin/main`.

### 13. P0-A1 result

**PASS**

P0-A1 established a reproducible, repository-documented computer-side environment and a reusable command-line smoke check.

### 14. Evidence boundary and current state

Confirmed:

- host Git workflow;
- project-local Python environment;
- offline locked Python synchronization;
- CMake/Ninja/ARM cross-toolchain invocation;
- ARM C/C++ compiler and GDB availability;
- CubeMX command-line startup;
- CubeProgrammer CLI startup;
- Cortex-M4F cross-compilation;
- repository hygiene checks;
- committed environment documentation and smoke script.

Still untested because the board and cable were unavailable:

- ST-LINK connection;
- physical board enumeration;
- flashing;
- target GDB session;
- VCP/UART;
- target boot;
- physical HSE behavior;
- real 180 MHz clock operation;
- ADC;
- TIM;
- DMA/DBM;
- DAC;
- watchdog;
- FreeRTOS runtime behavior;
- any board-level timing measurement;
- R0-R7 hardware validation.

At the end of P0-A1, `main` was synchronized with `origin/main`.

The only remaining working-tree modification was the separate CubeMX static clock configuration in:

```text
firmware/cubemx/cubemx.ioc
```

This change was intentionally not included in the P0-A1 environment commit.
