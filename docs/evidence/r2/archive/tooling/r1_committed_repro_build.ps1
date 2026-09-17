$ErrorActionPreference = 'Stop'

$repo = 'E:\Projects\stm32-stream-lab'
$git = 'C:\Program Files\Git\cmd\git.exe'

$cmake = 'E:\DevTools\cmake-3.31.12-windows-x86_64\bin\cmake.exe'
$ninja = 'E:\DevTools\ninja-1.13.1-windows-x86_64\ninja.exe'
$armBin = 'E:\DevTools\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi\bin'

$source = Join-Path $repo 'firmware\cubemx'
$toolchain = Join-Path $source 'cmake\gcc-arm-none-eabi.cmake'
$build = Join-Path $repo 'build\r1-soak-01'
$elf = Join-Path $build 'cubemx.elf'

$expectedHead = '5ebf62e90b31e262f44013afb594a430061f139a'
$expectedElfHash = '67040A73D2072C569918FA3E3AB9B3F88B52C0901E211665ED99462BF33E6CBA'
$expectedRawCsvHash = 'B01105298CDA8782E74D5E6702B5716293C23978D9AE6A9AF8567AF6B9DE1400'

$rawCsv = Join-Path $repo 'docs\evidence\r1\r1-raw-snapshot.csv'
$soakEvidence = Join-Path $repo 'docs\evidence\r1\r1-soak-result-01.txt'
$manifest = Join-Path $repo 'docs\evidence\r1\r1-tested-builds.txt'

Set-Location -LiteralPath $repo

Write-Output '=== R1 COMMITTED-STATE REPRODUCIBILITY BUILD ==='

