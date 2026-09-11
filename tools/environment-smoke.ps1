param(
    [Parameter(Mandatory = $true)]
    [string]$ToolsRoot,

    [string]$UvExe
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildRoot = Join-Path $RepoRoot 'build\environment-smoke'

function Assert-Leaf {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Label
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label not found: $Path"
    }

    Write-Host "[FOUND] $Label"
    return (Resolve-Path -LiteralPath $Path).Path
}

function Assert-Directory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Label
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Label not found: $Path"
    }

    Write-Host "[FOUND] $Label"
    return (Resolve-Path -LiteralPath $Path).Path
}

function Invoke-VersionCheck {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Exe,

        [Parameter(Mandatory = $true)]
        [string]$Label,

        [string[]]$Arguments = @('--version')
    )

    $output = & $Exe @Arguments 2>&1
    $exitCode = $LASTEXITCODE

    if ($exitCode -ne 0) {
        throw "$Label version check failed with exit code $exitCode."
    }

    $output | Select-Object -First 2
}

Write-Output '=== STM32 STREAM LAB ENVIRONMENT SMOKE CHECK ==='
Write-Output "Repository: $RepoRoot"
Write-Output "Tools root: $ToolsRoot"
Write-Output ''

# ----------------------------------------------------------------------
# Git
# ----------------------------------------------------------------------

$gitCommand = Get-Command git.exe -ErrorAction Stop | Select-Object -First 1
$GitExe = $gitCommand.Source

Write-Output '=== Git ==='
Invoke-VersionCheck -Exe $GitExe -Label 'Git'
Write-Output ''

# ----------------------------------------------------------------------
# uv and project Python
# ----------------------------------------------------------------------

if ([string]::IsNullOrWhiteSpace($UvExe)) {
    $uvCommand = Get-Command uv.exe -ErrorAction SilentlyContinue | Select-Object -First 1

    if ($uvCommand) {
        $UvExe = $uvCommand.Source
    } else {
        $fallbackUv = Join-Path $HOME '.local\bin\uv.exe'
        if (Test-Path -LiteralPath $fallbackUv -PathType Leaf) {
            $UvExe = $fallbackUv
        }
    }
}

if ([string]::IsNullOrWhiteSpace($UvExe)) {
    throw 'uv was not found in PATH and no fallback uv executable was found.'
}

$UvExe = Assert-Leaf -Path $UvExe -Label 'uv'

$ProjectPython = Assert-Leaf `
    -Path (Join-Path $RepoRoot '.venv\Scripts\python.exe') `
    -Label 'project Python'

$UvPythonRoot = Assert-Directory `
    -Path (Join-Path $ToolsRoot 'uv\python') `
    -Label 'uv managed Python root'

$UvCache = Assert-Directory `
    -Path (Join-Path $ToolsRoot 'uv\cache') `
    -Label 'uv cache'

Write-Output ''
Write-Output '=== uv / Python ==='

Invoke-VersionCheck -Exe $UvExe -Label 'uv'

$pythonCheck = 'import sys; expected=(3,12,13); assert sys.version_info[:3] == expected, f"Expected Python 3.12.13, got {sys.version.split()[0]}"; assert sys.prefix != sys.base_prefix, "Project Python is not running inside a virtual environment"; print("Python:", sys.version.split()[0]); print("Executable:", sys.executable); print("Virtual environment: True")'

$pythonOutput = & $ProjectPython -c $pythonCheck 2>&1
$pythonExit = $LASTEXITCODE

if ($pythonExit -ne 0) {
    $pythonOutput
    throw "Project Python check failed with exit code $pythonExit."
}

$pythonOutput

$oldPythonInstallDir = $env:UV_PYTHON_INSTALL_DIR

try {
    $env:UV_PYTHON_INSTALL_DIR = $UvPythonRoot

    & $UvExe sync `
        --locked `
        --offline `
        --directory $RepoRoot `
        --cache-dir $UvCache

    if ($LASTEXITCODE -ne 0) {
        throw 'uv locked/offline sync failed.'
    }
}
finally {
    $env:UV_PYTHON_INSTALL_DIR = $oldPythonInstallDir
}

