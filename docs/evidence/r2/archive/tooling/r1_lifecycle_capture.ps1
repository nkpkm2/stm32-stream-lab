$ErrorActionPreference = 'Stop'

$repo = 'E:\Projects\stm32-stream-lab'
$git = 'C:\Program Files\Git\cmd\git.exe'

$server = 'E:\DevTools\STM32CubeCLT-1.22.0\STLink-gdb-server\bin\ST-LINK_gdbserver.exe'
$gdb = 'E:\DevTools\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi\bin\arm-none-eabi-gdb.exe'
$cubeProgrammerBin = 'E:\DevTools\STM32CubeProgrammer-2.23.0\bin'

$serial = '067AFF545754655087043860'
$port = 61234

$build = Join-Path $repo 'build\r1-lifecycle-01'
$elf = Join-Path $build 'cubemx.elf'
$gdbScript = Join-Path $build 'r1-lifecycle-inspection.gdb'
$gdbOut = Join-Path $build 'r1-lifecycle-inspection.txt'
$gdbErr = Join-Path $build 'r1-lifecycle-inspection.stderr.txt'
$serverOut = Join-Path $build 'r1-lifecycle-gdbserver.stdout.txt'
$serverErr = Join-Path $build 'r1-lifecycle-gdbserver.stderr.txt'

$serverProc = $null

Set-Location -LiteralPath $repo

