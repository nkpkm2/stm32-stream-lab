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

## Session continuation: 2026-09-11 — Operations Work Package P0-A2

### 15. P0-A2 objective

Operations Work Package P0-A2 required a minimal reproducible firmware-build baseline using the existing STM32CubeMX project shell.

The required proof was:

- generate the actual STM32 project from the tracked `.ioc`;
- perform a clean configure/build/link using the installed ARM GCC toolchain;
- produce target artifacts;
- verify Cortex-M4F / FPv4-SP-D16 / hard-float configuration;
- document the reproducible build command;
- keep generated build output out of Git;
- preserve `.ioc` tracking;
- avoid machine-specific paths and unrelated firmware implementation.

The physical board and data cable were still unavailable, so the work package remained strictly host-side.

### 16. Isolating unrelated CubeMX clock work

Before P0-A2, the working tree contained an uncommitted CubeMX clock experiment with 180 MHz static clock settings.

That work was intentionally excluded from the P0-A2 build baseline and saved as:

```text
stash@{0}: On main: pre-P0-A2 CubeMX clock configuration
```

The working tree was returned to the committed minimal CubeMX shell.

The restored `.ioc` baseline reported:

```text
ProjectManager.FirmwarePackage=STM32Cube FW_F4 V1.28.3
ProjectManager.LastFirmware=false
ProjectManager.NoMain=false
ProjectManager.ProjectName=cubemx
ProjectManager.TargetToolchain=CMake
```

At that point, `firmware/cubemx/` contained only the tracked `cubemx.ioc`.

### 17. Full CubeMX project generation

The minimal CubeMX project was regenerated from the clean `.ioc`.

The generated project included:

```text
CMakeLists.txt
CMakePresets.json
startup_stm32f446xx.s
STM32F446xx_FLASH.ld
cmake/gcc-arm-none-eabi.cmake
cmake/stm32cubemx/CMakeLists.txt
Core/Inc/*
Core/Src/*
Drivers/CMSIS/*
Drivers/STM32F4xx_HAL_Driver/*
Drivers/BSP/STM32F4xx-Nucleo/*
```

The generated GCC toolchain file explicitly contained:

```text
-mcpu=cortex-m4
-mfpu=fpv4-sp-d16
-mfloat-abi=hard
```

and linked with:

```text
STM32F446xx_FLASH.ld
```

The generated CMake configuration produced an ELF executable and linker map.

### 18. Clean configure/build/link procedure

A clean build directory was used:

```text
build/p0-a2
```

The procedure was:

1. remove the previous `build/p0-a2` directory if present;
2. configure CMake using:
   - the generated CubeMX source tree;
   - Ninja;
   - `cmake/gcc-arm-none-eabi.cmake`;
   - `Debug` build type;
3. build the complete generated firmware;
4. inspect the resulting ELF;
5. package the ELF into `.hex` and `.bin` images using the same Arm GNU toolchain.

The build completed successfully.

### 19. Target artifacts

The final verified target artifacts were:

```text
cubemx.elf
cubemx.map
cubemx.hex
cubemx.bin
```

Observed sizes:

```text
cubemx.elf  1,007,272 bytes
cubemx.map    343,837 bytes
cubemx.hex     20,459 bytes
cubemx.bin      7,240 bytes
```

`arm-none-eabi-size` reported:

```text
text    data     bss     dec     hex
7220      20    1572    8812    226c
```

### 20. Architecture / ABI verification

The ELF header reported:

```text
Class:   ELF32
Data:    2's complement, little endian
Machine: ARM
Flags:   Version5 EABI, hard-float ABI
```

ARM attributes reported:

```text
Tag_CPU_name: "7E-M"
Tag_CPU_arch: v7E-M
Tag_CPU_arch_profile: Microcontroller
Tag_FP_arch: VFPv4-D16
Tag_ABI_VFP_args: VFP registers
```

The generated compile database independently confirmed:

```text
-mcpu=cortex-m4
-mfpu=fpv4-sp-d16
-mfloat-abi=hard
```

Therefore, the actual firmware build used the expected Cortex-M4F / ARMv7E-M / FPv4-SP-D16 / hard-float configuration.

### 21. Artifact packaging

The generated CMake baseline produced the ELF and linker map directly.

Standard flashable HEX and BIN images were then derived from the verified ELF with:

```text
arm-none-eabi-objcopy -O ihex
arm-none-eabi-objcopy -O binary
```

No CMake refactor or unnecessary post-build customization was introduced solely to create those formats.

### 22. Git hygiene

The following checks passed:

- `firmware/cubemx/cubemx.ioc` remained tracked;
- `build/p0-a2/*` remained ignored through the existing `/build/` rule;
- `.elf`, `.map`, `.hex`, and `.bin` build artifacts were not staged;
- the generated source tree contained no machine-specific Windows absolute-path candidates;
- `.mxproject` contained only CubeMX bookkeeping and relative paths and was intentionally ignored;
- ST/CubeMX generated source was not reformatted merely to satisfy whitespace style checks.

`system_stm32f4xx.c` contained vendor/generated trailing whitespace. This was intentionally left unchanged to avoid meaningless vendor-code diffs and regeneration churn.

