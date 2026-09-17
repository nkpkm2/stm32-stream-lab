[CmdletBinding()]
param(
    [string]$Repo = 'E:\Projects\stm32-stream-lab',
    [string]$BuildDir = 'E:\Projects\stm32-stream-lab\build\r2-w5-w5target-c4b79468'
)

$ErrorActionPreference = 'Stop'

$git = 'C:\Program Files\Git\cmd\git.exe'
$programmer = 'E:\DevTools\STM32CubeProgrammer-2.23.0\bin\STM32_Programmer_CLI.exe'
$programmerBin = 'E:\DevTools\STM32CubeProgrammer-2.23.0\bin'
$gdbServer = 'E:\DevTools\STM32CubeCLT-1.22.0\STLink-gdb-server\bin\ST-LINK_gdbserver.exe'
$gdb = 'E:\DevTools\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi\bin\arm-none-eabi-gdb.exe'
$nm = 'E:\DevTools\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi\bin\arm-none-eabi-nm.exe'

$stlinkSerial = '067AFF545754655087043860'
$port = 61234

$expectedHead = '7ae9a562ff0eaeeeda4a80c1bea16a6d22d7203c'
$expectedR1Pass = '5ebf62e90b31e262f44013afb594a430061f139a'

$serverProc = $null

Set-Location -LiteralPath $Repo

Write-Output '=== R2-W5 REAL-HARDWARE CONTROLLED CAPACITY DROP ==='