Write-Output 'uv locked/offline sync: PASS'

# ----------------------------------------------------------------------
# Native / ARM tools
# ----------------------------------------------------------------------

$CMakeExe = Assert-Leaf `
    -Path (Join-Path $ToolsRoot 'cmake-3.31.12-windows-x86_64\bin\cmake.exe') `
    -Label 'CMake'

$NinjaExe = Assert-Leaf `
    -Path (Join-Path $ToolsRoot 'ninja-1.13.1-windows-x86_64\ninja.exe') `
    -Label 'Ninja'

$ArmRoot = Assert-Directory `
    -Path (Join-Path $ToolsRoot 'arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi') `
    -Label 'Arm GNU Toolchain'

$ArmBin = Join-Path $ArmRoot 'bin'

$GccExe = Assert-Leaf `
    -Path (Join-Path $ArmBin 'arm-none-eabi-gcc.exe') `
    -Label 'ARM GCC'

$GppExe = Assert-Leaf `
    -Path (Join-Path $ArmBin 'arm-none-eabi-g++.exe') `
    -Label 'ARM G++'

$GdbExe = Assert-Leaf `
    -Path (Join-Path $ArmBin 'arm-none-eabi-gdb.exe') `
    -Label 'ARM GDB'

$ReadElfExe = Assert-Leaf `
    -Path (Join-Path $ArmBin 'arm-none-eabi-readelf.exe') `
    -Label 'ARM readelf'

Write-Output ''
Write-Output '=== CMake / Ninja / Arm GNU Toolchain ==='

Invoke-VersionCheck -Exe $CMakeExe -Label 'CMake'
Invoke-VersionCheck -Exe $NinjaExe -Label 'Ninja'
Invoke-VersionCheck -Exe $GccExe -Label 'ARM GCC'
Invoke-VersionCheck -Exe $GppExe -Label 'ARM G++'
Invoke-VersionCheck -Exe $GdbExe -Label 'ARM GDB'

$target = (& $GccExe -dumpmachine 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0) {
    throw 'ARM GCC target check failed.'
}

if ($target -ne 'arm-none-eabi') {
    throw "Unexpected ARM GCC target: $target"
}

Write-Output "ARM target: $target"

# ----------------------------------------------------------------------
# STM32CubeMX command-line smoke
# ----------------------------------------------------------------------

$CubeMxRoot = Assert-Directory `
    -Path (Join-Path $ToolsRoot 'STM32CubeMX-6.18.1') `
    -Label 'STM32CubeMX'

$CubeMxExe = Assert-Leaf `
    -Path (Join-Path $CubeMxRoot 'STM32CubeMX.exe') `
    -Label 'STM32CubeMX executable'

$CubeMxJava = Assert-Leaf `
    -Path (Join-Path $CubeMxRoot 'jre\bin\java.exe') `
    -Label 'STM32CubeMX bundled Java'

$CubeF4Root = Assert-Directory `
    -Path (Join-Path $ToolsRoot 'STM32Cube\Repository\STM32Cube_FW_F4_V1.28.3') `
    -Label 'STM32CubeF4 V1.28.3'

Write-Output ''
Write-Output '=== STM32CubeMX command-line smoke ==='

New-Item -ItemType Directory -Path $BuildRoot -Force | Out-Null

$CubeMxScript = Join-Path $BuildRoot 'cubemx-smoke.txt'
$CubeStdout = Join-Path $BuildRoot 'cubemx-stdout.log'
$CubeStderr = Join-Path $BuildRoot 'cubemx-stderr.log'

[System.IO.File]::WriteAllText(
    $CubeMxScript,
    "help`r`nexit`r`n",
    [System.Text.Encoding]::ASCII
)

foreach ($path in @($CubeStdout, $CubeStderr)) {
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Force
    }
}

$cubeArguments = ('-jar "{0}" -q "{1}"' -f $CubeMxExe, $CubeMxScript)