A focused whitespace check was applied only to project-authored files.

### 23. Firmware build documentation

A reproducible build reference was added:

```text
docs/firmware-build.md
```

It records:

- source and toolchain inputs;
- clean build directory;
- CMake/Ninja build procedure;
- ELF-to-HEX/BIN packaging;
- verified build result;
- architecture / ABI evidence;
- hardware evidence boundary.

### 24. P0-A2 milestone commit

The generated firmware baseline and build documentation were committed as:

```text
337e6c8  build: establish reproducible firmware baseline
```

### 25. GitHub push failure and network diagnosis

The initial push attempt failed with:

```text
Recv failure: Connection was reset
```

A second attempt also failed.

The local repository remained safe:

```text
## main...origin/main [ahead 1]
```

Network diagnosis showed:

```text
github.com DNS resolution     PASS
TCP connection to port 443    FAIL
curl HTTPS connection         TIMEOUT
git ls-remote                 FAIL
```

The browser could still access GitHub because the Windows user proxy was configured as:

```text
127.0.0.1:7890
```

while:

```text
WinHTTP proxy                 direct
HTTP_PROXY / HTTPS_PROXY      not set
Git proxy                     not configured
```

Therefore, Git was attempting a direct GitHub connection while the browser used the local VPN proxy.

A repository-local Git proxy was configured:

```text
http.proxy = http://127.0.0.1:7890
```

This setting is local to the repository and is not committed to Git.

Remote access then succeeded and the P0-A2 milestone was pushed successfully:

```text
337e6c8 (HEAD -> main, origin/main) build: establish reproducible firmware baseline
```

Final repository status:

```text
## main...origin/main
```

### 26. P0-A2 result

**PASS**

P0-A2 proves that the tracked minimal STM32CubeMX project can be regenerated into a complete STM32F446RE project, cleanly configured, compiled, linked, inspected, and packaged into standard target artifacts using the documented host-side workflow.

### 27. Remaining evidence boundary

Still untested because the physical board and cable are unavailable:

- ST-LINK detection;
- flashing;
- target boot;
- on-board GDB debugging;
- VCP/UART;
- real HSE and clock behavior;
- ADC;
- TIM;
- DMA / DBM;
- FreeRTOS runtime behavior;
- any board-level timing measurement;
- R0-R7 hardware validation.

Per the P0-A2 stop condition, firmware feature implementation must not continue until the physical board becomes available and board bring-up begins.
## 2026-09-13 — P0-B Real Hardware Bring-up

### Session scope

The first physical-board session began after the NUCLEO-F446RE and USB data cable became available.

Formal starting state:

- P0-A1 PASS — `6337c44`
- P0-A2 PASS — `337e6c8`
- P0-DOC1 evidence structure established
- R0 NOT STARTED

The session was intentionally limited to P0-B:

`identify → connect → flash → execute → debug → communicate`

No ADC, DMA, FreeRTOS application work, or R1 implementation was started.

### Physical board baseline

The physical board was confirmed as NUCLEO-F446RE.

Initial hardware state:

- both CN2 ST-LINK/Nucleo jumpers installed
- U5V selected
- IDD jumper installed
- JP1 not installed
- no external peripherals connected

During the initial failure state:

- LD1 COM: solid red
- LD3 PWR: solid red
- LD2 USER: green and rapidly blinking

The LD2 activity was treated only as evidence that a pre-existing application appeared to be running, not as proof that project firmware had executed.

### ST-LINK connection failure

STM32CubeProgrammer 2.23.0 successfully enumerated the onboard probe:

- ST-LINK serial: `067AFF545754655087043860`
- initial ST-LINK firmware: `V2J28M18`

SWD target connection repeatedly failed with:

`ST-LINK error (DEV_USB_COMM_ERR)`

The failure remained reproducible after:

- a controlled USB disconnect/reconnect
- changing the physical PC USB port
- changing to a second known-good USB data cable

Windows continued to enumerate the expected ST-LINK/V2-1 USB interfaces, including Debug, Mass Storage, Composite Device, and Virtual COM Port.

This separated basic USB enumeration from actual ST-LINK control/SWD communication.

### ST-LINK firmware recovery

The Java ST-Link Upgrade 3.17.11 utility detected the probe but failed to enter update mode with JNI/system error 121.

No firmware was changed during that failed attempt.

The official native Windows ST-LinkUpgrade utility was then used to update:

`V2J28M18 → V2J48M35`

LD1 flashed red regularly during the firmware update.

After the update, SWD connection succeeded immediately.

Post-upgrade target identification:

- ST-LINK firmware: `V2J48M35`
- target voltage: 3.20 V
- SWD frequency: 4000 kHz
- Device ID: `0x421`
- Revision: Rev A
- Device: STM32F446xx
- internal Flash: 512 KBytes
- CPU: Cortex-M4
- bootloader version: `0x90`

Technical conclusion: the repeatable `DEV_USB_COMM_ERR` was cleared after the onboard ST-LINK/V2-1 firmware update.

### First project firmware flash

Before the first project flash, `firmware/cubemx` was confirmed to be identical to the P0-A2 firmware milestone `337e6c8`.

