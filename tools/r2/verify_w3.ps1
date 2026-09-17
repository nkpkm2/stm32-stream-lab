# Native/target BUILD ONLY. No flash, debugger, reset, commit, or tag operation.
[CmdletBinding()]
param(
    [ValidateSet('Native','W3Target','R1Baseline')]
    [string]$Profile = 'Native',
    [string]$Repo = '',
    [string]$CMakeBin = 'E:\DevTools\cmake-3.31.12-windows-x86_64\bin',
    [string]$Ninja = 'E:\DevTools\ninja-1.13.1-windows-x86_64\ninja.exe',
    [string]$ArmBin = 'E:\DevTools\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi\bin',
    [string]$VsDevCmd = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
if ([string]::IsNullOrWhiteSpace($Repo)) {
    $Repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
}
$Repo = (Resolve-Path -LiteralPath $Repo).Path
$Git = (Get-Command git.exe -ErrorAction Stop).Source
$cmake = Join-Path $CMakeBin 'cmake.exe'
$ctest = Join-Path $CMakeBin 'ctest.exe'
$oldLocation = Get-Location

function Git-Lines {
    param([string[]]$Arguments)
    $old = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $lines = @(& $Git -C $Repo @Arguments)
        $rc = $LASTEXITCODE
    } finally { $ErrorActionPreference = $old }
    if ($rc -ne 0) { throw ('Git failed: {0}' -f ($Arguments -join ' ')) }
    return $lines
}
function Run-Logged {
    param([string]$Exe, [string[]]$Arguments, [string]$Log, [switch]$Quiet)
    if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
        throw ('Tool not found: {0}' -f $Exe)
    }
    $old = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Exe @Arguments 2>&1 | ForEach-Object {
            $line = $_.ToString()
            if (-not $Quiet) { Write-Host $line }
            $line
        } | Out-File -LiteralPath $Log -Encoding utf8
        $rc = $LASTEXITCODE
    } finally { $ErrorActionPreference = $old }
    if ($rc -ne 0) { throw ('Command failed ({0}). Log: {1}' -f $rc, $Log) }
}
function Source-Hashes {
    $paths = @(Git-Lines -Arguments @('ls-files','--cached','--others','--exclude-standard'))
    $result = @{}
    foreach ($relative in ($paths | Sort-Object -Unique)) {
        $full = Join-Path $Repo $relative
        if (-not (Test-Path -LiteralPath $full -PathType Leaf)) {
            throw ('Source file missing: {0}' -f $relative)
        }
        $result[$relative] = (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash
    }
    return $result
}
try {
    Write-Host ('=== R2-W3 VERIFICATION: {0} ===' -f $Profile)
    Set-Location -LiteralPath $Repo
    $tag = @(Git-Lines -Arguments @('rev-parse','r1-pass^{commit}'))
    if ($tag.Count -ne 1 -or $tag[0] -ne '5ebf62e90b31e262f44013afb594a430061f139a') {
        throw 'R1 gate tag mismatch.'
    }
    $head = @(Git-Lines -Arguments @('rev-parse','HEAD'))[0]
    $before = Source-Hashes
    $indexBefore = @(Git-Lines -Arguments @('write-tree'))[0]
    $build = Join-Path $Repo ('build\r2-w3-' + $Profile.ToLowerInvariant() + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
    New-Item -ItemType Directory -Path $build | Out-Null
    $source = Join-Path $Repo 'tests\native'
    $config = @('-S',$source,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Debug',('-DCMAKE_MAKE_PROGRAM=' + $Ninja))
    if ($Profile -eq 'Native') {
        if (-not (Test-Path -LiteralPath $VsDevCmd -PathType Leaf)) { throw 'VsDevCmd.bat not found.' }
        $old = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            $envLines = @(& $env:ComSpec /d /s /c ('"{0}" -arch=x64 -host_arch=x64 >nul && set' -f $VsDevCmd))
            $rc = $LASTEXITCODE
        } finally { $ErrorActionPreference = $old }
        if ($rc -ne 0) { throw 'MSVC environment setup failed.' }
        foreach ($line in $envLines) {
            if ($line -match '^([^=]+)=(.*)$') {
                [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process')
            }
        }
        $cl = (Get-Command cl.exe -ErrorAction Stop).Source
        $config += ('-DCMAKE_C_COMPILER:FILEPATH=' + $cl)
        Write-Host ('Compiler: {0}' -f $cl)
    } else {
        $env:PATH = $ArmBin + ';' + $env:PATH
        $source = Join-Path $Repo 'firmware\cubemx'
        $enabled = if ($Profile -eq 'W3Target') { 'ON' } else { 'OFF' }
        $config = @('-S',$source,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Debug',
            ('-DCMAKE_MAKE_PROGRAM=' + $Ninja),
            ('-DCMAKE_TOOLCHAIN_FILE=' + (Join-Path $source 'cmake\gcc-arm-none-eabi.cmake')),
            ('-DSTREAM_LAB_R2_W3=' + $enabled))
    }
    Run-Logged -Exe $cmake -Arguments $config -Log (Join-Path $build 'configure.txt')
    Run-Logged -Exe $cmake -Arguments @('--build',$build,'--parallel','--verbose') -Log (Join-Path $build 'build.txt')
    if ($Profile -eq 'Native') {
        Run-Logged -Exe $ctest -Arguments @('--test-dir',$build,'--show-only=json-v1') -Log (Join-Path $build 'test-list.json') -Quiet
        $listing = [System.IO.File]::ReadAllText((Join-Path $build 'test-list.json')) | ConvertFrom-Json
        if (@($listing.tests).Count -ne 62) { throw 'Expected exactly 62 registered W1/W2/W3 tests.' }
        Run-Logged -Exe $ctest -Arguments @('--test-dir',$build,'--no-tests=error','--output-on-failure','--output-junit',(Join-Path $build 'ctest.xml')) -Log (Join-Path $build 'ctest.txt')
        foreach ($name in @('test_r2_buffer_pool.exe','test_r2_dma_slots.exe','test_r2_w3_guard.exe')) {
            Run-Logged -Exe (Join-Path $build $name) -Arguments @() -Log (Join-Path $build ($name + '.txt'))
        }
    } else {
        $elf = Join-Path $build 'cubemx.elf'
        if (-not (Test-Path -LiteralPath $elf -PathType Leaf)) { throw 'Expected ELF not found.' }
        Run-Logged -Exe (Join-Path $ArmBin 'arm-none-eabi-nm.exe') -Arguments @('-g','--defined-only',$elf) -Log (Join-Path $build 'symbols.txt')
        $symbols = [System.IO.File]::ReadAllText((Join-Path $build 'symbols.txt'))
        $required = if ($Profile -eq 'W3Target') {
            @('R2_W3_CreateTask','R2_W3_IrqEnter','R2_W3_IrqExit','g_r2_w3_result')
        } else { @('R1_Acquisition_Start','R1_Bringup_CreateTask','g_r1_soak_result') }
        foreach ($symbol in $required) {
            if ($symbols -notmatch ('(?m)\b' + [regex]::Escape($symbol) + '\r?$')) {
                throw ('Missing linked symbol: {0}' -f $symbol)
            }
        }
        if ($Profile -eq 'W3Target' -and $symbols -match '(?m)\bR1_Acquisition_Start\r?$') {
            throw 'R1 driver unexpectedly linked into the W3-exclusive profile.'
        }
        $elfHash = (Get-FileHash -LiteralPath $elf -Algorithm SHA256).Hash
        Write-Host ('ELF SHA256: {0}' -f $elfHash)
        [System.IO.File]::WriteAllText((Join-Path $build 'elf.sha256'),$elfHash + "`n")
    }
    $after = Source-Hashes
    if ($before.Count -ne $after.Count) { throw 'Source file set changed during build.' }
    foreach ($path in $before.Keys) {
        if (-not $after.ContainsKey($path) -or $after[$path] -ne $before[$path]) {
            throw ('Source bytes changed during build: {0}' -f $path)
        }
    }
    if (@(Git-Lines -Arguments @('write-tree'))[0] -ne $indexBefore) { throw 'Git index content changed.' }
    if (@(Git-Lines -Arguments @('rev-parse','HEAD'))[0] -ne $head) { throw 'HEAD changed.' }
    $report = @(
        ('Profile: {0}' -f $Profile), ('Source HEAD: {0}' -f $head),
        'Build: PASS', 'Source byte fingerprint: unchanged', 'Git index content: unchanged',
        'Hardware: NOT RUN', ('Build directory: {0}' -f $build)
    )
    if ($Profile -eq 'Native') { $report += 'W1/W2/W3 native tests: 62 / 62 PASS' }
    [System.IO.File]::WriteAllLines((Join-Path $build 'verification-summary.txt'),$report,(New-Object System.Text.UTF8Encoding($false)))
    Write-Host ''
    Write-Host 'R2-W3 VERIFICATION: PASS'
    $report | ForEach-Object { Write-Host $_ }
    Write-Host 'No flash, reset, commit, push, or gate-tag operation.'
} catch {
    Write-Host ''
    Write-Host 'R2-W3 VERIFICATION: FAIL'
    Write-Host $_.Exception.Message
    Write-Host 'Preserve this build directory and return the error output.'
    exit 1
} finally { Set-Location -LiteralPath $oldLocation.Path }
