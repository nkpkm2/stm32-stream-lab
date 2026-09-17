$ErrorActionPreference = 'Stop'

$repo = 'E:\Projects\stm32-stream-lab'
$git = 'C:\Program Files\Git\cmd\git.exe'

$server = 'E:\DevTools\STM32CubeCLT-1.22.0\STLink-gdb-server\bin\ST-LINK_gdbserver.exe'
$gdb = 'E:\DevTools\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi\bin\arm-none-eabi-gdb.exe'
$cubeProgrammerBin = 'E:\DevTools\STM32CubeProgrammer-2.23.0\bin'

$serial = '067AFF545754655087043860'
$port = 61234

$build = Join-Path $repo 'build\r1-soak-01'
$elf = Join-Path $build 'cubemx.elf'
$resultFile = Join-Path $build 'r1-soak-inspection.txt'

$expectedElfHash = '67040A73D2072C569918FA3E3AB9B3F88B52C0901E211665ED99462BF33E6CBA'
$expectedHead = 'ec2cc4244837d54d423ca9e240b50fe0a66509f1'

$gdbScript = Join-Path $build 'r1-raw-snapshot-text.gdb'
$gdbOut = Join-Path $build 'r1-raw-snapshot-text.txt'
$gdbErr = Join-Path $build 'r1-raw-snapshot-text.stderr.txt'
$serverOut = Join-Path $build 'r1-raw-snapshot-recovery-gdbserver.stdout.txt'
$serverErr = Join-Path $build 'r1-raw-snapshot-recovery-gdbserver.stderr.txt'

$evidenceDir = Join-Path $repo 'docs\evidence\r1'
$csvPath = Join-Path $evidenceDir 'r1-raw-snapshot.csv'
$metadataPath = Join-Path $evidenceDir 'r1-raw-snapshot-metadata.txt'
$soakEvidencePath = Join-Path $evidenceDir 'r1-soak-result-01.txt'

$serverProc = $null

Set-Location -LiteralPath $repo

