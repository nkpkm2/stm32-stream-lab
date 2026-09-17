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
$gdbScript = Join-Path $build 'r1-soak-inspection.gdb'
$gdbOut = Join-Path $build 'r1-soak-inspection.txt'
$gdbErr = Join-Path $build 'r1-soak-inspection.stderr.txt'
$serverOut = Join-Path $build 'r1-soak-gdbserver.stdout.txt'
$serverErr = Join-Path $build 'r1-soak-gdbserver.stderr.txt'

$serverProc = $null

Set-Location -LiteralPath $repo

Write-Output '=== R1 FORMAL SOAK RESULT CAPTURE ==='

try {
    # -------------------------------------------------------------------------
    # 1. Preconditions
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 1. Preconditions ==='

    foreach ($path in @($server, $gdb, $elf)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required file is missing: $path"
        }
    }

    $existingServer = @(
        Get-Process -Name 'ST-LINK_gdbserver' -ErrorAction SilentlyContinue
    )

    if ($existingServer.Count -ne 0) {
        throw 'An ST-LINK GDB server is already running.'
    }

    $hash = (
        Get-FileHash `
            -LiteralPath $elf `
            -Algorithm SHA256
    ).Hash

    Write-Output "ELF SHA256: $hash"
    Write-Output 'Existing GDB server: none'

    # -------------------------------------------------------------------------
    # 2. GDB command file
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 2. Create inspection script ==='

    $fields = @(
        'task_created',
        'phase',
        'soak_pass',
        'start_status',
        'stop_status',
        'start_tick',
        'stop_tick',
        'elapsed_ticks',
        'elapsed_ms',
        'expected_tc_count',
        'actual_tc_count',
        'tc_count_error',
        'observed_block_rate_millihz',
        'expected_block_rate_millihz',
        'dbm_bit_seen',
        'start_ndtr',
        'start_ct',
        'm0ar_matches_buffer0',
        'm1ar_matches_buffer1',
        'adc_dma_bit_seen',
        'adc_dds_bit_seen',
        'tim2_running_after_start',
        'm0_complete_count',
        'm1_complete_count',
        'ct_mismatch_count',
        'alternation_mismatch_count',
        'suspected_event_loss_count',
        'adc_ovr_count',
        'dma_te_count',
        'dma_dme_count',
        'dma_fe_count',
        'dma_other_error_count',
        'timing_interval_count',
        'mean_delta_cycles',
        'min_delta_cycles',
        'max_delta_cycles',
        'mean_delta_error_cycles',
        'trace_count',
        'stop_remaining_ndtr',
        'stop_captured_samples',
        'stop_active_target',
        'partial_stop_count',
        'stop_artifact_count',
        'quiet_tc_count',
        'quiet_stop_artifact_count',
        'quiet_state',
        'raw0_min',
        'raw0_max',
        'raw1_min',
        'raw1_max',
        'system_core_clock',
        'aircr',
        'hal_tick_start',
        'hal_tick_stop',
        'hal_tick_delta'
    )

    $commands = New-Object System.Collections.Generic.List[string]

    $commands.Add('set pagination off')
    $commands.Add('set confirm off')
    $commands.Add('set print pretty off')
    $commands.Add(('target remote 127.0.0.1:{0}' -f $port))
    $commands.Add('printf "magic=0x%08x\n", (unsigned int)g_r1_soak_result.magic')

    foreach ($field in $fields) {
        if ($field -eq 'tc_count_error') {
            $commands.Add(
                ('printf "{0}=%d\n", (int)g_r1_soak_result.{0}' -f $field)
            )
        }
        elseif ($field -eq 'aircr') {
            $commands.Add(
                ('printf "{0}=0x%08x\n", (unsigned int)g_r1_soak_result.{0}' -f $field)
            )
        }
        else {
            $commands.Add(
                ('printf "{0}=%u\n", (unsigned int)g_r1_soak_result.{0}' -f $field)
            )
        }
    }

    $commands.Add('detach')
    $commands.Add('quit')

    $enc = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllLines($gdbScript, $commands, $enc)

    Write-Output "GDB command file: $gdbScript"

    # -------------------------------------------------------------------------
    # 3. Attach-mode server
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 3. Start attach-mode GDB server ==='

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
    # 4. Capture
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 4. Read frozen formal-soak result ==='

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

        throw 'GDB formal-soak inspection failed.'
    }

    $text = [System.IO.File]::ReadAllText($gdbOut)

    # -------------------------------------------------------------------------
    # 5. Parse helpers
    # -------------------------------------------------------------------------
    function Get-DecimalField {
        param([string]$Name)

        $match = [regex]::Match(
            $text,
            '(?m)^' + [regex]::Escape($Name) + '=([0-9]+)\r?$'
        )

        if (-not $match.Success) {
            throw "Missing result field: $Name"
        }

        return [uint64]$match.Groups[1].Value
    }

    function Get-SignedField {
        param([string]$Name)

        $match = [regex]::Match(
            $text,
            '(?m)^' + [regex]::Escape($Name) + '=(-?[0-9]+)\r?$'
        )

        if (-not $match.Success) {
            throw "Missing signed result field: $Name"
        }

        return [int64]$match.Groups[1].Value
    }

    function Get-HexField {
        param([string]$Name)

        $match = [regex]::Match(
            $text,
            '(?m)^' + [regex]::Escape($Name) + '=0x([0-9A-Fa-f]+)\r?$'
        )

        if (-not $match.Success) {
            throw "Missing hexadecimal result field: $Name"
        }

        return [Convert]::ToUInt64($match.Groups[1].Value, 16)
    }

    # -------------------------------------------------------------------------
    # 6. Independent formal classification
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 5. Independent formal classification ==='

    if ((Get-HexField 'magic') -ne 0x52315331) {
        throw 'Formal soak magic is invalid.'
    }

    if ((Get-DecimalField 'task_created') -ne 1) {
        throw 'Formal soak task was not created.'
    }

    if ((Get-DecimalField 'phase') -ne 5) {
        throw 'Formal soak harness did not reach COMPLETE.'
    }

    if ((Get-DecimalField 'soak_pass') -ne 1) {
        throw 'Firmware formal-soak classifier did not pass.'
    }

    if ((Get-DecimalField 'start_status') -ne 0 -or
        (Get-DecimalField 'stop_status') -ne 0) {
        throw 'Formal soak start or stop status failed.'
    }

    $elapsed = Get-DecimalField 'elapsed_ms'

    if ($elapsed -lt 600000) {
        throw "Formal soak duration is too short: $elapsed ms"
    }

    foreach ($field in @(
        'dbm_bit_seen',
        'm0ar_matches_buffer0',
        'm1ar_matches_buffer1',
        'adc_dma_bit_seen',
        'adc_dds_bit_seen',
        'tim2_running_after_start'
    )) {
        if ((Get-DecimalField $field) -ne 1) {
            throw "Formal soak startup assertion failed: $field"
        }
    }

    if ((Get-DecimalField 'start_ndtr') -ne 256) {
        throw 'Formal soak did not start with NDTR=256.'
    }

    if ((Get-DecimalField 'start_ct') -ne 0) {
        throw 'Formal soak did not start with CT=M0.'
    }

    $expectedTc = Get-DecimalField 'expected_tc_count'
    $actualTc = Get-DecimalField 'actual_tc_count'
    $tcError = Get-SignedField 'tc_count_error'

    if ([Math]::Abs($tcError) -gt 2) {
        throw "Formal soak TC-count error exceeds tolerance: $tcError"
    }

    if (($actualTc -lt 468748) -or ($actualTc -gt 468752)) {
        throw "Formal soak completion count is unexpected: $actualTc"
    }

    $m0 = Get-DecimalField 'm0_complete_count'
    $m1 = Get-DecimalField 'm1_complete_count'

    if (($m0 + $m1) -ne $actualTc) {
        throw 'Formal soak M0/M1 counts do not sum to total TC count.'
    }

    if ([Math]::Abs([int64]$m0 - [int64]$m1) -gt 1) {
        throw 'Formal soak M0/M1 completion balance is invalid.'
    }

    foreach ($field in @(
        'ct_mismatch_count',
        'alternation_mismatch_count',
        'suspected_event_loss_count',
        'adc_ovr_count',
        'dma_te_count',
        'dma_dme_count',
        'dma_fe_count',
        'dma_other_error_count',
        'stop_artifact_count',
        'quiet_stop_artifact_count'
    )) {
        $value = Get-DecimalField $field

        if ($value -ne 0) {
            throw "Formal soak non-zero integrity/error counter: $field=$value"
        }
    }

    $intervals = Get-DecimalField 'timing_interval_count'

    if ($intervals -ne ($actualTc - 1)) {
        throw 'Formal soak timing interval count is inconsistent.'
    }

    $meanDelta = Get-DecimalField 'mean_delta_cycles'
    $minDelta = Get-DecimalField 'min_delta_cycles'
    $maxDelta = Get-DecimalField 'max_delta_cycles'
    $meanError = Get-DecimalField 'mean_delta_error_cycles'

    if ($meanError -gt 2304) {
        throw "Formal soak mean cadence is out of tolerance: $meanDelta"
    }

    $quietTc = Get-DecimalField 'quiet_tc_count'

    if ($quietTc -ne $actualTc) {
        throw 'TC count changed after formal-soak STOP.'
    }

    if ((Get-DecimalField 'quiet_state') -ne 1) {
        throw 'Acquisition did not remain STOPPED after formal soak.'
    }

    foreach ($field in @(
        'raw0_min',
        'raw0_max',
        'raw1_min',
        'raw1_max'
    )) {
        if ((Get-DecimalField $field) -gt 4095) {
            throw "Raw ADC result exceeds 12-bit range: $field"
        }
    }

    $systemCoreClock = Get-DecimalField 'system_core_clock'

    if ($systemCoreClock -ne 180000000) {
        throw "SystemCoreClock regressed: $systemCoreClock"
    }

    $aircr = Get-HexField 'aircr'
    $prigroup = ($aircr -shr 8) -band 7

    if ($prigroup -ne 3) {
        throw "AIRCR.PRIGROUP regressed: $prigroup"
    }

    $halTickDelta = Get-DecimalField 'hal_tick_delta'

    if ($halTickDelta -lt 600000) {
        throw "HAL tick did not advance across soak: $halTickDelta"
    }

    $observedMilliHz = Get-DecimalField 'observed_block_rate_millihz'
    $expectedMilliHz = Get-DecimalField 'expected_block_rate_millihz'

    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R1 FORMAL 10-MINUTE SOAK CLASSIFICATION: PASS'
    Write-Output '============================================================'
    Write-Output "Elapsed time: $elapsed ms"
    Write-Output "Expected TC count: $expectedTc"
    Write-Output "Actual TC count: $actualTc"
    Write-Output "TC count error: $tcError"
    Write-Output ("Observed block rate: {0:F3} blocks/s" -f ($observedMilliHz / 1000.0))
    Write-Output ("Expected block rate: {0:F3} blocks/s" -f ($expectedMilliHz / 1000.0))
    Write-Output "M0 completions: $m0"
    Write-Output "M1 completions: $m1"
    Write-Output "Mean block delta: $meanDelta cycles"
    Write-Output "Min block delta: $minDelta cycles"
    Write-Output "Max block delta: $maxDelta cycles"
    Write-Output 'CT mismatch: 0'
    Write-Output 'Alternation mismatch: 0'
    Write-Output 'Suspected event loss: 0'
    Write-Output 'ADC OVR: 0'
    Write-Output 'DMA TE/DME/FE: 0'
    Write-Output 'Post-stop stale completion: NONE'
    Write-Output "SystemCoreClock: $systemCoreClock"
    Write-Output "AIRCR.PRIGROUP: $prigroup"
    Write-Output "HAL tick delta: $halTickDelta"
    Write-Output ('Grounded raw M0 range: {0} .. {1}' -f `
        (Get-DecimalField 'raw0_min'), `
        (Get-DecimalField 'raw0_max'))
    Write-Output ('Grounded raw M1 range: {0} .. {1}' -f `
        (Get-DecimalField 'raw1_min'), `
        (Get-DecimalField 'raw1_max'))
    Write-Output ''
    Write-Output 'R1 formal soak gate: PASS'
    Write-Output 'R1 overall remains IN PROGRESS pending evidence/committed-state closeout.'
    Write-Output ''
    Write-Output "Result file: $gdbOut"

    # -------------------------------------------------------------------------
    # 7. Repository source-state guard
    # -------------------------------------------------------------------------
    $state = @(& $git status --porcelain=v1 --untracked-files=all)

    $expectedChanges = @(
        'firmware/acquisition/r1_bringup.c',
        'firmware/acquisition/r1_bringup.h'
    ) | Sort-Object

    $actualChanges = @(
        $state |
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
        throw 'Repository source state changed unexpectedly.'
    }
}
catch {
    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R1 FORMAL 10-MINUTE SOAK CLASSIFICATION: FAIL'
    Write-Output '============================================================'
    Write-Output $_.Exception.Message
    Write-Output ''
    Write-Output 'Do not commit or proceed to evidence closeout.'
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
