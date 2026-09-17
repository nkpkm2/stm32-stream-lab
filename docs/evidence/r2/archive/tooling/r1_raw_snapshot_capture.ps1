$ErrorActionPreference = 'Stop'

$repo = 'E:\Projects\stm32-stream-lab'
$git = 'C:\Program Files\Git\cmd\git.exe'

$server = 'E:\DevTools\STM32CubeCLT-1.22.0\STLink-gdb-server\bin\ST-LINK_gdbserver.exe'
$gdb = 'E:\DevTools\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi\bin\arm-none-eabi-gdb.exe'
$nm = 'E:\DevTools\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi\bin\arm-none-eabi-nm.exe'
$cubeProgrammerBin = 'E:\DevTools\STM32CubeProgrammer-2.23.0\bin'

$serial = '067AFF545754655087043860'
$port = 61234

$build = Join-Path $repo 'build\r1-soak-01'
$elf = Join-Path $build 'cubemx.elf'
$resultFile = Join-Path $build 'r1-soak-inspection.txt'

$expectedElfHash = '67040A73D2072C569918FA3E3AB9B3F88B52C0901E211665ED99462BF33E6CBA'
$expectedHead = 'ec2cc4244837d54d423ca9e240b50fe0a66509f1'

$dump0 = Join-Path $build 'r1-raw-m0.bin'
$dump1 = Join-Path $build 'r1-raw-m1.bin'

$evidenceDir = Join-Path $repo 'docs\evidence\r1'
$csvPath = Join-Path $evidenceDir 'r1-raw-snapshot.csv'
$metadataPath = Join-Path $evidenceDir 'r1-raw-snapshot-metadata.txt'
$soakEvidencePath = Join-Path $evidenceDir 'r1-soak-result-01.txt'

$gdbScript = Join-Path $build 'r1-raw-snapshot.gdb'
$gdbOut = Join-Path $build 'r1-raw-snapshot-gdb.txt'
$gdbErr = Join-Path $build 'r1-raw-snapshot-gdb.stderr.txt'
$serverOut = Join-Path $build 'r1-raw-snapshot-gdbserver.stdout.txt'
$serverErr = Join-Path $build 'r1-raw-snapshot-gdbserver.stderr.txt'

$serverProc = $null

Set-Location -LiteralPath $repo

Write-Output '=== R1 RAW SNAPSHOT CAPTURE ==='