Write-Output '=== R1 LIFECYCLE REGRESSION RESULT CAPTURE ==='

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
    # 2. Create GDB inspection script
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 2. Create GDB inspection script ==='

    $commands = New-Object System.Collections.Generic.List[string]

    $commands.Add('set pagination off')
    $commands.Add('set confirm off')
    $commands.Add('set print pretty off')
    $commands.Add('set print elements 0')
    $commands.Add(('target remote 127.0.0.1:{0}' -f $port))
    $commands.Add('printf "\n=== LIFECYCLE HEADER ===\n"')
    $commands.Add('printf "magic=0x%08x\n", (unsigned int)g_r1_lifecycle_result.magic')
    $commands.Add('printf "task_created=%u\n", (unsigned int)g_r1_lifecycle_result.task_created')
    $commands.Add('printf "phase=%u\n", (unsigned int)g_r1_lifecycle_result.phase')
    $commands.Add('printf "completed_cycles=%u\n", (unsigned int)g_r1_lifecycle_result.completed_cycles')
    $commands.Add('printf "all_pass=%u\n", (unsigned int)g_r1_lifecycle_result.all_pass')

    for ($index = 0; $index -lt 6; $index++) {
        $prefix = "g_r1_lifecycle_result.cycles[$index]"

        $commands.Add(('printf "\n=== CYCLE {0} ===\n"' -f $index))

        foreach ($field in @(
            'target_run_cycles',
            'start_status',
            'stop_status',
            'tim2_cr1_after_start',
            'restart_count',
            'dbm_bit_seen',
            'start_ndtr',
            'start_ct',
            'm0ar_matches_buffer0',
            'm1ar_matches_buffer1',
            'adc_dma_bit_seen',
            'adc_dds_bit_seen',
            'tc_count',
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
            'trace_count',
            'first_sequence',
            'first_completed_target',
            'second_sequence',
            'second_completed_target',
            'partial_stop_count',
            'stop_remaining_ndtr',
            'stop_captured_samples',
            'stop_active_target',
            'stop_artifact_count',
            'quiet_tc_count',
            'quiet_stop_artifact_count',
            'quiet_state',
            'cycle_pass'
        )) {
            $commands.Add(
                ('printf "c{0}_{1}=%u\n", (unsigned int){2}.{1}' -f
                    $index,
                    $field,
                    $prefix)
            )
        }
    }

    $commands.Add('detach')
    $commands.Add('quit')

    $enc = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllLines($gdbScript, $commands, $enc)

    Write-Output "GDB command file: $gdbScript"

    # -------------------------------------------------------------------------
    # 3. Start true attach-mode GDB server
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 3. Start attach-mode ST-LINK GDB server ==='

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
    # 4. Read result
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 4. Read lifecycle result ==='

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

        throw 'GDB lifecycle result inspection failed.'
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
    # 6. Global classification
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 5. Classification ==='

    if ((Get-HexField 'magic') -ne 0x52314C31) {
        throw 'Lifecycle result magic is invalid.'
    }

    if ((Get-DecimalField 'task_created') -ne 1) {
        throw 'Lifecycle task was not created.'
    }

    if ((Get-DecimalField 'phase') -ne 4) {
        throw 'Lifecycle harness did not reach COMPLETE.'
    }

    if ((Get-DecimalField 'completed_cycles') -ne 6) {
        throw 'Lifecycle harness did not complete all six cycles.'
    }

    if ((Get-DecimalField 'all_pass') -ne 1) {
        throw 'Firmware lifecycle classifier did not pass.'
    }

    # -------------------------------------------------------------------------
    # 7. Per-cycle independent classification
    # -------------------------------------------------------------------------
    for ($index = 0; $index -lt 6; $index++) {
        $p = "c$index" + "_"

        $startStatus = Get-DecimalField ($p + 'start_status')
        $stopStatus = Get-DecimalField ($p + 'stop_status')
        $tim2Cr1 = Get-DecimalField ($p + 'tim2_cr1_after_start')
        $restartCount = Get-DecimalField ($p + 'restart_count')

        if ($startStatus -ne 0 -or $stopStatus -ne 0) {
            throw "Cycle $index start/stop status failed."
        }

        if (($tim2Cr1 -band 1) -eq 0) {
            throw "Cycle $index TIM2 was not running after start."
        }

        $expectedRestart = if ($index -eq 0) { 0 } else { 1 }

        if ($restartCount -ne $expectedRestart) {
            throw "Cycle $index restart_count is wrong: $restartCount"
        }

        foreach ($field in @(
            'dbm_bit_seen',
            'm0ar_matches_buffer0',
            'm1ar_matches_buffer1',
            'adc_dma_bit_seen',
            'adc_dds_bit_seen'
        )) {
            if ((Get-DecimalField ($p + $field)) -ne 1) {
                throw "Cycle $index startup assertion failed: $field"
            }
        }

        if ((Get-DecimalField ($p + 'start_ndtr')) -ne 256) {
            throw "Cycle $index did not restart with NDTR=256."
        }

        if ((Get-DecimalField ($p + 'start_ct')) -ne 0) {
            throw "Cycle $index did not restart with CT=M0."
        }

        $tc = Get-DecimalField ($p + 'tc_count')
        $m0 = Get-DecimalField ($p + 'm0_complete_count')
        $m1 = Get-DecimalField ($p + 'm1_complete_count')

        if ($tc -lt 2) {
            throw "Cycle $index observed too few transfer completions."
        }

        if (($m0 + $m1) -ne $tc) {
            throw "Cycle $index M0/M1 counts do not sum to TC count."
        }

        if ([Math]::Abs([int64]$m0 - [int64]$m1) -gt 1) {
            throw "Cycle $index M0/M1 completion balance is invalid."
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
            $value = Get-DecimalField ($p + $field)

            if ($value -ne 0) {
                throw "Cycle $index non-zero counter: $field=$value"
            }
        }

        $intervals = Get-DecimalField ($p + 'timing_interval_count')

        if ($intervals -ne ($tc - 1)) {
            throw "Cycle $index timing interval count is inconsistent."
        }

        $mean = Get-DecimalField ($p + 'mean_delta_cycles')

        if ([Math]::Abs([int64]$mean - 230400) -gt 2304) {
            throw "Cycle $index mean cadence is out of tolerance: $mean"
        }

        if ((Get-DecimalField ($p + 'trace_count')) -lt 2) {
            throw "Cycle $index trace is too short."
        }

        if ((Get-DecimalField ($p + 'first_sequence')) -ne 1 -or
            (Get-DecimalField ($p + 'first_completed_target')) -ne 0 -or
            (Get-DecimalField ($p + 'second_sequence')) -ne 2 -or
            (Get-DecimalField ($p + 'second_completed_target')) -ne 1) {
            throw "Cycle $index first completion sequence is not fresh M0->M1."
        }

        $partialCount = Get-DecimalField ($p + 'partial_stop_count')
        $remaining = Get-DecimalField ($p + 'stop_remaining_ndtr')
        $captured = Get-DecimalField ($p + 'stop_captured_samples')

        if ($partialCount -ne 1) {
            throw "Cycle $index did not observe exactly one partial stop."
        }

        if ($remaining -lt 1 -or $remaining -gt 255) {
            throw "Cycle $index stop NDTR is not partial: $remaining"
        }

        if (($remaining + $captured) -ne 256) {
            throw "Cycle $index partial-stop accounting is inconsistent."
        }

        if ((Get-DecimalField ($p + 'quiet_tc_count')) -ne $tc) {
            throw "Cycle $index TC count changed after STOP."
        }

        if ((Get-DecimalField ($p + 'quiet_state')) -ne 1) {
            throw "Cycle $index did not remain STOPPED during quiet window."
        }

        if ((Get-DecimalField ($p + 'cycle_pass')) -ne 1) {
            throw "Cycle $index firmware cycle classifier failed."
        }

        Write-Output (
            'PASS  Cycle {0}: TC={1}, NDTR={2}, captured={3}, mean={4}' -f
            $index,
            $tc,
            $remaining,
            $captured,
            $mean
        )
    }

    # -------------------------------------------------------------------------
    # 8. Repository guard
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

    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R1 LIFECYCLE REGRESSION CLASSIFICATION: PASS'
    Write-Output '============================================================'
    Write-Output 'Completed cycles: 6 / 6'
    Write-Output 'Basic restart sanity: PASS'
    Write-Output 'Targeted partial-stop campaign: PASS'
    Write-Output 'Fresh first completion after restart: PASS'
    Write-Output 'NDTR reset to 256 every start: PASS'
    Write-Output 'CT reset to M0 every start: PASS'
    Write-Output 'Post-stop quiet-window stability: PASS'
    Write-Output 'CT / alternation / event integrity: PASS'
    Write-Output 'ADC OVR / DMA errors: 0'
    Write-Output 'R1 overall: IN PROGRESS'
    Write-Output ''
    Write-Output "Result file: $gdbOut"
}
catch {
    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R1 LIFECYCLE REGRESSION CLASSIFICATION: FAIL'
    Write-Output '============================================================'
    Write-Output $_.Exception.Message
    Write-Output ''
    Write-Output 'Do not commit or start soak testing yet.'
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