$cubeProcess = Start-Process `
    -FilePath $CubeMxJava `
    -ArgumentList $cubeArguments `
    -WorkingDirectory $CubeMxRoot `
    -RedirectStandardOutput $CubeStdout `
    -RedirectStandardError $CubeStderr `
    -NoNewWindow `
    -PassThru

$null = $cubeProcess.Handle

if (-not $cubeProcess.WaitForExit(120000)) {
    Stop-Process -InputObject $cubeProcess -ErrorAction Stop
    $cubeProcess.WaitForExit()
    $cubeProcess.Dispose()
    throw 'STM32CubeMX command-line smoke timed out after 120 seconds.'
}

$cubeProcess.WaitForExit()
$cubeExit = $cubeProcess.ExitCode
$cubeProcess.Dispose()

$cubeOut = Get-Content -LiteralPath $CubeStdout -ErrorAction SilentlyContinue
$cubeErr = Get-Content -LiteralPath $CubeStderr -ErrorAction SilentlyContinue

$cubeOut | Select-Object -First 20

if ($cubeErr) {
    Write-Output 'CubeMX stderr (informational because process exit code is checked separately):'
    $cubeErr
}

if ($cubeExit -ne 0) {
    throw "STM32CubeMX command-line smoke failed with exit code $cubeExit."
}

if (($cubeOut | Measure-Object).Count -eq 0) {
    throw 'STM32CubeMX command-line smoke produced no stdout.'
}

Write-Output 'STM32CubeMX CLI: PASS'

# ----------------------------------------------------------------------
# STM32CubeProgrammer CLI
# ----------------------------------------------------------------------

$ProgrammerRoot = Assert-Directory `
    -Path (Join-Path $ToolsRoot 'STM32CubeProgrammer-2.23.0') `
    -Label 'STM32CubeProgrammer'

$ProgrammerCli = Get-ChildItem `
    -LiteralPath $ProgrammerRoot `
    -Filter 'STM32_Programmer_CLI.exe' `
    -File `
    -Recurse `
    -ErrorAction Stop |
    Select-Object -First 1

if (-not $ProgrammerCli) {
    throw 'STM32_Programmer_CLI.exe not found inside STM32CubeProgrammer installation.'
}

Write-Output ''
Write-Output '=== STM32CubeProgrammer CLI ==='
Write-Output "CLI: $($ProgrammerCli.FullName)"

$programmerHelp = & $ProgrammerCli.FullName -h 2>&1
$programmerExit = $LASTEXITCODE

if ($programmerExit -ne 0) {
    throw "STM32CubeProgrammer CLI help check failed with exit code $programmerExit."
}

$programmerHelp | Select-Object -First 12

$programmerText = $programmerHelp -join "`n"
if ($programmerText -notmatch 'STM32CubeProgrammer v2\.23\.0') {
    throw 'STM32CubeProgrammer version banner did not report v2.23.0.'
}

Write-Output 'STM32CubeProgrammer CLI: PASS'

# ----------------------------------------------------------------------
# Reproducible Cortex-M4F compile smoke
# ----------------------------------------------------------------------

Write-Output ''
Write-Output '=== Cortex-M4F cross-compile smoke ==='

$ArmSmokeRoot = Join-Path $BuildRoot 'arm-static-lib'
$ArmSmokeBuild = Join-Path $ArmSmokeRoot 'build'

if (Test-Path -LiteralPath $ArmSmokeRoot) {
    Remove-Item -LiteralPath $ArmSmokeRoot -Recurse -Force
}

New-Item -ItemType Directory -Path $ArmSmokeRoot -Force | Out-Null

