# R2 final control-traffic CT-W2 BUILD ONLY. No flash/reset/debug/commit/push/tag.
[CmdletBinding()]
param(
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
$objcopy = Join-Path $ArmBin 'arm-none-eabi-objcopy.exe'
$nm = Join-Path $ArmBin 'arm-none-eabi-nm.exe'
$sizeTool = Join-Path $ArmBin 'arm-none-eabi-size.exe'
$oldLocation = Get-Location
$expectedHead = '45a5b0b3983d01b59893aea8cfb1b42c4afb2420'

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

function Initialize-Msvc {
    $existingCl = Get-Command cl.exe -ErrorAction SilentlyContinue
    $reuse = (
        $null -ne $existingCl -and
        -not [string]::IsNullOrWhiteSpace($env:VSCMD_VER) -and
        $env:VSCMD_ARG_TGT_ARCH -eq 'x64' -and
        $env:VSCMD_ARG_HOST_ARCH -eq 'x64'
    )

    if ($reuse) {
        Write-Host 'MSVC environment: reusing current x64 developer environment'
        return $existingCl.Source
    }

    if (-not (Test-Path -LiteralPath $VsDevCmd -PathType Leaf)) {
        throw 'VsDevCmd.bat not found.'
    }

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
    return (Get-Command cl.exe -ErrorAction Stop).Source
}

function Configure-Target {
    param([string]$Build, [string]$Ct)
    $source = Join-Path $Repo 'firmware\cubemx'
    $args = @(
        '-S',$source,'-B',$Build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Debug',
        ('-DCMAKE_MAKE_PROGRAM=' + $Ninja),
        ('-DCMAKE_TOOLCHAIN_FILE=' + (Join-Path $source 'cmake\gcc-arm-none-eabi.cmake')),
        '-DSTREAM_LAB_R2_W3=OFF',
        '-DSTREAM_LAB_R2_W4=OFF',
        '-DSTREAM_LAB_R2_W5=OFF',
        '-DSTREAM_LAB_R2_W6=ON',
        '-DSTREAM_LAB_R2_W6_K=8',
        '-DSTREAM_LAB_R2_W6_MODE=NORMAL',
        ('-DSTREAM_LAB_R2_CT=' + $Ct)
    )
    Run-Logged -Exe $cmake -Arguments $args -Log (Join-Path $Build 'configure.txt')
    Run-Logged -Exe $cmake -Arguments @('--build',$Build,'--parallel','--verbose') -Log (Join-Path $Build 'build.txt')
}

try {
    Write-Host '=== R2 FINAL CONTROL / CT-W2 VERIFICATION ==='
    Set-Location -LiteralPath $Repo

    foreach ($tool in @($cmake,$ctest,$Ninja,$objcopy,$nm,$sizeTool)) {
        if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
            throw ('Required tool not found: {0}' -f $tool)
        }
    }

    $head = @(Git-Lines -Arguments @('rev-parse','HEAD'))[0]
    $origin = @(Git-Lines -Arguments @('rev-parse','origin/main'))[0]
    if (($head -ne $expectedHead) -or ($origin -ne $expectedHead)) {
        throw ('Expected HEAD/origin at {0}; got HEAD={1}, origin={2}' -f $expectedHead,$head,$origin)
    }
    if (@(Git-Lines -Arguments @('tag','--list','r2-pass')).Count -ne 0) {
        throw 'r2-pass exists unexpectedly.'
    }
    if (@(Git-Lines -Arguments @('rev-parse','r1-pass^{commit}'))[0] -ne
        '5ebf62e90b31e262f44013afb594a430061f139a') {
        throw 'r1-pass gate mismatch.'
    }

    $protected = @{
        'firmware\acquisition\r2_w6_matrix.c' = '7C0F7DEFFCC885965A926B7A3737F684E1D7D951C4D44A96FF33B60CF8930B4F'
        'firmware\acquisition\r2_w6_matrix.h' = '3AE0544E05E9DA0E2048B17D71B0259E9BA719BA70C9715D90215B087AC5716A'
        'firmware\acquisition\r2_w6_guard.h' = '5AA5A03435B1E0A4ABEDAB7F04D63AD696E4CC751CA25A2D36B408E4075552C4'
        'firmware\acquisition\r2_w5_capacity.c' = 'F40FFB8651F17735022DB29E0958836ECAF7A71BF78FF857B3991398EA440C54'
        'firmware\acquisition\r2_w4_roundtrip.c' = '546F237AF2F38ACAAD41CAB54032D21832D0A666870CB557980CFC28711E1E4D'
        'firmware\acquisition\r2_w3_rebind.c' = '9E47F011EFA845CFE5F76B0D9AE6F7AAE20DF76DF587A91BF5346C5F36D70C84'
        'firmware\acquisition\r2_dma_slots.c' = '39F4FAFF9169E2640EA81269428CB67F0ED7278C699973F662EF4AB745156C51'
        'firmware\buffer_pool\r2_buffer_pool.c' = '64B51573E8CC72F080D54255193DCFC9E9D6D0021F4C68E9783C846EF509837B'
    }
    foreach ($relative in $protected.Keys) {
        $actual = (Get-FileHash -LiteralPath (Join-Path $Repo $relative) -Algorithm SHA256).Hash
        if ($actual -ne $protected[$relative]) {
            throw ('Protected W1-W6 source changed: {0}' -f $relative)
        }
    }
    Write-Host 'Protected W1-W6 source: UNCHANGED'

    $ioc = [System.IO.File]::ReadAllText((Join-Path $Repo 'firmware\cubemx\cubemx.ioc'))
    foreach ($needle in @(
        'Dma.Request1=USART2_TX',
        'Dma.USART2_TX.1.Instance=DMA1_Stream6',
        'Dma.USART2_TX.1.Direction=DMA_MEMORY_TO_PERIPH',
        'NVIC.DMA1_Stream6_IRQn=true\:6\:0',
        'NVIC.USART2_IRQn=true\:6\:0')) {
        if (-not $ioc.Contains($needle)) { throw ('IOC contract missing: {0}' -f $needle) }
    }
    Write-Host 'IOC USART2 RX/TX resource contract: PASS'

    $before = Source-Hashes
    $indexBefore = @(Git-Lines -Arguments @('write-tree'))[0]

    $suffix = [guid]::NewGuid().ToString('N').Substring(0,8)
    $native = Join-Path $Repo ('build\r2-ct-native-' + $suffix)
    $ctOff = Join-Path $Repo ('build\r2-ct-w6off-k8-normal-' + $suffix)
    $ctOn = Join-Path $Repo ('build\r2-ct-w2-k8-normal-' + $suffix)
    foreach ($build in @($native,$ctOff,$ctOn)) { New-Item -ItemType Directory -Path $build | Out-Null }

    Write-Host ''
    Write-Host '=== NATIVE W1-W6 + CT PROTOCOL ==='
    $cl = Initialize-Msvc
    $nativeSource = Join-Path $Repo 'tests\native'
    $nativeConfig = @(
        '-S',$nativeSource,'-B',$native,'-G','Ninja','-DCMAKE_BUILD_TYPE=Debug',
        ('-DCMAKE_MAKE_PROGRAM=' + $Ninja),
        ('-DCMAKE_C_COMPILER:FILEPATH=' + $cl),
        '-DR2_CT_NATIVE_TESTS=ON'
    )
    Run-Logged -Exe $cmake -Arguments $nativeConfig -Log (Join-Path $native 'configure.txt')
    Run-Logged -Exe $cmake -Arguments @('--build',$native,'--parallel','--verbose') -Log (Join-Path $native 'build.txt')
    Run-Logged -Exe $ctest -Arguments @('--test-dir',$native,'--show-only=json-v1') -Log (Join-Path $native 'test-list.json') -Quiet
    $listing = [System.IO.File]::ReadAllText((Join-Path $native 'test-list.json')) | ConvertFrom-Json
    if (@($listing.tests).Count -ne 124) { throw 'Expected 124 tests: sealed 116 plus 8 CT protocol tests.' }
    $ctNames = @($listing.tests | Where-Object { $_.name -like 'r2_ct.*' })
    if ($ctNames.Count -ne 8) { throw 'Expected exactly 8 r2_ct protocol tests.' }
    Run-Logged -Exe $ctest -Arguments @('--test-dir',$native,'--no-tests=error','--output-on-failure','--output-junit',(Join-Path $native 'ctest.xml')) -Log (Join-Path $native 'ctest.txt')
    Write-Host 'Native: 124 / 124 PASS expected gate satisfied'

    Write-Host ''
    Write-Host '=== CT-OFF SEALED W6 IMAGE REGRESSION ==='
    $env:PATH = $ArmBin + ';' + $env:PATH
    Configure-Target -Build $ctOff -Ct 'OFF'
    $offElf = Join-Path $ctOff 'cubemx.elf'
    $offBin = Join-Path $ctOff 'cubemx.bin'
    Run-Logged -Exe $objcopy -Arguments @('-O','binary',$offElf,$offBin) -Log (Join-Path $ctOff 'objcopy.txt') -Quiet
    $anchor = Join-Path $Repo 'docs\evidence\r2\w6\regression\committed-state\anchor-w6-k8-normal.bin'
    if (-not (Test-Path -LiteralPath $anchor -PathType Leaf)) { throw 'Sealed W6 K8 NORMAL anchor missing.' }
    $offHash = (Get-FileHash -LiteralPath $offBin -Algorithm SHA256).Hash
    $anchorHash = (Get-FileHash -LiteralPath $anchor -Algorithm SHA256).Hash
    Write-Host ('Anchor programmed SHA256: {0}' -f $anchorHash)
    Write-Host ('CT-OFF programmed SHA256: {0}' -f $offHash)
    if ($offHash -ne $anchorHash) { throw 'CT-OFF programmed image differs from sealed W6 K8 NORMAL image.' }
    Write-Host 'CT-OFF W6 programmed-byte identity: PASS'

    Write-Host ''
    Write-Host '=== CT-ON K8 NORMAL TARGET BUILD ==='
    Configure-Target -Build $ctOn -Ct 'ON'
    $onElf = Join-Path $ctOn 'cubemx.elf'
    $onBin = Join-Path $ctOn 'cubemx.bin'
    Run-Logged -Exe $objcopy -Arguments @('-O','binary',$onElf,$onBin) -Log (Join-Path $ctOn 'objcopy.txt') -Quiet
    Run-Logged -Exe $nm -Arguments @('-g','--defined-only',$onElf) -Log (Join-Path $ctOn 'symbols.txt') -Quiet
    Run-Logged -Exe $sizeTool -Arguments @($onElf) -Log (Join-Path $ctOn 'size.txt')
    $symbols = [System.IO.File]::ReadAllText((Join-Path $ctOn 'symbols.txt'))
    foreach ($symbol in @(
        'R2_W6_CreateTasks','R2_W6_IrqEnter','R2_W6_IrqExit','g_r2_w6_result',
        'R2_CT_CreateTask','R2_CT_UsartIrqHandler','R2_CT_TxDmaIrqHandler','g_r2_ct_result',
        'DMA1_Stream6_IRQHandler','USART2_IRQHandler')) {
        if ($symbols -notmatch ('(?m)\b' + [regex]::Escape($symbol) + '\r?$')) {
            throw ('Missing CT-W2 target symbol: {0}' -f $symbol)
        }
    }
    foreach ($symbol in @('R2_W5_CreateTasks','R2_W4_CreateTasks','R2_W3_CreateTask','R1_Acquisition_Start')) {
        if ($symbols -match ('(?m)\b' + [regex]::Escape($symbol) + '\r?$')) {
            throw ('Unexpected lower diagnostic profile linked: {0}' -f $symbol)
        }
    }
    $onElfHash = (Get-FileHash -LiteralPath $onElf -Algorithm SHA256).Hash
    $onBinHash = (Get-FileHash -LiteralPath $onBin -Algorithm SHA256).Hash
    [System.IO.File]::WriteAllText((Join-Path $ctOn 'elf.sha256'),$onElfHash + "`n")
    [System.IO.File]::WriteAllText((Join-Path $ctOn 'programmed.sha256'),$onBinHash + "`n")

    $after = Source-Hashes
    if ($before.Count -ne $after.Count) { throw 'Source file set changed during verification.' }
    foreach ($path in $before.Keys) {
        if (-not $after.ContainsKey($path) -or $after[$path] -ne $before[$path]) {
            throw ('Source bytes changed during verification: {0}' -f $path)
        }
    }
    if (@(Git-Lines -Arguments @('write-tree'))[0] -ne $indexBefore) { throw 'Git index changed.' }
    if (@(Git-Lines -Arguments @('rev-parse','HEAD'))[0] -ne $expectedHead) { throw 'HEAD changed.' }

    $report = @(
        'R2 CT-W2 VERIFICATION: PASS',
        ('Source HEAD: {0}' -f $expectedHead),
        'Protected W1-W6 source: unchanged',
        'Native W1-W6 + CT protocol: 124 / 124 PASS',
        'CT protocol tests: 8 / 8 PASS',
        'CT-OFF sealed W6 K8 NORMAL programmed image: byte-identical PASS',
        'CT-ON K8 NORMAL target build: PASS',
        ('CT-ON ELF SHA256: {0}' -f $onElfHash),
        ('CT-ON programmed SHA256: {0}' -f $onBinHash),
        ('Native build: {0}' -f $native),
        ('CT-OFF build: {0}' -f $ctOff),
        ('CT-ON build: {0}' -f $ctOn),
        'Hardware: NOT RUN',
        'No flash/reset/debug/commit/push/tag operation.'
    )
    [System.IO.File]::WriteAllLines((Join-Path $ctOn 'verification-summary.txt'),$report,(New-Object System.Text.UTF8Encoding($false)))
    Write-Host ''
    $report | ForEach-Object { Write-Host $_ }
} catch {
    Write-Host ''
    Write-Host 'R2 CT-W2 VERIFICATION: FAIL'
    Write-Host $_.Exception.Message
    Write-Host 'Preserve generated build directories and return the complete error output.'
    exit 1
} finally {
    Set-Location -LiteralPath $oldLocation.Path
}