try {
    # -------------------------------------------------------------------------
    # 1. Repository guard
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 1. Repository guard ==='

    $head = (& $git rev-parse HEAD).Trim()

    if ($head -ne $expectedHead) {
        throw "Unexpected HEAD: $head"
    }

    $status = @(& $git status --porcelain=v1 --untracked-files=all)

    if ($status.Count -ne 0) {
        $status
        throw 'Working tree is not clean.'
    }

    $localHead = (& $git rev-parse HEAD).Trim()
    $remoteHead = (& $git rev-parse origin/main).Trim()

    if ($localHead -ne $remoteHead) {
        throw 'main and origin/main are not synchronized.'
    }

    $localR1Pass = @(& $git tag --list 'r1-pass')

    if ($localR1Pass.Count -ne 0) {
        throw 'Local r1-pass tag exists unexpectedly.'
    }

    Write-Output "HEAD: $head"
    Write-Output 'Working tree: clean'
    Write-Output 'main / origin/main: synchronized'
    Write-Output 'r1-pass: NOT CREATED'

    # -------------------------------------------------------------------------
    # 2. Evidence guard
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 2. Evidence guard ==='

    foreach ($path in @($rawCsv, $soakEvidence, $manifest, $elf)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required artifact is missing: $path"
        }
    }

    $rawHash = (
        Get-FileHash `
            -LiteralPath $rawCsv `
            -Algorithm SHA256
    ).Hash

    if ($rawHash -ne $expectedRawCsvHash) {
        throw "Raw snapshot evidence hash mismatch: $rawHash"
    }

    $soakText = [System.IO.File]::ReadAllText($soakEvidence)

    foreach ($required in @(
        'soak_pass=1',
        'elapsed_ms=600000',
        'actual_tc_count=468749',
        'ct_mismatch_count=0',
        'alternation_mismatch_count=0',
        'suspected_event_loss_count=0',
        'adc_ovr_count=0'
    )) {
        if (-not $soakText.Contains($required)) {
            throw "Committed formal-soak evidence is missing: $required"
        }
    }

    $preBuildHash = (
        Get-FileHash `
            -LiteralPath $elf `
            -Algorithm SHA256
    ).Hash

    if ($preBuildHash -ne $expectedElfHash) {
        throw "Existing tested ELF hash mismatch before rebuild: $preBuildHash"
    }

    Write-Output "Committed raw CSV SHA256: $rawHash"
    Write-Output "Existing tested ELF SHA256: $preBuildHash"
    Write-Output 'Committed formal-soak evidence: verified'

    # -------------------------------------------------------------------------
    # 3. Tool checks
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 3. Tool checks ==='

    foreach ($path in @($cmake, $ninja, $toolchain)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required build tool/file is missing: $path"
        }
    }

    if (-not (Test-Path -LiteralPath $armBin -PathType Container)) {
        throw 'Arm GNU toolchain directory is missing.'
    }

    $env:PATH = "$armBin;$(Split-Path -Parent $ninja);$env:PATH"

    Write-Output 'Build tools: verified'

    # -------------------------------------------------------------------------
    # 4. Fresh reconfigure in the exact tested build path
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 4. Fresh reconfigure ==='

    $configureOut = Join-Path $build 'committed-rebuild.configure.stdout.txt'
    $configureErr = Join-Path $build 'committed-rebuild.configure.stderr.txt'

    $configureArgs = @(
        '--fresh',
        '-S', $source,
        '-B', $build,
        '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Debug',
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
        "-DCMAKE_MAKE_PROGRAM=$ninja"
    )

    $p = Start-Process `
        -FilePath $cmake `
        -ArgumentList $configureArgs `
        -Wait `
        -PassThru `
        -NoNewWindow `
        -RedirectStandardOutput $configureOut `
        -RedirectStandardError $configureErr

    $p.Refresh()

    if ($p.ExitCode -ne 0) {
        if (Test-Path -LiteralPath $configureOut) {
            Get-Content $configureOut | Select-Object -Last 80
        }

        if (Test-Path -LiteralPath $configureErr) {
            Get-Content $configureErr | Select-Object -Last 80
        }

        throw 'Fresh committed-state configure failed.'
    }

    Write-Output 'Fresh configure: PASS'

    # -------------------------------------------------------------------------
    # 5. Clean committed-state rebuild
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 5. Clean committed-state rebuild ==='

    $buildOut = Join-Path $build 'committed-rebuild.build.stdout.txt'
    $buildErr = Join-Path $build 'committed-rebuild.build.stderr.txt'

    $p = Start-Process `
        -FilePath $cmake `
        -ArgumentList @('--build', $build, '--clean-first', '--parallel') `
        -Wait `
        -PassThru `
        -NoNewWindow `
        -RedirectStandardOutput $buildOut `
        -RedirectStandardError $buildErr

    $p.Refresh()

    if ($p.ExitCode -ne 0) {
        if (Test-Path -LiteralPath $buildOut) {
            Get-Content $buildOut | Select-Object -Last 100
        }

        if (Test-Path -LiteralPath $buildErr) {
            Get-Content $buildErr | Select-Object -Last 100
        }

        throw 'Committed-state rebuild failed.'
    }

    $warnings = @(
        Select-String `
            -Path $buildOut,$buildErr `
            -Pattern @(
                'r1_acquisition.*warning:',
                'warning:.*r1_acquisition',
                'r1_bringup.*warning:',
                'warning:.*r1_bringup'
            )
    )

    if ($warnings.Count -ne 0) {
        $warnings
        throw 'Compiler warnings were emitted for R1 sources.'
    }

    if (-not (Test-Path -LiteralPath $elf -PathType Leaf)) {
        throw 'Committed-state rebuild did not produce the expected ELF.'
    }

    $rebuiltHash = (
        Get-FileHash `
            -LiteralPath $elf `
            -Algorithm SHA256
    ).Hash

    Write-Output "Rebuilt ELF SHA256: $rebuiltHash"

    if ($rebuiltHash -ne $expectedElfHash) {
        throw (
            "Committed-state rebuild is not byte-identical to the hardware-tested ELF. " +
            "Expected $expectedElfHash, observed $rebuiltHash"
        )
    }

    Write-Output 'Byte-identical tested ELF reproduction: PASS'
    Write-Output 'R1 compiler warnings: 0'

    # -------------------------------------------------------------------------
    # 6. Repository state after rebuild
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 6. Repository state after rebuild ==='

    $statusAfter = @(& $git status --porcelain=v1 --untracked-files=all)

    if ($statusAfter.Count -ne 0) {
        $statusAfter
        throw 'Build changed repository source state.'
    }

    $headAfter = (& $git rev-parse HEAD).Trim()

    if ($headAfter -ne $expectedHead) {
        throw "HEAD changed unexpectedly: $headAfter"
    }

    Write-Output 'Repository source state: unchanged'
    Write-Output 'Working tree: clean'

    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R1 COMMITTED-STATE REPRODUCIBILITY BUILD: PASS'
    Write-Output '============================================================'
    Write-Output "Committed HEAD: $expectedHead"
    Write-Output "Hardware-tested ELF SHA256: $expectedElfHash"
    Write-Output "Committed-state rebuilt ELF SHA256: $rebuiltHash"
    Write-Output 'Byte-identical reproduction: PASS'
    Write-Output 'Fresh configure/build: PASS'
    Write-Output 'R1 compiler warnings: 0'
    Write-Output 'Committed evidence guard: PASS'
    Write-Output 'Working tree: clean'
    Write-Output 'r1-pass: NOT CREATED'
    Write-Output ''
    Write-Output 'No flash, reset, commit, or push was performed.'
}
catch {
    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R1 COMMITTED-STATE REPRODUCIBILITY BUILD: FAIL'
    Write-Output '============================================================'
    Write-Output $_.Exception.Message
    Write-Output ''
    Write-Output 'Do not proceed to final hardware regression yet.'
    exit 1
}