try {
    # -------------------------------------------------------------------------
    # 1. Preconditions
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 1. Preconditions ==='

    $head = (& $git rev-parse HEAD).Trim()

    if ($head -ne $expectedHead) {
        throw "Unexpected HEAD: $head"
    }

    $expectedChanges = @(
        'firmware/acquisition/r1_bringup.c',
        'firmware/acquisition/r1_bringup.h'
    ) | Sort-Object

    $stateBefore = @(& $git status --porcelain=v1 --untracked-files=all)

    $actualChanges = @(
        $stateBefore |
        ForEach-Object {
            if ($_.Length -ge 4) {
                $_.Substring(3)
            }
        }
    ) | Sort-Object

    $scopeDiff = Compare-Object `
        -ReferenceObject $expectedChanges `
        -DifferenceObject $actualChanges

    if ($scopeDiff) {
        $scopeDiff | Format-Table
        throw 'Repository source state is not the tested soak-harness state.'
    }

    foreach ($path in @($server, $gdb, $nm, $elf, $resultFile)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required file is missing: $path"
        }
    }

    $hash = (
        Get-FileHash `
            -LiteralPath $elf `
            -Algorithm SHA256
    ).Hash

    if ($hash -ne $expectedElfHash) {
        throw "Formal-soak ELF SHA256 mismatch: $hash"
    }

    $resultText = [System.IO.File]::ReadAllText($resultFile)

    foreach ($required in @(
        'soak_pass=1',
        'phase=5',
        'elapsed_ms=600000',
        'actual_tc_count=468749',
        'ct_mismatch_count=0',
        'alternation_mismatch_count=0',
        'suspected_event_loss_count=0',
        'adc_ovr_count=0',
        'dma_te_count=0',
        'dma_dme_count=0',
        'dma_fe_count=0'
    )) {
        if (-not $resultText.Contains($required)) {
            throw "Formal-soak result is missing: $required"
        }
    }

    Write-Output "Formal-soak ELF SHA256: $hash"
    Write-Output 'Formal-soak PASS result: verified'
    Write-Output 'Repository source state: verified'

    # -------------------------------------------------------------------------
    # 2. Parse frozen stop state and expected raw ranges
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 2. Frozen stop-state selection ==='

    function Get-DecimalField {
        param([string]$Name)

        $match = [regex]::Match(
            $resultText,
            '(?m)^' + [regex]::Escape($Name) + '=([0-9]+)\r?$'
        )

        if (-not $match.Success) {
            throw "Missing formal-soak result field: $Name"
        }

        return [uint32]$match.Groups[1].Value
    }

    $activeTarget = Get-DecimalField 'stop_active_target'
    $remainingNdtr = Get-DecimalField 'stop_remaining_ndtr'
    $capturedSamples = Get-DecimalField 'stop_captured_samples'

    if ($activeTarget -gt 1) {
        throw "Invalid stop_active_target: $activeTarget"
    }

    $completeTarget = 1 - $activeTarget

    Write-Output "Stopped active target: M$activeTarget"
    Write-Output "Stop remaining NDTR: $remainingNdtr"
    Write-Output "Stop captured samples in active target: $capturedSamples"
    Write-Output "Selected last fully completed target: M$completeTarget"

    # -------------------------------------------------------------------------
    # 3. Verify static buffer symbols exist in debug ELF
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 3. Debug-symbol verification ==='

    $symbols = @(& $nm $elf)

    if ($LASTEXITCODE -ne 0) {
        throw 'nm failed on formal-soak ELF.'
    }

    foreach ($symbol in @('r1_buffer0', 'r1_buffer1')) {
        $matches = @(
            $symbols |
            Where-Object {
                $_ -match ("\b{0}$" -f [regex]::Escape($symbol))
            }
        )

        if ($matches.Count -ne 1) {
            throw "Expected one debug/linker symbol for $symbol, found: $($matches.Count)"
        }

        Write-Output "PASS  $symbol"
    }

    # -------------------------------------------------------------------------
    # 4. Attach without reset and dump both fixed buffers
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 4. Attach and dump stopped buffers ==='

    $existingServer = @(
        Get-Process -Name 'ST-LINK_gdbserver' -ErrorAction SilentlyContinue
    )

    if ($existingServer.Count -ne 0) {
        throw 'An ST-LINK GDB server is already running.'
    }

    $dump0Gdb = $dump0.Replace('\', '/')
    $dump1Gdb = $dump1.Replace('\', '/')

    $commands = @(
        'set pagination off',
        'set confirm off',
        ('target remote 127.0.0.1:{0}' -f $port),
        'printf "buffer0_address=0x%08x\n", (unsigned int)&r1_buffer0[0]',
        'printf "buffer1_address=0x%08x\n", (unsigned int)&r1_buffer1[0]',
        ('dump binary memory "{0}" &r1_buffer0[0] &r1_buffer0[256]' -f $dump0Gdb),
        ('dump binary memory "{0}" &r1_buffer1[0] &r1_buffer1[256]' -f $dump1Gdb),
        'detach',
        'quit'
    )

    $enc = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllLines($gdbScript, $commands, $enc)

    $serverArgs = @(
        '-d',
        '-m', '0',
        '-p', "$port",
        '-l', '1',
        '-i', $serial,
        '-cp', $cubeProgrammerBin,
        '-g'
    )

    $serverProc = Start-Process `
        -FilePath $server `
        -ArgumentList $serverArgs `
        -PassThru `
        -NoNewWindow `
        -RedirectStandardOutput $serverOut `
        -RedirectStandardError $serverErr

    Start-Sleep -Seconds 2
    $serverProc.Refresh()

    if ($serverProc.HasExited) {
        if (Test-Path $serverOut) {
            Get-Content $serverOut | Select-Object -Last 80
        }

        if (Test-Path $serverErr) {
            Get-Content $serverErr | Select-Object -Last 80
        }

        throw "ST-LINK GDB server exited early with code $($serverProc.ExitCode)."
    }

    $gdbProc = Start-Process `
        -FilePath $gdb `
        -ArgumentList @('-q', '-batch', '-x', $gdbScript, $elf) `
        -Wait `
        -PassThru `
        -NoNewWindow `
        -RedirectStandardOutput $gdbOut `
        -RedirectStandardError $gdbErr

    $gdbProc.Refresh()

    if ($gdbProc.ExitCode -ne 0) {
        if (Test-Path $gdbOut) {
            Get-Content $gdbOut
        }

        if (Test-Path $gdbErr) {
            Get-Content $gdbErr
        }

        throw 'GDB raw-buffer dump failed.'
    }

    foreach ($path in @($dump0, $dump1)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Raw binary dump is missing: $path"
        }

        $length = (Get-Item -LiteralPath $path).Length

        if ($length -ne 512) {
            throw "Raw binary dump size is not 512 bytes: $path ($length bytes)"
        }
    }

    Write-Output 'M0 binary dump: 512 bytes'
    Write-Output 'M1 binary dump: 512 bytes'

    # -------------------------------------------------------------------------
    # 5. Convert binary dumps to uint16 samples
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 5. Convert stopped buffers ==='

    function Read-Uint16Samples {
        param([string]$Path)

        $bytes = [System.IO.File]::ReadAllBytes($Path)

        if ($bytes.Length -ne 512) {
            throw "Unexpected raw-buffer size: $Path"
        }

        $samples = New-Object 'System.UInt16[]' 256

        for ($index = 0; $index -lt 256; $index++) {
            $low = [uint16]$bytes[$index * 2]
            $high = [uint16]$bytes[($index * 2) + 1]
            $samples[$index] = [uint16]($low -bor ($high -shl 8))
        }

        return $samples
    }

    $samples0 = Read-Uint16Samples $dump0
    $samples1 = Read-Uint16Samples $dump1

    if ($completeTarget -eq 0) {
        $selected = $samples0
        $expectedMin = Get-DecimalField 'raw0_min'
        $expectedMax = Get-DecimalField 'raw0_max'
    }
    else {
        $selected = $samples1
        $expectedMin = Get-DecimalField 'raw1_min'
        $expectedMax = Get-DecimalField 'raw1_max'
    }

    $actualMin = [uint32]($selected | Measure-Object -Minimum).Minimum
    $actualMax = [uint32]($selected | Measure-Object -Maximum).Maximum

    if ($actualMin -ne $expectedMin -or $actualMax -ne $expectedMax) {
        throw (
            "Selected complete-buffer range changed. " +
            "Expected $expectedMin..$expectedMax, observed $actualMin..$actualMax."
        )
    }

    foreach ($sample in $selected) {
        if ($sample -gt 4095) {
            throw "Raw sample exceeds 12-bit range: $sample"
        }
    }

    Write-Output "Complete-buffer range: $actualMin .. $actualMax"
    Write-Output 'Complete-buffer sample count: 256'

    # -------------------------------------------------------------------------
    # 6. Preserve formal evidence in repository
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 6. Preserve formal R1 evidence ==='

    if (-not (Test-Path -LiteralPath $evidenceDir -PathType Container)) {
        New-Item -ItemType Directory -Path $evidenceDir | Out-Null
    }

    $csvLines = New-Object System.Collections.Generic.List[string]
    $csvLines.Add('sample_index,adc_raw')

    for ($index = 0; $index -lt 256; $index++) {
        $csvLines.Add(('{0},{1}' -f $index, $selected[$index]))
    }

    [System.IO.File]::WriteAllLines($csvPath, $csvLines, $enc)

    $metadata = @(
        'R1 raw snapshot metadata',
        '',
        'Input condition: PA0 / ADC1_IN0 connected to GND',
        'Target configuration: 200 kS/s, N=256, fixed M0/M1 DBM',
        'Formal soak duration: 600000 ms',
        ('Formal soak ELF SHA256: {0}' -f $expectedElfHash),
        ('Stopped active DMA target: M{0}' -f $activeTarget),
        ('Selected fully completed DMA target: M{0}' -f $completeTarget),
        ('Stop remaining NDTR: {0}' -f $remainingNdtr),
        ('Stop captured samples in active target: {0}' -f $capturedSamples),
        ('Snapshot sample count: {0}' -f $selected.Count),
        ('Snapshot minimum raw value: {0}' -f $actualMin),
        ('Snapshot maximum raw value: {0}' -f $actualMax),
        '',
        'The CSV contains the last fully completed fixed DMA target after the formal soak stop.',
        'The active partial DMA target was intentionally not used as the complete raw snapshot.'
    )

    [System.IO.File]::WriteAllLines($metadataPath, $metadata, $enc)

    Copy-Item `
        -LiteralPath $resultFile `
        -Destination $soakEvidencePath `
        -Force

    # -------------------------------------------------------------------------
    # 7. Verify evidence files
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 7. Evidence verification ==='

    $csvRead = [System.IO.File]::ReadAllLines($csvPath)

    if ($csvRead.Count -ne 257) {
        throw "Raw snapshot CSV line count is wrong: $($csvRead.Count)"
    }

    if ($csvRead[0] -ne 'sample_index,adc_raw') {
        throw 'Raw snapshot CSV header is wrong.'
    }

    $metadataRead = [System.IO.File]::ReadAllText($metadataPath)

    if (-not $metadataRead.Contains(
        'The CSV contains the last fully completed fixed DMA target')) {
        throw 'Raw snapshot metadata verification failed.'
    }

    $soakEvidenceRead = [System.IO.File]::ReadAllText($soakEvidencePath)

    if (-not $soakEvidenceRead.Contains('soak_pass=1')) {
        throw 'Preserved formal-soak result is not a passing result.'
    }

    # -------------------------------------------------------------------------
    # 8. Repository scope
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 8. Repository scope ==='

    $stateAfter = @(& $git status --porcelain=v1 --untracked-files=all)

    $actualPaths = @(
        $stateAfter |
        ForEach-Object {
            if ($_.Length -ge 4) {
                $_.Substring(3)
            }
        }
    ) | Sort-Object

    $expectedPaths = @(
        'firmware/acquisition/r1_bringup.c',
        'firmware/acquisition/r1_bringup.h',
        'docs/evidence/r1/r1-raw-snapshot.csv',
        'docs/evidence/r1/r1-raw-snapshot-metadata.txt',
        'docs/evidence/r1/r1-soak-result-01.txt'
    ) | Sort-Object

    $finalScopeDiff = Compare-Object `
        -ReferenceObject $expectedPaths `
        -DifferenceObject $actualPaths

    if ($finalScopeDiff) {
        $finalScopeDiff | Format-Table
        throw 'Repository change scope after evidence capture is unexpected.'
    }

    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R1 RAW SNAPSHOT CAPTURE: PASS'
    Write-Output '============================================================'
    Write-Output "Formal-soak ELF SHA256: $expectedElfHash"
    Write-Output "Stopped active target: M$activeTarget"
    Write-Output "Selected complete target: M$completeTarget"
    Write-Output 'Complete raw snapshot samples: 256'
    Write-Output "Raw range: $actualMin .. $actualMax"
    Write-Output 'Formal soak result: preserved'
    Write-Output 'Repository evidence files: created'
    Write-Output ''
    Write-Output 'No reset, flash, commit, or push was performed.'
}
catch {
    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R1 RAW SNAPSHOT CAPTURE: FAIL'
    Write-Output '============================================================'
    Write-Output $_.Exception.Message
    Write-Output ''
    Write-Output 'Do not reset or modify firmware yet.'
    exit 1
}
finally {
    if ($null -ne $serverProc) {
        try {
            $serverProc.Refresh()

            if (-not $serverProc.HasExited) {
                Stop-Process `
                    -Id $serverProc.Id `
                    -Force `
                    -ErrorAction SilentlyContinue
            }
        }
        catch {
        }
    }
}