Write-Output '=== R1 RAW SNAPSHOT RECOVERY CAPTURE ==='

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

    foreach ($path in @($server, $gdb, $elf, $resultFile)) {
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

    $expectedChanges = @(
        'firmware/acquisition/r1_bringup.c',
        'firmware/acquisition/r1_bringup.h'
    ) | Sort-Object

    $stateBefore = @(
        & $git status --porcelain=v1 --untracked-files=all
    )

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
        throw 'Repository source state is not the tested formal-soak state.'
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
    # 2. Parse formal stop state and select fully completed DMA target
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 2. Select fully completed stopped buffer ==='

    function Get-DecimalField {
        param([string]$Name)

        $match = [regex]::Match(
            $resultText,
            '(?m)^' + [regex]::Escape($Name) + '=([0-9]+)\r?$'
        )

        if (-not $match.Success) {
            throw "Missing formal-soak field: $Name"
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

    if ($completeTarget -eq 0) {
        $bufferSymbol = 'r1_buffer0'
        $expectedMin = Get-DecimalField 'raw0_min'
        $expectedMax = Get-DecimalField 'raw0_max'
    }
    else {
        $bufferSymbol = 'r1_buffer1'
        $expectedMin = Get-DecimalField 'raw1_min'
        $expectedMax = Get-DecimalField 'raw1_max'
    }

    Write-Output "Stopped active target: M$activeTarget"
    Write-Output "Stop remaining NDTR: $remainingNdtr"
    Write-Output "Stop captured samples: $capturedSamples"
    Write-Output "Selected complete target: M$completeTarget"
    Write-Output "Selected debug symbol: $bufferSymbol"
    Write-Output "Expected frozen range: $expectedMin .. $expectedMax"

    # -------------------------------------------------------------------------
    # 3. Start attach-mode GDB server
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 3. Start attach-mode debugger ==='

    $existingServer = @(
        Get-Process -Name 'ST-LINK_gdbserver' -ErrorAction SilentlyContinue
    )

    if ($existingServer.Count -ne 0) {
        throw 'An ST-LINK GDB server is already running.'
    }

    $commands = New-Object System.Collections.Generic.List[string]

    $commands.Add('set pagination off')
    $commands.Add('set confirm off')
    $commands.Add(('target remote 127.0.0.1:{0}' -f $port))
    $commands.Add('set $i = 0')
    $commands.Add('printf "SNAPSHOT_BEGIN\n"')
    $commands.Add('while $i < 256')
    $commands.Add(
        ('  printf "sample_%03u=%u\n", (unsigned int)$i, (unsigned int){0}[$i]' -f
            $bufferSymbol)
    )
    $commands.Add('  set $i = $i + 1')
    $commands.Add('end')
    $commands.Add('printf "SNAPSHOT_END\n"')
    $commands.Add('detach')
    $commands.Add('quit')

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

    Write-Output 'Attach-mode GDB server: running'

    # -------------------------------------------------------------------------
    # 4. Capture 256 samples as text
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 4. Capture 256 samples ==='

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
            Get-Content $gdbOut | Select-Object -Last 80
        }

        if (Test-Path $gdbErr) {
            Get-Content $gdbErr | Select-Object -Last 80
        }

        throw 'GDB textual raw-buffer capture failed.'
    }

    $captureText = [System.IO.File]::ReadAllText($gdbOut)

    $sampleMatches = [regex]::Matches(
        $captureText,
        '(?m)^sample_([0-9]{3})=([0-9]+)\r?$'
    )

    if ($sampleMatches.Count -ne 256) {
        throw "Expected 256 raw samples, captured: $($sampleMatches.Count)"
    }

    $samples = New-Object 'System.UInt16[]' 256

    for ($index = 0; $index -lt 256; $index++) {
        $match = $sampleMatches[$index]
        $parsedIndex = [int]$match.Groups[1].Value
        $value = [uint32]$match.Groups[2].Value

        if ($parsedIndex -ne $index) {
            throw "Raw sample index mismatch at position $index."
        }

        if ($value -gt 4095) {
            throw "Raw ADC sample exceeds 12-bit range at index ${index}: $value"
        }

        $samples[$index] = [uint16]$value
    }

    $actualMin = [uint32]($samples | Measure-Object -Minimum).Minimum
    $actualMax = [uint32]($samples | Measure-Object -Maximum).Maximum

    if ($actualMin -ne $expectedMin -or $actualMax -ne $expectedMax) {
        throw (
            "Frozen complete-buffer range mismatch. " +
            "Expected $expectedMin..$expectedMax, observed $actualMin..$actualMax."
        )
    }

    Write-Output 'Raw sample count: 256'
    Write-Output "Observed raw range: $actualMin .. $actualMax"

    # -------------------------------------------------------------------------
    # 5. Preserve formal repository evidence
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 5. Preserve formal repository evidence ==='

    if (-not (Test-Path -LiteralPath $evidenceDir -PathType Container)) {
        New-Item -ItemType Directory -Path $evidenceDir | Out-Null
    }

    $csvLines = New-Object System.Collections.Generic.List[string]
    $csvLines.Add('sample_index,adc_raw')

    for ($index = 0; $index -lt 256; $index++) {
        $csvLines.Add(('{0},{1}' -f $index, $samples[$index]))
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
        ('Snapshot sample count: {0}' -f $samples.Count),
        ('Snapshot minimum raw value: {0}' -f $actualMin),
        ('Snapshot maximum raw value: {0}' -f $actualMax),
        '',
        'The CSV contains the last fully completed fixed DMA target after the formal soak stop.',
        'The active partial DMA target was intentionally excluded from the complete raw snapshot.'
    )

    [System.IO.File]::WriteAllLines($metadataPath, $metadata, $enc)

    Copy-Item `
        -LiteralPath $resultFile `
        -Destination $soakEvidencePath `
        -Force

    $csvHash = (
        Get-FileHash `
            -LiteralPath $csvPath `
            -Algorithm SHA256
    ).Hash

    # -------------------------------------------------------------------------
    # 6. Verify repository evidence
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 6. Evidence verification ==='

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
    # 7. Repository scope
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 7. Repository scope ==='

    $stateAfter = @(
        & $git status --porcelain=v1 --untracked-files=all
    )

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
    Write-Output 'R1 RAW SNAPSHOT RECOVERY CAPTURE: PASS'
    Write-Output '============================================================'
    Write-Output "Formal-soak ELF SHA256: $expectedElfHash"
    Write-Output "Stopped active target: M$activeTarget"
    Write-Output "Selected complete target: M$completeTarget"
    Write-Output 'Complete raw snapshot samples: 256'
    Write-Output "Raw range: $actualMin .. $actualMax"
    Write-Output "CSV SHA256: $csvHash"
    Write-Output 'Formal soak result: preserved'
    Write-Output 'Repository evidence files: created'
    Write-Output ''
    Write-Output 'No reset, flash, commit, or push was performed.'
}
catch {
    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R1 RAW SNAPSHOT RECOVERY CAPTURE: FAIL'
    Write-Output '============================================================'
    Write-Output $_.Exception.Message
    Write-Output ''
    Write-Output 'Do not reset or power-cycle the board.'
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