A separate clean build directory was used:

`build/p0-b-baseline`

The existing P0-A2 build evidence was not overwritten.

Baseline footprint:

- text: 7220 B
- data: 20 B
- bss: 1572 B

First-flash ELF SHA-256:

`09982EB00B2454C045B3A1DEFF2C34561A0CF018C77E0769DFFAB49B0F0437A7`

Before the destructive first project flash, the previous target Flash contents were read back and retained locally as ignored evidence.

Project firmware programming and Flash verification both passed.

Flash success was not treated as target execution proof.

### Target execution and debugger proof

STM32CubeCLT 1.22.0 was added only to obtain the official ST-LINK GDB server.

ST-LINK GDB server version:

`7.14.0`

The existing project Arm GNU Toolchain remained in use. No GDB `load` command was issued during the execution test.

The debugger sequence was:

`reset → hardware breakpoint at main() → continue → breakpoint hit`

The target reached `main()` at the `HAL_Init()` line.

The session captured PC, SP, LR, and a valid backtrace.

Result:

- target execution: PASS
- debugger connection: PASS
- reset: PASS
- breakpoint: PASS
- continue-to-breakpoint: PASS

### USART2 / ST-LINK VCP bring-up

Before the VCP firmware change, PA2 and PA3 were already assigned to USART2 TX/RX at the pin level, and Windows exposed the ST-LINK VCP as COM5, but the MCU firmware did not yet contain a complete USART2 HAL initialization path.

USART2 was formally enabled in CubeMX with:

- PA2 = USART2_TX
- PA3 = USART2_RX
- 115200 baud
- 8 data bits
- no parity
- 1 stop bit
- TX/RX
- no hardware flow control
- oversampling 16
- USART2 IRQ disabled
- USART2 DMA disabled

CubeMX regeneration was reviewed before building.

No unexpected FreeRTOS, ADC1, TIM2, or DMA configuration was introduced.

The newly introduced UART HAL/LL vendor files were SHA-256 checked against STM32CubeF4 V1.28.3 and matched exactly.

A deterministic startup message was added only inside the CubeMX USER CODE region:

`P0-B VCP READY\r\n`

### VCP runtime validation

The VCP candidate was built in:

`build/p0-b-vcp-01`

VCP build footprint:

- text: 9620 B
- data: 40 B
- bss: 1640 B

VCP ELF SHA-256:

`62AFAC04597BD590092042257D18C796E4DC9A3C1DDB0C3F88E7597FF6523BF2`

The image was programmed and Flash verification passed.

The host opened the ST-LINK Virtual COM Port at 115200 8N1.

Two independent presses of the physical black RESET button each produced the exact expected startup message:

`P0-B VCP READY`

Result:

- MCU → PC VCP communication: PASS
- physical reset repeatability: PASS

### Milestones and evidence

P0-B firmware milestone:

`7b62082 feat: establish hardware bring-up and VCP baseline`

P0-B evidence/documentation commit:

`282c0b3 docs: record P0-B hardware bring-up evidence`

Daily project journal commit:

`c908478 docs: add project logs for 2026-09-13`

Formal evidence is stored under:

`docs/evidence/p0-b/`

The evidence includes ST-LINK failure/recovery output, real target identity, Flash verification, GDB execution proof, CubeMX/VCP audit, artifact manifests, and two-reset VCP runtime verification.

### Final state

- P0-B technical status: PASS
- R0 status: NOT STARTED
- R1-R7: NOT STARTED
- `p0-b-pass` tag not created pending Principal Lab Instructor acceptance

Work stopped after P0-B closeout. No R0 or R1 implementation was started.

## R0 Platform Freeze Closeout — 2026-09-13

R0 completed the common STM32F446 + FreeRTOS platform bring-up and freeze.

Accepted results:

- 180 MHz SYSCLK/HCLK runtime state verified.
- DWT/CYCCNT timing primitive verified.
- HAL timebase moved to TIM7 @ 1 kHz.
- FreeRTOS-Kernel V11.1.0 integrated from the exact locked upstream revision.
- SysTick, PendSV, and SVC ownership assigned to FreeRTOS.
- Two statically allocated smoke tasks repeatedly scheduled.
- NVIC_PRIORITYGROUP_0 reproduced as a genuine scheduler-start HardFault root cause.
- CubeMX source of truth repaired to NVIC_PRIORITYGROUP_4.
- Runtime AIRCR.PRIGROUP repaired from 7 to 3.
- Final IRQ / FromISR priority contract verified.
- Final committed-state clean rebuild, Flash, runtime, VCP, and two-reset regression passed.

Final firmware milestone: 9ade715d6f3035cd60512bf2ec4dd1c226436af8
Final committed-state ELF SHA256: 3E078CC76AB82A424B5E0141A1C9686821B776D6D959E44EB9F349B4BB202DEC

Formal defect investigation:
docs/evidence/r0/investigation-001-nvic-priority-group-hardfault.md

Principal technical acceptance: GRANTED.
Final Principal administrative closeout: PENDING.
r0-pass: NOT CREATED.
R1: NOT STARTED.