$gccCMake = $GccExe.Replace('\', '/')
$arCMake = (Join-Path $ArmBin 'arm-none-eabi-ar.exe').Replace('\', '/')
$ranlibCMake = (Join-Path $ArmBin 'arm-none-eabi-ranlib.exe').Replace('\', '/')

$toolchainText = @"
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER "$gccCMake")
set(CMAKE_AR "$arCMake")
set(CMAKE_RANLIB "$ranlibCMake")
"@

[System.IO.File]::WriteAllText(
    (Join-Path $ArmSmokeRoot 'toolchain.cmake'),
    $toolchainText,
    [System.Text.Encoding]::ASCII
)

$cmakeListsText = @'
cmake_minimum_required(VERSION 3.31)

project(environment_arm_smoke LANGUAGES C)

add_library(environment_arm_smoke STATIC smoke.c)

set_target_properties(environment_arm_smoke PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

target_compile_options(environment_arm_smoke PRIVATE
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
    -Wall
    -Wextra
    -Werror
)
'@

[System.IO.File]::WriteAllText(
    (Join-Path $ArmSmokeRoot 'CMakeLists.txt'),
    $cmakeListsText,
    [System.Text.Encoding]::ASCII
)

$sourceText = @'
volatile unsigned int smoke_value = 0x1234u;

void smoke_increment(void)
{
    smoke_value++;
}
'@

[System.IO.File]::WriteAllText(
    (Join-Path $ArmSmokeRoot 'smoke.c'),
    $sourceText,
    [System.Text.Encoding]::ASCII
)

& $CMakeExe `
    -S $ArmSmokeRoot `
    -B $ArmSmokeBuild `
    -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$NinjaExe" `
    "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $ArmSmokeRoot 'toolchain.cmake')"

if ($LASTEXITCODE -ne 0) {
    throw 'CMake ARM smoke configure failed.'
}

& $CMakeExe --build $ArmSmokeBuild --verbose

if ($LASTEXITCODE -ne 0) {
    throw 'Cortex-M4F smoke build failed.'
}

$ObjectFile = Get-ChildItem `
    -LiteralPath $ArmSmokeBuild `
    -Filter 'smoke.c.obj' `
    -File `
    -Recurse `
    -ErrorAction Stop |
    Select-Object -First 1

if (-not $ObjectFile) {
    throw 'Compiled ARM smoke object not found.'
}

$attributes = (& $ReadElfExe -A $ObjectFile.FullName 2>&1) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw 'ARM readelf attribute check failed.'
}

foreach ($required in @(
    'Tag_CPU_arch: v7E-M',
    'Tag_FP_arch: VFPv4-D16',
    'Tag_ABI_VFP_args: VFP registers'
)) {
    if ($attributes -notmatch [regex]::Escape($required)) {
        throw "Required ARM attribute missing: $required"
    }
}

Write-Output 'ARM object attributes: PASS'

# ----------------------------------------------------------------------
# Repository hygiene checks
# ----------------------------------------------------------------------

Write-Output ''
Write-Output '=== Repository hygiene ==='

& $GitExe -C $RepoRoot check-ignore -q -- '.venv/probe.txt'
if ($LASTEXITCODE -ne 0) {
    throw '.venv is not ignored by Git.'
}

& $GitExe -C $RepoRoot check-ignore -q -- 'build/probe.txt'
if ($LASTEXITCODE -ne 0) {
    throw 'build output is not ignored by Git.'
}

$trackedGenerated = & $GitExe -C $RepoRoot ls-files -- '.venv' 'build' 2>&1
if ($LASTEXITCODE -ne 0) {
    throw 'Could not inspect tracked generated directories.'
}

if (($trackedGenerated | Measure-Object).Count -ne 0) {
    throw "Generated directories contain tracked files:`n$($trackedGenerated -join "`n")"
}

$pathMatches = & $GitExe -C $RepoRoot --no-pager grep -n -I -E `
    '(^|[^[:alnum:]_])[A-Za-z](\\)?:[\\/]' -- . 2>&1
$grepExit = $LASTEXITCODE

if ($grepExit -eq 0) {
    throw "Tracked text contains Windows absolute-path candidates:`n$($pathMatches -join "`n")"
}

if ($grepExit -ne 1) {
    throw "Absolute-path search failed with exit code $grepExit."
}

Write-Output '.venv ignore: PASS'
Write-Output 'build ignore: PASS'
Write-Output 'tracked generated directories: PASS'
Write-Output 'tracked Windows absolute paths: PASS'

Write-Output ''
Write-Output '=== Repository status after smoke ==='

& $GitExe -C $RepoRoot status --short --untracked-files=all
if ($LASTEXITCODE -ne 0) {
    throw 'Git status check failed.'
}

Write-Output ''
Write-Output '=== ENVIRONMENT SMOKE RESULT: PASS ==='