function Get-ChangedPaths {
    $lines = @(& $git status --porcelain=v1 --untracked-files=all)

    return @(
        $lines |
        ForEach-Object {
            if ($_.Length -ge 4) {
                $_.Substring(3).Replace('\','/')
            }
        }
    ) | Sort-Object
}

function Get-Dec {
    param(
        [string]$Text,
        [string]$Name
    )

    $m = [regex]::Match(
        $Text,
        '(?m)^' + [regex]::Escape($Name) + '=([0-9]+)\r?$'
    )

    if (-not $m.Success) {
        throw "Missing decimal result field: $Name"
    }

    return [uint64]$m.Groups[1].Value
}

function Get-Hex {
    param(
        [string]$Text,
        [string]$Name
    )

    $m = [regex]::Match(
        $Text,
        '(?m)^' + [regex]::Escape($Name) + '=0x([0-9A-Fa-f]+)\r?$'
    )

    if (-not $m.Success) {
        throw "Missing hexadecimal result field: $Name"
    }

    return [Convert]::ToUInt64($m.Groups[1].Value, 16)
}

function Add-DecCommand {
    param(
        [System.Collections.Generic.List[string]]$Commands,
        [string]$Name,
        [string]$Expression
    )

    $Commands.Add(
        ('printf "{0}=%u\n", (unsigned int)({1})' -f $Name, $Expression)
    )
}

function Add-HexCommand {
    param(
        [System.Collections.Generic.List[string]]$Commands,
        [string]$Name,
        [string]$Expression
    )

    $Commands.Add(
        ('printf "{0}=0x%08x\n", (unsigned int)({1})' -f $Name, $Expression)
    )
}

try {
    # -------------------------------------------------------------------------
    # 1. Repository / gate / installed W5 source guard
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 1. Repository and gate guard ==='

    $head = (& $git rev-parse HEAD).Trim()

    if ($head -ne $expectedHead) {
        throw "Unexpected HEAD: $head"
    }

    $localMain = (& $git rev-parse main).Trim()
    $remoteMain = (& $git rev-parse origin/main).Trim()

    if ($localMain -ne $remoteMain) {
        throw 'main and origin/main are not synchronized.'
    }

    $r1 = (& $git rev-parse 'r1-pass^{commit}').Trim()

    if ($r1 -ne $expectedR1Pass) {
        throw "r1-pass moved unexpectedly: $r1"
    }

    if (@(& $git tag --list 'r2-pass').Count -ne 0) {
        throw 'r2-pass exists unexpectedly.'
    }

    if (@(& $git diff --cached --name-only).Count -ne 0) {
        throw 'Git staging area is not empty.'
    }

    $expectedChanges = @(
        'docs/evidence/r2/w5/PLAN.md',
        'firmware/acquisition/r2_w5_capacity.c',
        'firmware/acquisition/r2_w5_capacity.h',
        'firmware/acquisition/r2_w5_guard.h',
        'firmware/cubemx/CMakeLists.txt',
        'firmware/cubemx/Core/Inc/FreeRTOSConfig.h',
        'firmware/cubemx/Core/Src/main.c',
        'firmware/cubemx/Core/Src/stm32f4xx_it.c',
        'tests/native/CMakeLists.txt',
        'tests/native/test_r2_w5_model.c',
        'tools/r2/verify_w5.ps1'
    ) | Sort-Object

    $actualChanges = Get-ChangedPaths

    $scopeDiff = Compare-Object `
        -ReferenceObject $expectedChanges `
        -DifferenceObject $actualChanges

    if ($scopeDiff) {
        $scopeDiff | Format-Table
        throw 'Working tree is not the exact installed W5 source state.'
    }

    Write-Output "HEAD: $head"
    Write-Output "r1-pass -> $r1"
    Write-Output 'r2-pass: NOT CREATED'
    Write-Output 'W5 installed source scope: verified'
    Write-Output 'Staging area: unchanged'

    # -------------------------------------------------------------------------
    # 2. Exact W5 target-build guard
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 2. Exact W5 target-build guard ==='

    foreach ($tool in @($programmer, $gdbServer, $gdb, $nm)) {
        if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
            throw "Required tool missing: $tool"
        }
    }

    if (-not (Test-Path -LiteralPath $BuildDir -PathType Container)) {
        throw "W5 target build directory missing: $BuildDir"
    }

    $cache = Join-Path $BuildDir 'CMakeCache.txt'
    $verification = Join-Path $BuildDir 'verification-summary.txt'
    $elf = Join-Path $BuildDir 'cubemx.elf'

    foreach ($path in @($cache, $verification, $elf)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required W5 target-build artifact missing: $path"
        }
    }

    $cacheText = [System.IO.File]::ReadAllText($cache)

    foreach ($required in @(
        'STREAM_LAB_R2_W3:BOOL=OFF',
        'STREAM_LAB_R2_W4:BOOL=OFF',
        'STREAM_LAB_R2_W5:BOOL=ON'
    )) {
        if (-not $cacheText.Contains($required)) {
            throw "Selected build profile is missing: $required"
        }
    }

    $verificationText = [System.IO.File]::ReadAllText($verification)

    if (-not $verificationText.Contains('Profile: W5Target') -or
        -not $verificationText.Contains('Build: PASS') -or
        -not $verificationText.Contains('Hardware: NOT RUN')) {
        throw 'Selected build directory is not a verified W5Target build.'
    }

    $symbols = @(& $nm -g --defined-only $elf)

    if ($LASTEXITCODE -ne 0) {
        throw 'arm-none-eabi-nm failed.'
    }

    $symbolText = $symbols -join "`n"

    foreach ($symbol in @(
        'R2_W5_CreateTasks',
        'R2_W5_IrqEnter',
        'R2_W5_IrqExit',
        'R2_W5_TraceQueueSend',
        'g_r2_w5_result'
    )) {
        if ($symbolText -notmatch
            ('(?m)\b' + [regex]::Escape($symbol) + '\r?$')) {
            throw "Required W5 symbol missing from ELF: $symbol"
        }
    }

    foreach ($forbiddenSymbol in @(
        'R1_Acquisition_Start',
        'R2_W3_CreateTask',
        'R2_W4_CreateTasks'
    )) {
        if ($symbolText -match
            ('(?m)\b' + [regex]::Escape($forbiddenSymbol) + '\r?$')) {
            throw "Exclusive W5 ELF unexpectedly contains: $forbiddenSymbol"
        }
    }

    $elfHash = (
        Get-FileHash -LiteralPath $elf -Algorithm SHA256
    ).Hash

    Write-Output "Build directory: $BuildDir"
    Write-Output "W5 ELF SHA256: $elfHash"
    Write-Output 'Profile identity: W3=OFF / W4=OFF / W5=ON'
    Write-Output 'W5 symbols: verified'
    Write-Output 'R1/W3/W4 experiment entry points in W5 ELF: ABSENT'

    # -------------------------------------------------------------------------
    # 3. Flash / verify / reset exact W5 ELF
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 3. Flash exact W5 ELF ==='
    Write-Output 'PA0 / ADC1_IN0 must remain connected to GND.'

    $flashOut = Join-Path $BuildDir 'hardware-flash.stdout.txt'
    $flashErr = Join-Path $BuildDir 'hardware-flash.stderr.txt'

    $flashArgs = @(
        '-c',
        'port=SWD',
        'freq=4000',
        "sn=$stlinkSerial",
        '-w',
        $elf,
        '-v',
        '-rst'
    )

    $flashProc = Start-Process `
        -FilePath $programmer `
        -ArgumentList $flashArgs `
        -Wait `
        -PassThru `
        -NoNewWindow `
        -RedirectStandardOutput $flashOut `
        -RedirectStandardError $flashErr

    $flashProc.Refresh()

    if ($flashProc.ExitCode -ne 0) {
        if (Test-Path -LiteralPath $flashOut) {
            Get-Content $flashOut | Select-Object -Last 100
        }

        if (Test-Path -LiteralPath $flashErr) {
            Get-Content $flashErr | Select-Object -Last 100
        }

        throw 'W5 flash / verify / reset failed.'
    }

    Write-Output 'SWD flash: PASS'
    Write-Output 'Flash verify: PASS'
    Write-Output 'Target reset: issued once'

    # 100 ms startup + about 82 ms of TC events + delayed consumer/drain margin.
    Start-Sleep -Seconds 2

    Write-Output 'W5 bounded capacity-drop completion wait: complete'

    # -------------------------------------------------------------------------
    # 4. Capture frozen W5 result
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 4. Capture frozen W5 result ==='

    $gdbScript = Join-Path $BuildDir 'hardware-inspection.gdb'
    $gdbOut = Join-Path $BuildDir 'hardware-inspection.txt'
    $gdbErr = Join-Path $BuildDir 'hardware-inspection.stderr.txt'
    $serverOut = Join-Path $BuildDir 'hardware-gdbserver.stdout.txt'
    $serverErr = Join-Path $BuildDir 'hardware-gdbserver.stderr.txt'

    $commands = New-Object System.Collections.Generic.List[string]

    $commands.Add('set pagination off')
    $commands.Add('set confirm off')
    $commands.Add('set print pretty off')
    $commands.Add(('target remote 127.0.0.1:{0}' -f $port))

    Add-HexCommand $commands 'magic' 'g_r2_w5_result.magic'

    foreach ($field in @(
        'control_task_created',
        'processing_task_created',
        'phase',
        'test_pass',
        'system_core_clock',
        'start_status',
        'stop_status',
        'start_dma_ndtr',
        'epoch_before',
        'epoch_after',
        'irq_count',
        'input_count',
        'admitted_count',
        'capacity_drop_count',
        'processed_count',
        'released_count',
        'recovered_admission_after_drop_count',
        'current_drop_streak',
        'max_drop_streak',
        'init_hook_count',
        'completion_hook_count',
        'illegal_free_send_count',
        'ready_send_fail_count',
        'notification_fail_count',
        'token_ledger_errors',
        'dma_error_flags_seen',
        'adc_ovr_seen',
        'max_nominal_to_decision_cycles',
        'max_nominal_to_irq_exit_cycles',
        'max_final_window_cycles',
        'max_ready_depth',
        'min_free_depth',
        'free_queue_depth_final',
        'ready_queue_depth_final',
        'full_sample_count',
        'sample_errors',
        'canary_errors',
        'quiet_irq_count',
        'quiet_input_count',
        'quiet_admitted_count',
        'quiet_drop_count',
        'quiet_processed_count'
    )) {
        Add-DecCommand $commands $field ('g_r2_w5_result.' + $field)
    }

    foreach ($field in @(
        'fault_bits',
        'aircr',
        'start_dma_cr',
        'start_adc_cr2',
        'start_tim2_cr1',
        'free_token_mask_final',
        'ready_token_mask_final'
    )) {
        Add-HexCommand $commands $field ('g_r2_w5_result.' + $field)
    }

    for ($i = 0; $i -lt 3; $i++) {
        Add-HexCommand $commands ("buffer_address_$i") ("g_r2_w5_result.buffer_address[$i]")
        Add-DecCommand $commands ("admitted_by_buffer_$i") ("g_r2_w5_result.admitted_by_buffer[$i]")
        Add-DecCommand $commands ("dropped_by_buffer_$i") ("g_r2_w5_result.dropped_by_buffer[$i]")
        Add-DecCommand $commands ("processed_by_buffer_$i") ("g_r2_w5_result.processed_by_buffer[$i]")
        Add-DecCommand $commands ("released_by_buffer_$i") ("g_r2_w5_result.released_by_buffer[$i]")
    }

    foreach ($field in @(
        'activated',
        'k',
        'active_count',
        'inactive_count',
        'free_count',
        'dma_owned_count',
        'ready_count',
        'processing_count',
        'violation_count'
    )) {
        Add-DecCommand $commands ("pool_$field") ("g_r2_w5_result.pool_at_stop.$field")
    }

    for ($i = 0; $i -lt 10; $i++) {
        Add-DecCommand $commands ("pool_state_$i") ("g_r2_w5_result.pool_at_stop.states[$i]")
    }

    foreach ($field in @(
        'initialized',
        'mapping_epoch',
        'violation_count',
        'm0_buffer',
        'm1_buffer'
    )) {
        Add-DecCommand $commands ("slots_$field") ("g_r2_w5_result.slots_at_stop.$field")
    }

    $traceDecFields = @(
        'sequence',
        'decision',
        'ct_entry',
        'completed_slot',
        'completed_id',
        'replacement_id',
        'ndtr_guard',
        'free_depth_after_take',
        'ready_depth_after_publish',
        'nominal_to_decision_cycles',
        'nominal_to_irq_exit_cycles',
        'final_window_cycles',
        'mapping_epoch_after',
        'processing_begin_offset_cycles',
        'processing_end_offset_cycles',
        'release_commit_offset_cycles',
        'raw_min',
        'raw_max',
        'processed_ok'
    )

    $traceHexFields = @(
        'm0_before',
        'm1_before',
        'm0_after',
        'm1_after'
    )

    for ($i = 0; $i -lt 64; $i++) {
        foreach ($field in $traceDecFields) {
            Add-DecCommand `
                $commands `
                ("trace_{0}_{1}" -f ($i + 1), $field) `
                ("g_r2_w5_result.trace[$i].$field")
        }

        foreach ($field in $traceHexFields) {
            Add-HexCommand `
                $commands `
                ("trace_{0}_{1}" -f ($i + 1), $field) `
                ("g_r2_w5_result.trace[$i].$field")
        }
    }

    # Final live hardware state after Stop().
    Add-HexCommand $commands 'live_dma_cr' '*(unsigned int*)0x40026410'
    Add-DecCommand $commands 'live_dma_ndtr' '*(unsigned int*)0x40026414'
    Add-HexCommand $commands 'live_dma_m0ar' '*(unsigned int*)0x4002641c'
    Add-HexCommand $commands 'live_dma_m1ar' '*(unsigned int*)0x40026420'
    Add-HexCommand $commands 'live_dma_lisr' '*(unsigned int*)0x40026400'
    Add-HexCommand $commands 'live_tim2_cr1' '*(unsigned int*)0x40000000'
    Add-HexCommand $commands 'live_adc_cr2' '*(unsigned int*)0x40012008'

    $commands.Add('detach')
    $commands.Add('quit')

    $enc = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllLines($gdbScript, $commands, $enc)

    $existingServer = @(
        Get-Process -Name 'ST-LINK_gdbserver' -ErrorAction SilentlyContinue
    )

    if ($existingServer.Count -ne 0) {
        throw 'An ST-LINK GDB server is already running.'
    }

    $serverArgs = @(
        '-d',
        '-m', '0',
        '-p', "$port",
        '-l', '1',
        '-i', $stlinkSerial,
        '-cp', $programmerBin,
        '-g'
    )

    $serverProc = Start-Process `
        -FilePath $gdbServer `
        -ArgumentList $serverArgs `
        -PassThru `
        -NoNewWindow `
        -RedirectStandardOutput $serverOut `
        -RedirectStandardError $serverErr

    Start-Sleep -Seconds 2
    $serverProc.Refresh()

    if ($serverProc.HasExited) {
        if (Test-Path -LiteralPath $serverOut) {
            Get-Content $serverOut | Select-Object -Last 100
        }

        if (Test-Path -LiteralPath $serverErr) {
            Get-Content $serverErr | Select-Object -Last 100
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
        if (Test-Path -LiteralPath $gdbOut) {
            Get-Content $gdbOut | Select-Object -Last 180
        }

        if (Test-Path -LiteralPath $gdbErr) {
            Get-Content $gdbErr | Select-Object -Last 180
        }

        throw 'GDB W5 inspection failed.'
    }

    $text = [System.IO.File]::ReadAllText($gdbOut)

    # -------------------------------------------------------------------------
    # 5. Independent host classification
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 5. Independent host classification ==='

    $magic = Get-Hex $text 'magic'
    $phase = Get-Dec $text 'phase'
    $testPass = Get-Dec $text 'test_pass'
    $faultBits = Get-Hex $text 'fault_bits'

    if ($magic -ne 0x52325735) {
        throw ("Invalid W5 magic: 0x{0:X8}" -f $magic)
    }

    if ((Get-Dec $text 'control_task_created') -ne 1 -or
        (Get-Dec $text 'processing_task_created') -ne 1) {
        throw 'W5 tasks were not both created.'
    }

    if ($phase -ne 5) {
        Write-Output ("Firmware phase: {0}" -f $phase)
        Write-Output ("Firmware fault bits: 0x{0:X8}" -f $faultBits)
        throw 'W5 firmware did not reach COMPLETE.'
    }

    if ($testPass -ne 1 -or $faultBits -ne 0) {
        Write-Output ("Firmware fault bits: 0x{0:X8}" -f $faultBits)
        throw 'W5 firmware classifier did not pass.'
    }

    if ((Get-Dec $text 'system_core_clock') -ne 180000000) {
        throw 'SystemCoreClock regression.'
    }

    $aircr = Get-Hex $text 'aircr'
    $prigroup = ($aircr -shr 8) -band 7

    if ($prigroup -ne 3) {
        throw "AIRCR.PRIGROUP regression: $prigroup"
    }

    if ((Get-Dec $text 'start_status') -ne 0 -or
        (Get-Dec $text 'stop_status') -ne 0) {
        throw 'W5 start or stop status is not HAL_OK.'
    }

    if ((Get-Hex $text 'start_dma_cr') -ne 0x00062D17 -or
        (Get-Dec $text 'start_dma_ndtr') -ne 256 -or
        (Get-Hex $text 'start_adc_cr2') -ne 0x16000701 -or
        ((Get-Hex $text 'start_tim2_cr1') -band 1) -ne 0) {
        throw 'W5 startup hardware state is unexpected.'
    }

    $irqCount = Get-Dec $text 'irq_count'
    $inputCount = Get-Dec $text 'input_count'
    $admittedCount = Get-Dec $text 'admitted_count'
    $dropCount = Get-Dec $text 'capacity_drop_count'
    $processedCount = Get-Dec $text 'processed_count'
    $releasedCount = Get-Dec $text 'released_count'
    $recoveries = Get-Dec $text 'recovered_admission_after_drop_count'
    $maxDropStreak = Get-Dec $text 'max_drop_streak'

    if ($irqCount -ne 64 -or
        $inputCount -ne 64 -or
        ($admittedCount + $dropCount) -ne 64) {
        throw 'W5 input accounting does not satisfy admitted + drops == 64.'
    }

    if ($admittedCount -lt 4 -or
        $dropCount -lt 16 -or
        $maxDropStreak -lt 3 -or
        $recoveries -lt 3) {
        throw 'W5 did not generate the required directed capacity-pressure behavior.'
    }

    if ($processedCount -ne $admittedCount -or
        $releasedCount -ne $admittedCount) {
        throw 'W5 admitted blocks were not all processed and released.'
    }

    if ((Get-Dec $text 'init_hook_count') -ne 1 -or
        (Get-Dec $text 'completion_hook_count') -ne $admittedCount -or
        (Get-Dec $text 'illegal_free_send_count') -ne 0 -or
        (Get-Dec $text 'ready_send_fail_count') -ne 0 -or
        (Get-Dec $text 'notification_fail_count') -ne 0 -or
        (Get-Dec $text 'token_ledger_errors') -ne 0) {
        throw 'W5 queue/hook/token counters are invalid.'
    }

    if ((Get-Dec $text 'dma_error_flags_seen') -ne 0 -or
        (Get-Dec $text 'adc_ovr_seen') -ne 0) {
        throw 'DMA error or ADC overrun was observed.'
    }

    $maxDecision = Get-Dec $text 'max_nominal_to_decision_cycles'
    $maxExit = Get-Dec $text 'max_nominal_to_irq_exit_cycles'
    $maxFinal = Get-Dec $text 'max_final_window_cycles'
    $maxReadyDepth = Get-Dec $text 'max_ready_depth'
    $minFreeDepth = Get-Dec $text 'min_free_depth'

    if ($maxDecision -gt 57600 -or
        $maxExit -gt 80640 -or
        $maxFinal -gt 3600) {
        throw 'W5 timing budget was exceeded.'
    }

    if ($maxReadyDepth -lt 1 -or $maxReadyDepth -gt 1 -or
        $minFreeDepth -ne 0) {
        throw 'W5 queue-depth summary does not prove K=1 exhaustion.'
    }

    if ((Get-Dec $text 'free_queue_depth_final') -ne 1 -or
        (Get-Dec $text 'ready_queue_depth_final') -ne 0) {
        throw 'W5 final queue depths are invalid.'
    }

    if ((Get-Dec $text 'full_sample_count') -ne ($admittedCount * 256) -or
        (Get-Dec $text 'sample_errors') -ne 0 -or
        (Get-Dec $text 'canary_errors') -ne 0) {
        throw 'W5 sample/canary validation failed.'
    }

    if ((Get-Dec $text 'quiet_irq_count') -ne $irqCount -or
        (Get-Dec $text 'quiet_input_count') -ne $inputCount -or
        (Get-Dec $text 'quiet_admitted_count') -ne $admittedCount -or
        (Get-Dec $text 'quiet_drop_count') -ne $dropCount -or
        (Get-Dec $text 'quiet_processed_count') -ne $processedCount) {
        throw 'W5 post-stop quiet counters are not stable.'
    }

    # -------------------------------------------------------------------------
    # 6. Final owner / token conservation
    # -------------------------------------------------------------------------
    if ((Get-Dec $text 'pool_activated') -ne 1 -or
        (Get-Dec $text 'pool_k') -ne 1 -or
        (Get-Dec $text 'pool_active_count') -ne 3 -or
        (Get-Dec $text 'pool_inactive_count') -ne 7 -or
        (Get-Dec $text 'pool_free_count') -ne 1 -or
        (Get-Dec $text 'pool_dma_owned_count') -ne 2 -or
        (Get-Dec $text 'pool_ready_count') -ne 0 -or
        (Get-Dec $text 'pool_processing_count') -ne 0 -or
        (Get-Dec $text 'pool_violation_count') -ne 0) {
        throw 'Final W5 BufferPool summary is invalid.'
    }

    if ((Get-Dec $text 'slots_initialized') -ne 1 -or
        (Get-Dec $text 'slots_mapping_epoch') -ne ($admittedCount + 1) -or
        (Get-Dec $text 'slots_violation_count') -ne 0) {
        throw 'Final W5 DMA-slot summary is invalid.'
    }

    $m0Id = [int](Get-Dec $text 'slots_m0_buffer')
    $m1Id = [int](Get-Dec $text 'slots_m1_buffer')

    if ($m0Id -lt 0 -or $m0Id -ge 3 -or
        $m1Id -lt 0 -or $m1Id -ge 3 -or
        $m0Id -eq $m1Id) {
        throw 'Final W5 DMA-slot IDs are invalid.'
    }

    $freeId = -1

    for ($i = 0; $i -lt 10; $i++) {
        $state = Get-Dec $text ("pool_state_$i")

        if ($i -ge 3) {
            if ($state -ne 0) {
                throw "Inactive tail B$i is not INACTIVE."
            }
        }
        elseif ($state -eq 1) {
            if ($freeId -ne -1) {
                throw 'More than one final FREE buffer found.'
            }

            $freeId = $i
        }
        elseif ($state -ne 2) {
            throw "Active buffer B$i is not FREE or DMA_OWNED."
        }
    }

    if ($freeId -lt 0 -or
        $freeId -eq $m0Id -or
        $freeId -eq $m1Id) {
        throw 'Final FREE buffer identity is invalid.'
    }

    if ((Get-Dec $text ("pool_state_$m0Id")) -ne 2 -or
        (Get-Dec $text ("pool_state_$m1Id")) -ne 2) {
        throw 'Final M0/M1 buffers are not DMA_OWNED.'
    }

    $freeMask = Get-Hex $text 'free_token_mask_final'
    $readyMask = Get-Hex $text 'ready_token_mask_final'
    $expectedFreeMask = [uint64]1 -shl $freeId

    if ($freeMask -ne $expectedFreeMask -or $readyMask -ne 0) {
        throw 'Final W5 queue-token masks do not match ownership.'
    }

    if (($freeMask -bor
        ([uint64]1 -shl $m0Id) -bor
        ([uint64]1 -shl $m1Id)) -ne 0x7) {
        throw 'Final three-buffer token/ownership conservation failed.'
    }

    # -------------------------------------------------------------------------
    # 7. Physical addresses and live post-stop hardware
    # -------------------------------------------------------------------------
    $bufferAddress = @()

    for ($i = 0; $i -lt 3; $i++) {
        $address = Get-Hex $text ("buffer_address_$i")

        if (($address -band 3) -ne 0 -or
            $address -lt 0x20000000 -or
            $address -ge 0x20020000) {
            throw ("Invalid SRAM address for B{0}: 0x{1:X8}" -f $i, $address)
        }

        if ($bufferAddress -contains $address) {
            throw "Duplicate physical address for B$i."
        }

        $bufferAddress += $address
    }

    if (((Get-Hex $text 'live_dma_cr') -band 1) -ne 0 -or
        ((Get-Hex $text 'live_tim2_cr1') -band 1) -ne 0 -or
        ((Get-Hex $text 'live_adc_cr2') -band 0x101) -ne 0) {
        throw 'Final live W5 hardware is not quiescent.'
    }

    if (((Get-Hex $text 'live_dma_lisr') -band 0x0D) -ne 0) {
        throw 'Final live DMA FE/DME/TE flag is set.'
    }

    if ((Get-Hex $text 'live_dma_m0ar') -ne $bufferAddress[$m0Id] -or
        (Get-Hex $text 'live_dma_m1ar') -ne $bufferAddress[$m1Id]) {
        throw 'Final live M0AR/M1AR do not match the slot model.'
    }

    # -------------------------------------------------------------------------
    # 8. Per-buffer totals and all 64 decision traces
    # -------------------------------------------------------------------------
    $admittedSum = 0
    $dropSum = 0
    $processedSum = 0
    $releasedSum = 0

    for ($i = 0; $i -lt 3; $i++) {
        $a = Get-Dec $text ("admitted_by_buffer_$i")
        $d = Get-Dec $text ("dropped_by_buffer_$i")
        $p = Get-Dec $text ("processed_by_buffer_$i")
        $r = Get-Dec $text ("released_by_buffer_$i")

        if ($a -ne $p -or $a -ne $r) {
            throw "Per-buffer admitted/processed/released mismatch for B$i."
        }

        $admittedSum += $a
        $dropSum += $d
        $processedSum += $p
        $releasedSum += $r
    }

    if ($admittedSum -ne $admittedCount -or
        $dropSum -ne $dropCount -or
        $processedSum -ne $processedCount -or
        $releasedSum -ne $releasedCount) {
        throw 'Per-buffer W5 totals do not match global totals.'
    }

    $currentM0 = 0
    $currentM1 = 1
    $mappingEpoch = 1
    $derivedAdmitted = 0
    $derivedDrops = 0
    $derivedRecoveries = 0
    $currentDropStreak = 0
    $derivedMaxDropStreak = 0
    $lastDecisionWasDrop = $false
    $derivedMaxDecision = 0
    $derivedMaxExit = 0
    $derivedMaxFinal = 0
    $globalRawMin = 4096
    $globalRawMax = 0

    $traceRows = New-Object System.Collections.Generic.List[string]
    $traceRows.Add(
        'sequence,decision,ct,completed_slot,completed_id,replacement_id,ndtr_guard,nominal_to_decision_cycles,nominal_to_irq_exit_cycles,final_window_cycles,mapping_epoch_after,free_depth_after_take,ready_depth_after_publish,processed_ok'
    )

    for ($seq = 1; $seq -le 64; $seq++) {
        $prefix = "trace_${seq}_"

        if ((Get-Dec $text ($prefix + 'sequence')) -ne $seq) {
            throw "Trace $seq sequence field is invalid."
        }

        $decision = [int](Get-Dec $text ($prefix + 'decision'))
        $ct = [int](Get-Dec $text ($prefix + 'ct_entry'))
        $completedSlot = [int](Get-Dec $text ($prefix + 'completed_slot'))
        $completedId = [int](Get-Dec $text ($prefix + 'completed_id'))
        $replacementId = [int](Get-Dec $text ($prefix + 'replacement_id'))

        $expectedCt = $seq -band 1
        $expectedCompletedSlot = $expectedCt -bxor 1
        $expectedCompletedId = if ($expectedCompletedSlot -eq 0) {
            $currentM0
        }
        else {
            $currentM1
        }

        if ($ct -ne $expectedCt -or
            $completedSlot -ne $expectedCompletedSlot -or
            $completedId -ne $expectedCompletedId) {
            throw "Trace $seq CT/slot/completed-ID identity is invalid."
        }

        $m0Before = Get-Hex $text ($prefix + 'm0_before')
        $m1Before = Get-Hex $text ($prefix + 'm1_before')
        $m0After = Get-Hex $text ($prefix + 'm0_after')
        $m1After = Get-Hex $text ($prefix + 'm1_after')

        if ($m0Before -ne $bufferAddress[$currentM0] -or
            $m1Before -ne $bufferAddress[$currentM1]) {
            throw "Trace $seq pre-decision M0/M1 mapping is inconsistent."
        }

        $ndtr = Get-Dec $text ($prefix + 'ndtr_guard')
        $decisionCycles = Get-Dec $text ($prefix + 'nominal_to_decision_cycles')
        $exitCycles = Get-Dec $text ($prefix + 'nominal_to_irq_exit_cycles')
        $finalWindow = Get-Dec $text ($prefix + 'final_window_cycles')
        $epochAfter = Get-Dec $text ($prefix + 'mapping_epoch_after')
        $freeDepth = Get-Dec $text ($prefix + 'free_depth_after_take')
        $readyDepth = Get-Dec $text ($prefix + 'ready_depth_after_publish')
        $processingBegin = Get-Dec $text ($prefix + 'processing_begin_offset_cycles')
        $processingEnd = Get-Dec $text ($prefix + 'processing_end_offset_cycles')
        $releaseCommit = Get-Dec $text ($prefix + 'release_commit_offset_cycles')
        $rawMin = Get-Dec $text ($prefix + 'raw_min')
        $rawMax = Get-Dec $text ($prefix + 'raw_max')
        $processedOk = Get-Dec $text ($prefix + 'processed_ok')

        if ($ndtr -lt 192 -or $ndtr -gt 256) {
            throw "Trace $seq NDTR safety guard failed."
        }

        if ($decisionCycles -gt 57600 -or
            $exitCycles -gt 80640 -or
            $finalWindow -gt 3600) {
            throw "Trace $seq timing budget was exceeded."
        }

        if ($decisionCycles -gt $derivedMaxDecision) {
            $derivedMaxDecision = $decisionCycles
        }
        if ($exitCycles -gt $derivedMaxExit) {
            $derivedMaxExit = $exitCycles
        }
        if ($finalWindow -gt $derivedMaxFinal) {
            $derivedMaxFinal = $finalWindow
        }

        if ($decision -eq 1) {
            # ADMIT
            if ($replacementId -lt 0 -or $replacementId -ge 3 -or
                $replacementId -eq $currentM0 -or
                $replacementId -eq $currentM1) {
                throw "Trace $seq admission replacement is invalid."
            }

            if ($completedSlot -eq 0) {
                $currentM0 = $replacementId
            }
            else {
                $currentM1 = $replacementId
            }

            ++$mappingEpoch
            ++$derivedAdmitted

            if ($lastDecisionWasDrop) {
                ++$derivedRecoveries
            }

            $lastDecisionWasDrop = $false
            $currentDropStreak = 0

            if ($m0After -ne $bufferAddress[$currentM0] -or
                $m1After -ne $bufferAddress[$currentM1]) {
                throw "Trace $seq admission did not update only the expected inactive mapping."
            }

            if ($freeDepth -ne 0 -or
                $readyDepth -lt 1 -or $readyDepth -gt 1) {
                throw "Trace $seq admission queue-depth evidence is invalid."
            }

            $nominal = $seq * 230400

            if ($processingBegin -lt ($nominal + $exitCycles) -or
                $processingEnd -lt $processingBegin -or
                $releaseCommit -lt $processingEnd -or
                $rawMin -gt $rawMax -or
                $rawMax -gt 4095 -or
                $processedOk -ne 1) {
                throw "Trace $seq admitted processing timeline/data is invalid."
            }

            if ($rawMin -lt $globalRawMin) {
                $globalRawMin = $rawMin
            }
            if ($rawMax -gt $globalRawMax) {
                $globalRawMax = $rawMax
            }
        }
        elseif ($decision -eq 2) {
            # DROP
            if ($replacementId -ne 255) {
                throw "Trace $seq dropped event unexpectedly has a replacement buffer."
            }

            ++$derivedDrops
            ++$currentDropStreak
            $lastDecisionWasDrop = $true

            if ($currentDropStreak -gt $derivedMaxDropStreak) {
                $derivedMaxDropStreak = $currentDropStreak
            }

            if ($m0After -ne $m0Before -or
                $m1After -ne $m1Before) {
                throw "Trace $seq DROP changed an MxAR mapping."
            }

            if ($freeDepth -ne 0) {
                throw "Trace $seq DROP did not occur with FreeBufferQueue empty."
            }

            if ($processingBegin -ne 0 -or
                $processingEnd -ne 0 -or
                $releaseCommit -ne 0 -or
                $rawMin -ne 0 -or
                $rawMax -ne 0 -or
                $processedOk -ne 0) {
                throw "Trace $seq DROP was incorrectly processed/published."
            }
        }
        else {
            throw "Trace $seq has invalid decision code: $decision"
        }

        if ($epochAfter -ne $mappingEpoch) {
            throw "Trace $seq mapping epoch changed incorrectly."
        }

        $traceRows.Add(
            ('{0},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10},{11},{12},{13}' -f
                $seq,
                $decision,
                $ct,
                $completedSlot,
                $completedId,
                $replacementId,
                $ndtr,
                $decisionCycles,
                $exitCycles,
                $finalWindow,
                $epochAfter,
                $freeDepth,
                $readyDepth,
                $processedOk)
        )
    }

    if ($derivedAdmitted -ne $admittedCount -or
        $derivedDrops -ne $dropCount -or
        $derivedRecoveries -ne $recoveries -or
        $derivedMaxDropStreak -ne $maxDropStreak) {
        throw 'Trace-derived W5 admission/drop/recovery summaries do not match firmware summaries.'
    }

    if ($mappingEpoch -ne (Get-Dec $text 'slots_mapping_epoch') -or
        $currentM0 -ne $m0Id -or
        $currentM1 -ne $m1Id) {
        throw 'Trace-derived final DMA mapping does not match final state.'
    }

    if ($derivedMaxDecision -ne $maxDecision -or
        $derivedMaxExit -ne $maxExit -or
        $derivedMaxFinal -ne $maxFinal) {
        throw 'Trace-derived timing maxima do not match firmware summaries.'
    }

    # -------------------------------------------------------------------------
    # 9. Preserve local machine-readable run outputs
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 6. Preserve local run outputs ==='

    $traceCsv = Join-Path $BuildDir 'hardware-trace.csv'
    $classification = Join-Path $BuildDir 'hardware-classification.txt'

    [System.IO.File]::WriteAllLines($traceCsv, $traceRows, $enc)

    $classificationLines = @(
        'R2-W5 real-hardware controlled capacity-drop classification',
        '',
        ('Source HEAD: {0}' -f $expectedHead),
        ('W5 ELF SHA256: {0}' -f $elfHash),
        'Input: PA0 / ADC1_IN0 connected to GND',
        'K: 1',
        'P: 3',
        'Sample rate: 200 kS/s',
        'Block samples: 256',
        'Input TC events: 64',
        ('Admitted blocks: {0}' -f $admittedCount),
        ('Capacity drops: {0}' -f $dropCount),
        ('Processed blocks: {0}' -f $processedCount),
        ('Released blocks: {0}' -f $releasedCount),
        ('Recovered admissions after drop streaks: {0}' -f $recoveries),
        ('Maximum consecutive drop streak: {0}' -f $maxDropStreak),
        'Initialization FreeBufferQueue commits: 1',
        ('Completion release commits: {0}' -f $admittedCount),
        'Illegal FreeBufferQueue sends: 0',
        'ReadyQueue send failures: 0',
        'Processing notification failures: 0',
        'Token ledger errors: 0',
        'DMA error flags seen: 0',
        'ADC OVR seen: 0',
        ('Maximum nominal-to-decision cycles: {0}' -f $maxDecision),
        ('Maximum nominal-to-IRQ-exit cycles: {0}' -f $maxExit),
        ('Maximum protected final-window cycles: {0}' -f $maxFinal),
        ('Maximum ReadyQueue depth: {0}' -f $maxReadyDepth),
        'Minimum FreeBufferQueue depth after take: 0',
        'Final FreeBufferQueue depth: 1',
        'Final ReadyQueue depth: 0',
        ('Final M0 binding: B{0}' -f $m0Id),
        ('Final M1 binding: B{0}' -f $m1Id),
        ('Final FREE buffer: B{0}' -f $freeId),
        'Final ownership: 1 FREE + 2 DMA_OWNED',
        'Ownership violations: 0',
        'DMA-slot violations: 0',
        ('Completed admitted samples validated: {0}' -f
            ($admittedCount * 256)),
        'Sample errors: 0',
        'Canary errors: 0',
        ('Observed raw range across admitted blocks: {0} .. {1}' -f
            $globalRawMin, $globalRawMax),
        'Every DROP left M0AR/M1AR and mapping epoch unchanged: PASS',
        'No dropped block entered Processing: PASS',
        'Admissions resumed after controlled drop streaks: PASS',
        'Post-stop quiet counters: stable',
        '',
        'Classification: PASS'
    )

    [System.IO.File]::WriteAllLines(
        $classification,
        $classificationLines,
        $enc
    )

    # -------------------------------------------------------------------------
    # 10. Final repository guard
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 7. Final repository guard ==='

    $afterChanges = Get-ChangedPaths

    $afterDiff = Compare-Object `
        -ReferenceObject $expectedChanges `
        -DifferenceObject $afterChanges

    if ($afterDiff) {
        $afterDiff | Format-Table
        throw 'Hardware run changed repository source scope.'
    }

    if (@(& $git diff --cached --name-only).Count -ne 0) {
        throw 'Hardware run changed the Git index.'
    }

    if ((& $git rev-parse HEAD).Trim() -ne $expectedHead) {
        throw 'Hardware run changed HEAD.'
    }

    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R2-W5 REAL-HARDWARE CONTROLLED CAPACITY DROP: PASS'
    Write-Output '============================================================'
    Write-Output "Source HEAD: $expectedHead"
    Write-Output "W5 ELF SHA256: $elfHash"
    Write-Output 'Exact ELF flash/verify: PASS'
    Write-Output 'Input TC events: 64'
    Write-Output "Admitted blocks: $admittedCount"
    Write-Output "Controlled capacity drops: $dropCount"
    Write-Output 'Accounting: admitted + drops = 64 PASS'
    Write-Output "Processed/released admitted blocks: $processedCount / $releasedCount"
    Write-Output "Recovered admissions after drop streaks: $recoveries"
    Write-Output "Max consecutive drop streak: $maxDropStreak"
    Write-Output 'Every DROP changed M0AR/M1AR: NEVER'
    Write-Output 'Every DROP changed software mapping epoch: NEVER'
    Write-Output 'Dropped block entered Processing: NEVER'
    Write-Output 'FreeBufferQueue exhaustion observed: PASS'
    Write-Output 'DMA TE/DME/FE: 0'
    Write-Output 'ADC OVR: 0'
    Write-Output 'BufferPool violations: 0'
    Write-Output 'DMA-slot mapping violations: 0'
    Write-Output 'Token ledger errors: 0'
    Write-Output "Validated admitted samples: $($admittedCount * 256)"
    Write-Output 'Sample errors: 0'
    Write-Output 'Canary errors: 0'
    Write-Output "Max nominal-to-decision: $maxDecision cycles"
    Write-Output "Max nominal-to-IRQ-exit: $maxExit cycles"
    Write-Output "Max protected window: $maxFinal cycles"
    Write-Output "Final mapping: M0=B$m0Id, M1=B$m1Id"
    Write-Output "Final FREE buffer: B$freeId"
    Write-Output 'Final ownership: 1 FREE + 2 DMA_OWNED'
    Write-Output 'Post-stop quiet window: PASS'
    Write-Output 'Repository source/index/HEAD: unchanged'
    Write-Output ''
    Write-Output "Raw GDB result: $gdbOut"
    Write-Output "Trace CSV: $traceCsv"
    Write-Output "Classification: $classification"
    Write-Output ''
    Write-Output 'R2-W5 directed drop experiment: PASS'
    Write-Output 'R2 overall: IN PROGRESS'
    Write-Output 'No commit, push, or gate-tag operation.'
}
catch {
    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R2-W5 REAL-HARDWARE CONTROLLED CAPACITY DROP: FAIL'
    Write-Output '============================================================'

    $failurePath = Join-Path $BuildDir 'hardware-inspection.txt'

    if (Test-Path -LiteralPath $failurePath) {
        $failureText = [System.IO.File]::ReadAllText($failurePath)

        try {
            $phase = Get-Dec $failureText 'phase'
            $fault = Get-Hex $failureText 'fault_bits'
            $input = Get-Dec $failureText 'input_count'
            $admitted = Get-Dec $failureText 'admitted_count'
            $drops = Get-Dec $failureText 'capacity_drop_count'
            $processed = Get-Dec $failureText 'processed_count'

            Write-Output ("Firmware phase: {0}" -f $phase)
            Write-Output ("Fault bits: 0x{0:X8}" -f $fault)
            Write-Output ("Input events: {0}" -f $input)
            Write-Output ("Admitted: {0}" -f $admitted)
            Write-Output ("Drops: {0}" -f $drops)
            Write-Output ("Processed: {0}" -f $processed)
        }
        catch {
        }
    }

    Write-Output $_.Exception.Message
    Write-Output ''
    Write-Output 'Do not commit W5 or begin W6.'
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
