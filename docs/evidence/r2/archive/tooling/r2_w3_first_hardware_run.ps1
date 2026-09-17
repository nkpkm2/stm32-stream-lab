[CmdletBinding()]
param(
    [string]$Repo = 'E:\Projects\stm32-stream-lab',
    [string]$BuildDir = 'E:\Projects\stm32-stream-lab\build\r2-w3-w3target-23c9886a'
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

$expectedHead = 'a966cd3e78b4f190ca731daea8388463d39f7486'
$expectedR1Pass = '5ebf62e90b31e262f44013afb594a430061f139a'

$serverProc = $null

Set-Location -LiteralPath $Repo

Write-Output '=== R2-W3 FIRST REAL-HARDWARE BOUNDED REBINDING ==='

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
    # 1. Repository / gate / W3 source-state guard
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
        'docs/evidence/r2/w3/PLAN.md',
        'firmware/acquisition/r2_w3_guard.h',
        'firmware/acquisition/r2_w3_rebind.c',
        'firmware/acquisition/r2_w3_rebind.h',
        'firmware/cubemx/CMakeLists.txt',
        'firmware/cubemx/Core/Src/main.c',
        'firmware/cubemx/Core/Src/stm32f4xx_it.c',
        'tests/native/CMakeLists.txt',
        'tests/native/test_r2_w3_guard.c',
        'tools/r2/verify_w3.ps1'
    ) | Sort-Object

    $actualChanges = Get-ChangedPaths

    $scopeDiff = Compare-Object `
        -ReferenceObject $expectedChanges `
        -DifferenceObject $actualChanges

    if ($scopeDiff) {
        $scopeDiff | Format-Table
        throw 'Working tree is not the exact installed W3 source state.'
    }

    Write-Output "HEAD: $head"
    Write-Output "r1-pass -> $r1"
    Write-Output 'r2-pass: NOT CREATED'
    Write-Output 'W3 installed source scope: verified'
    Write-Output 'Staging area: unchanged'

    # -------------------------------------------------------------------------
    # 2. Exact W3 target-build guard
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 2. Exact W3 target-build guard ==='

    foreach ($tool in @($programmer, $gdbServer, $gdb, $nm)) {
        if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
            throw "Required tool missing: $tool"
        }
    }

    if (-not (Test-Path -LiteralPath $BuildDir -PathType Container)) {
        throw "W3 target build directory missing: $BuildDir"
    }

    $cache = Join-Path $BuildDir 'CMakeCache.txt'
    $summary = Join-Path $BuildDir 'verification-summary.txt'
    $elf = Join-Path $BuildDir 'cubemx.elf'

    foreach ($path in @($cache, $summary, $elf)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required W3 target-build artifact missing: $path"
        }
    }

    $cacheText = [System.IO.File]::ReadAllText($cache)
    if (-not $cacheText.Contains('STREAM_LAB_R2_W3:BOOL=ON')) {
        throw 'Selected build directory is not STREAM_LAB_R2_W3=ON.'
    }

    $summaryText = [System.IO.File]::ReadAllText($summary)
    if (-not $summaryText.Contains('Profile: W3Target') -or
        -not $summaryText.Contains('Build: PASS') -or
        -not $summaryText.Contains('Hardware: NOT RUN')) {
        throw 'Selected build directory is not a verified W3Target build.'
    }

    $symbolText = @(& $nm -g --defined-only $elf)
    if ($LASTEXITCODE -ne 0) {
        throw 'arm-none-eabi-nm failed.'
    }

    foreach ($symbol in @(
        'R2_W3_CreateTask',
        'R2_W3_IrqEnter',
        'R2_W3_IrqExit',
        'g_r2_w3_result'
    )) {
        if (($symbolText -join "`n") -notmatch
            ('(?m)\b' + [regex]::Escape($symbol) + '\r?$')) {
            throw "Required W3 symbol missing from ELF: $symbol"
        }
    }

    if (($symbolText -join "`n") -match '(?m)\bR1_Acquisition_Start\r?$') {
        throw 'R1 acquisition driver is unexpectedly linked into the W3-exclusive ELF.'
    }

    $elfHash = (
        Get-FileHash -LiteralPath $elf -Algorithm SHA256
    ).Hash

    Write-Output "Build directory: $BuildDir"
    Write-Output "W3 ELF SHA256: $elfHash"
    Write-Output 'STREAM_LAB_R2_W3: ON'
    Write-Output 'W3 symbols: verified'
    Write-Output 'R1 acquisition driver in W3 ELF: ABSENT'

    # -------------------------------------------------------------------------
    # 3. Flash / verify / reset exact W3 ELF
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 3. Flash exact W3 ELF ==='
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
        throw 'W3 flash / verify / reset failed.'
    }

    Write-Output 'SWD flash: PASS'
    Write-Output 'Flash verify: PASS'
    Write-Output 'Target reset: issued once'

    # Task waits 100 ms, experiment is about 10.24 ms, then finalizes.
    Start-Sleep -Seconds 2

    Write-Output 'Bounded experiment completion wait: complete'

    # -------------------------------------------------------------------------
    # 4. Build one-shot attach-mode GDB inspection
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 4. Capture frozen W3 result ==='

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

    Add-HexCommand $commands 'magic' 'g_r2_w3_result.magic'
    foreach ($field in @(
        'task_created',
        'phase',
        'test_pass'
    )) {
        Add-DecCommand $commands $field ('g_r2_w3_result.' + $field)
    }

    Add-HexCommand $commands 'fault_bits' 'g_r2_w3_result.fault_bits'

    foreach ($field in @(
        'first_fault_cycle',
        'first_fault_ndtr',
        'system_core_clock',
        'epoch_before',
        'epoch_after',
        'start_status',
        'stop_status',
        'start_dma_ndtr',
        'irq_count',
        'full_tc_count',
        'rebind_count',
        'bounded_stop_requested',
        'suppressed_stop_irqs',
        'dma_error_flags_seen',
        'adc_ovr_seen',
        'max_nominal_to_commit_cycles',
        'max_nominal_to_irq_exit_cycles',
        'max_final_window_cycles',
        'stop_ct',
        'stop_ndtr',
        'quiet_tc_count',
        'quiet_rebind_count',
        'quiet_irq_count',
        'full_sample_count',
        'sample_errors',
        'canary_errors'
    )) {
        Add-DecCommand $commands $field ('g_r2_w3_result.' + $field)
    }

    foreach ($field in @(
        'first_fault_dma_cr',
        'first_fault_lisr',
        'first_fault_adc_sr',
        'aircr',
        'start_dma_cr',
        'start_adc_cr2',
        'start_tim2_cr1',
        'stop_lisr_before_abort',
        'stop_lisr_after_abort',
        'stop_dma_cr',
        'stop_adc_cr2',
        'stop_tim2_cr1'
    )) {
        Add-HexCommand $commands $field ('g_r2_w3_result.' + $field)
    }

    for ($i = 0; $i -lt 10; $i++) {
        Add-HexCommand $commands ("buffer_address_$i") ("g_r2_w3_result.buffer_address[$i]")
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
        Add-DecCommand $commands ("pool_$field") ("g_r2_w3_result.pool_at_stop.$field")
    }

    for ($i = 0; $i -lt 10; $i++) {
        Add-DecCommand $commands ("pool_state_$i") ("g_r2_w3_result.pool_at_stop.states[$i]")
    }

    foreach ($field in @(
        'initialized',
        'mapping_epoch',
        'violation_count',
        'm0_buffer',
        'm1_buffer'
    )) {
        Add-DecCommand $commands ("slots_$field") ("g_r2_w3_result.slots_at_stop.$field")
    }

    for ($i = 0; $i -lt 8; $i++) {
        Add-DecCommand $commands ("raw_min_$i") ("g_r2_w3_result.raw_min[$i]")
        Add-DecCommand $commands ("raw_max_$i") ("g_r2_w3_result.raw_max[$i]")
    }

    $traceDecimalFields = @(
        'sequence',
        'ct_entry',
        'completed_slot',
        'completed_id',
        'replacement_id',
        'ct_prewrite',
        'ct_postwrite',
        'ndtr_prewrite',
        'nominal_offset_cycles',
        'irq_entry_offset_cycles',
        'decision_offset_cycles',
        'prewrite_offset_cycles',
        'commit_offset_cycles',
        'irq_exit_offset_cycles',
        'final_window_cycles',
        'guard_status',
        'write_performed',
        'readback_ok',
        'active_address_unchanged',
        'mapping_committed',
        'ownership_committed',
        'free_after',
        'ready_after',
        'mapping_epoch_after'
    )

    $traceHexFields = @(
        'm0_before',
        'm1_before',
        'm0_after',
        'm1_after',
        'lisr_entry'
    )

    for ($i = 0; $i -lt 8; $i++) {
        foreach ($field in $traceDecimalFields) {
            Add-DecCommand `
                $commands `
                ("trace_{0}_{1}" -f ($i + 1), $field) `
                ("g_r2_w3_result.trace[$i].$field")
        }

        foreach ($field in $traceHexFields) {
            Add-HexCommand `
                $commands `
                ("trace_{0}_{1}" -f ($i + 1), $field) `
                ("g_r2_w3_result.trace[$i].$field")
        }
    }

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
            Get-Content $gdbOut | Select-Object -Last 120
        }
        if (Test-Path -LiteralPath $gdbErr) {
            Get-Content $gdbErr | Select-Object -Last 120
        }
        throw 'GDB W3 inspection failed.'
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

    if ($magic -ne 0x52325733) {
        throw ("Invalid W3 magic: 0x{0:X8}" -f $magic)
    }

    if ((Get-Dec $text 'task_created') -ne 1) {
        throw 'W3 task was not created.'
    }

    if ($phase -ne 5) {
        Write-Output ("Firmware phase: {0}" -f $phase)
        Write-Output ("Firmware fault bits: 0x{0:X8}" -f $faultBits)
        throw 'W3 firmware did not reach COMPLETE.'
    }

    if ($testPass -ne 1 -or $faultBits -ne 0) {
        Write-Output ("Firmware fault bits: 0x{0:X8}" -f $faultBits)
        throw 'W3 firmware classifier did not pass.'
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
        throw 'W3 start or stop status is not HAL_OK.'
    }

    if ((Get-Hex $text 'start_dma_cr') -ne 0x00062D17) {
        throw 'Unexpected W3 start DMA CR.'
    }

    if ((Get-Dec $text 'start_dma_ndtr') -ne 256) {
        throw 'W3 did not start with NDTR=256.'
    }

    if ((Get-Hex $text 'start_adc_cr2') -ne 0x16000701) {
        throw 'Unexpected W3 start ADC CR2.'
    }

    if ((Get-Dec $text 'full_tc_count') -ne 8 -or
        (Get-Dec $text 'rebind_count') -ne 8 -or
        (Get-Dec $text 'bounded_stop_requested') -ne 1) {
        throw 'W3 did not complete exactly eight bounded rebinds.'
    }

    if ((Get-Dec $text 'dma_error_flags_seen') -ne 0 -or
        (Get-Dec $text 'adc_ovr_seen') -ne 0) {
        throw 'DMA error or ADC OVR was observed.'
    }

    if ((Get-Dec $text 'quiet_tc_count') -ne 8 -or
        (Get-Dec $text 'quiet_rebind_count') -ne 8) {
        throw 'Post-stop quiet counters are inconsistent.'
    }

    if ((Get-Dec $text 'quiet_irq_count') -ne
        (Get-Dec $text 'irq_count')) {
        throw 'IRQ count changed during post-stop quiet window.'
    }

    if ((Get-Dec $text 'full_sample_count') -ne 2048 -or
        (Get-Dec $text 'sample_errors') -ne 0 -or
        (Get-Dec $text 'canary_errors') -ne 0) {
        throw 'Sample or canary validation failed.'
    }

    if ((Get-Dec $text 'pool_activated') -ne 1 -or
        (Get-Dec $text 'pool_k') -ne 8 -or
        (Get-Dec $text 'pool_active_count') -ne 10 -or
        (Get-Dec $text 'pool_inactive_count') -ne 0 -or
        (Get-Dec $text 'pool_free_count') -ne 0 -or
        (Get-Dec $text 'pool_dma_owned_count') -ne 2 -or
        (Get-Dec $text 'pool_ready_count') -ne 8 -or
        (Get-Dec $text 'pool_processing_count') -ne 0 -or
        (Get-Dec $text 'pool_violation_count') -ne 0) {
        throw 'Final BufferPool snapshot violates W3 ownership invariants.'
    }

    for ($i = 0; $i -lt 8; $i++) {
        if ((Get-Dec $text ("pool_state_$i")) -ne 3) {
            throw "B$i is not READY at W3 stop."
        }
    }

    for ($i = 8; $i -lt 10; $i++) {
        if ((Get-Dec $text ("pool_state_$i")) -ne 2) {
            throw "B$i is not DMA_OWNED at W3 stop."
        }
    }

    if ((Get-Dec $text 'slots_initialized') -ne 1 -or
        (Get-Dec $text 'slots_mapping_epoch') -ne 9 -or
        (Get-Dec $text 'slots_violation_count') -ne 0 -or
        (Get-Dec $text 'slots_m0_buffer') -ne 8 -or
        (Get-Dec $text 'slots_m1_buffer') -ne 9) {
        throw 'Final DMA-slot mapping snapshot is invalid.'
    }

    $bufferAddress = @()
    for ($i = 0; $i -lt 10; $i++) {
        $address = Get-Hex $text ("buffer_address_$i")

        if (($address -band 3) -ne 0 -or
            $address -lt 0x20000000 -or
            $address -ge 0x20020000) {
            throw ("Invalid SRAM buffer address B{0}: 0x{1:X8}" -f $i, $address)
        }

        if ($bufferAddress -contains $address) {
            throw ("Duplicate physical buffer address detected for B{0}" -f $i)
        }

        $bufferAddress += $address
    }

    $expectedM0 = 0
    $expectedM1 = 1
    $traceRows = New-Object System.Collections.Generic.List[string]
    $traceRows.Add(
        'sequence,ct,completed_slot,completed_id,replacement_id,ndtr_prewrite,nominal_to_commit_cycles,nominal_to_irq_exit_cycles,final_window_cycles,free_after,ready_after,mapping_epoch_after'
    )

    $derivedMaxCommit = 0
    $derivedMaxExit = 0
    $derivedMaxFinal = 0

    for ($seq = 1; $seq -le 8; $seq++) {
        $prefix = "trace_${seq}_"

        $ct = [int](Get-Dec $text ($prefix + 'ct_entry'))
        $completedSlot = [int](Get-Dec $text ($prefix + 'completed_slot'))
        $completedId = [int](Get-Dec $text ($prefix + 'completed_id'))
        $replacementId = [int](Get-Dec $text ($prefix + 'replacement_id'))

        $expectedCt = $seq % 2
        $expectedCompletedSlot = $expectedCt -bxor 1

        if ((Get-Dec $text ($prefix + 'sequence')) -ne $seq -or
            $ct -ne $expectedCt -or
            $completedSlot -ne $expectedCompletedSlot -or
            $completedId -ne ($seq - 1) -or
            $replacementId -ne ($seq + 1)) {
            throw "Trace $seq event identity is invalid."
        }

        $m0Before = Get-Hex $text ($prefix + 'm0_before')
        $m1Before = Get-Hex $text ($prefix + 'm1_before')
        $m0After = Get-Hex $text ($prefix + 'm0_after')
        $m1After = Get-Hex $text ($prefix + 'm1_after')

        if ($m0Before -ne $bufferAddress[$expectedM0] -or
            $m1Before -ne $bufferAddress[$expectedM1]) {
            throw "Trace $seq pre-write M0/M1 mapping does not match the software model."
        }

        if ($completedSlot -eq 0) {
            $expectedM0 = $replacementId
        }
        else {
            $expectedM1 = $replacementId
        }

        if ($m0After -ne $bufferAddress[$expectedM0] -or
            $m1After -ne $bufferAddress[$expectedM1]) {
            throw "Trace $seq post-write M0/M1 mapping is invalid."
        }

        if ((Get-Dec $text ($prefix + 'ct_prewrite')) -ne $expectedCt -or
            (Get-Dec $text ($prefix + 'ct_postwrite')) -ne $expectedCt) {
            throw "Trace $seq CT changed across the protected write window."
        }

        $ndtr = Get-Dec $text ($prefix + 'ndtr_prewrite')
        if ($ndtr -lt 192 -or $ndtr -gt 256) {
            throw "Trace $seq NDTR safety guard failed: $ndtr"
        }

        foreach ($field in @(
            'write_performed',
            'readback_ok',
            'active_address_unchanged',
            'mapping_committed',
            'ownership_committed'
        )) {
            if ((Get-Dec $text ($prefix + $field)) -ne 1) {
                throw "Trace $seq failed assertion: $field"
            }
        }

        if ((Get-Dec $text ($prefix + 'guard_status')) -ne 0) {
            throw "Trace $seq guard_status is not OK."
        }

        if ((Get-Dec $text ($prefix + 'free_after')) -ne (8 - $seq) -or
            (Get-Dec $text ($prefix + 'ready_after')) -ne $seq -or
            (Get-Dec $text ($prefix + 'mapping_epoch_after')) -ne ($seq + 1)) {
            throw "Trace $seq ownership/mapping counters are invalid."
        }

        $nominal = Get-Dec $text ($prefix + 'nominal_offset_cycles')
        $entry = Get-Dec $text ($prefix + 'irq_entry_offset_cycles')
        $decision = Get-Dec $text ($prefix + 'decision_offset_cycles')
        $prewrite = Get-Dec $text ($prefix + 'prewrite_offset_cycles')
        $commit = Get-Dec $text ($prefix + 'commit_offset_cycles')
        $exit = Get-Dec $text ($prefix + 'irq_exit_offset_cycles')
        $finalWindow = Get-Dec $text ($prefix + 'final_window_cycles')

        if ($nominal -ne ($seq * 230400)) {
            throw "Trace $seq nominal cycle marker is invalid."
        }

        if ($entry -lt $nominal -or
            $decision -lt $entry -or
            $prewrite -lt $decision -or
            $commit -lt $prewrite -or
            $exit -lt $commit) {
            throw "Trace $seq timing markers are not monotonic."
        }

        $nominalToCommit = $commit - $nominal
        $nominalToExit = $exit - $nominal

        if ($nominalToCommit -gt 57600) {
            throw "Trace $seq nominal-to-commit budget exceeded: $nominalToCommit"
        }

        if ($nominalToExit -gt 80640) {
            throw "Trace $seq nominal-to-exit diagnostic budget exceeded: $nominalToExit"
        }

        if ($finalWindow -gt 3600) {
            throw "Trace $seq final protected window exceeded: $finalWindow"
        }

        if ($nominalToCommit -gt $derivedMaxCommit) {
            $derivedMaxCommit = $nominalToCommit
        }
        if ($nominalToExit -gt $derivedMaxExit) {
            $derivedMaxExit = $nominalToExit
        }
        if ($finalWindow -gt $derivedMaxFinal) {
            $derivedMaxFinal = $finalWindow
        }

        $traceRows.Add(
            ('{0},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10},{11}' -f
                $seq,
                $ct,
                $completedSlot,
                $completedId,
                $replacementId,
                $ndtr,
                $nominalToCommit,
                $nominalToExit,
                $finalWindow,
                (8 - $seq),
                $seq,
                ($seq + 1))
        )
    }

    if ($expectedM0 -ne 8 -or $expectedM1 -ne 9) {
        throw 'Derived final M0/M1 mapping is not B8/B9.'
    }

    if ((Get-Dec $text 'max_nominal_to_commit_cycles') -ne $derivedMaxCommit -or
        (Get-Dec $text 'max_nominal_to_irq_exit_cycles') -ne $derivedMaxExit -or
        (Get-Dec $text 'max_final_window_cycles') -ne $derivedMaxFinal) {
        throw 'Firmware maximum timing summaries do not match the captured trace.'
    }

    if (($derivedMaxCommit * 4) -gt 230400) {
        throw 'Observed nominal-to-commit maximum exceeds 0.25 TB.'
    }

    for ($i = 0; $i -lt 8; $i++) {
        $lo = Get-Dec $text ("raw_min_$i")
        $hi = Get-Dec $text ("raw_max_$i")

        if ($lo -gt $hi -or $hi -gt 4095) {
            throw "Invalid ADC range for completed buffer B$i."
        }
    }

    # -------------------------------------------------------------------------
    # 6. Preserve local machine-readable run outputs
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 6. Preserve local run outputs ==='

    $traceCsv = Join-Path $BuildDir 'hardware-trace.csv'
    $classification = Join-Path $BuildDir 'hardware-classification.txt'

    [System.IO.File]::WriteAllLines($traceCsv, $traceRows, $enc)

    $summaryLines = @(
        'R2-W3 bounded dynamic-DBM hardware classification',
        '',
        ('Source HEAD: {0}' -f $expectedHead),
        ('W3 ELF SHA256: {0}' -f $elfHash),
        'Input: PA0 / ADC1_IN0 connected to GND',
        'K: 8',
        'P: 10',
        'Sample rate: 200 kS/s',
        'Block samples: 256',
        'Rebind events: 8 / 8',
        ('IRQ count: {0}' -f (Get-Dec $text 'irq_count')),
        ('Suppressed stop IRQs: {0}' -f (Get-Dec $text 'suppressed_stop_irqs')),
        'DMA error flags seen: 0',
        'ADC OVR seen: 0',
        ('Max nominal-to-commit cycles: {0}' -f $derivedMaxCommit),
        ('Max nominal-to-IRQ-exit cycles: {0}' -f $derivedMaxExit),
        ('Max protected final-window cycles: {0}' -f $derivedMaxFinal),
        'Final M0 binding: B8',
        'Final M1 binding: B9',
        'Final READY buffers: 8',
        'Final DMA_OWNED buffers: 2',
        'Ownership violations: 0',
        'DMA-slot violations: 0',
        'Completed samples validated: 2048',
        'Sample errors: 0',
        'Canary errors: 0',
        'Post-stop quiet counts: stable',
        '',
        'Classification: PASS'
    )

    [System.IO.File]::WriteAllLines($classification, $summaryLines, $enc)

    # -------------------------------------------------------------------------
    # 7. Final repository guard
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
    Write-Output 'R2-W3 REAL-HARDWARE BOUNDED REBINDING: PASS'
    Write-Output '============================================================'
    Write-Output "Source HEAD: $expectedHead"
    Write-Output "W3 ELF SHA256: $elfHash"
    Write-Output 'Exact ELF flash/verify: PASS'
    Write-Output 'Dynamic inactive-MxAR rebinds: 8 / 8 PASS'
    Write-Output 'Physical buffer rotation: B0/B1 -> B2..B9 PASS'
    Write-Output 'Active-slot address changed by software: NEVER'
    Write-Output 'CT stable across protected writes: PASS'
    Write-Output 'DMA TE/DME/FE: 0'
    Write-Output 'ADC OVR: 0'
    Write-Output 'BufferPool violations: 0'
    Write-Output 'DMA-slot mapping violations: 0'
    Write-Output 'Completed samples checked: 2048'
    Write-Output 'Sample errors: 0'
    Write-Output 'Canary errors: 0'
    Write-Output "Max nominal-to-commit: $derivedMaxCommit cycles"
    Write-Output "Max nominal-to-IRQ-exit marker: $derivedMaxExit cycles"
    Write-Output "Max protected write window: $derivedMaxFinal cycles"
    Write-Output 'Post-stop quiet window: PASS'
    Write-Output 'Final mapping: M0=B8, M1=B9'
    Write-Output 'Repository source/index/HEAD: unchanged'
    Write-Output ''
    Write-Output "Raw GDB result: $gdbOut"
    Write-Output "Trace CSV: $traceCsv"
    Write-Output "Classification: $classification"
    Write-Output ''
    Write-Output 'R2-W3 bounded hardware experiment: PASS'
    Write-Output 'R2 overall: IN PROGRESS'
    Write-Output 'No commit, push, or gate-tag operation.'
}
catch {
    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R2-W3 REAL-HARDWARE BOUNDED REBINDING: FAIL'
    Write-Output '============================================================'

    if (Test-Path -LiteralPath (Join-Path $BuildDir 'hardware-inspection.txt')) {
        $failureText = [System.IO.File]::ReadAllText(
            (Join-Path $BuildDir 'hardware-inspection.txt')
        )

        try {
            $fault = Get-Hex $failureText 'fault_bits'
            $phase = Get-Dec $failureText 'phase'
            $tc = Get-Dec $failureText 'full_tc_count'
            $rebind = Get-Dec $failureText 'rebind_count'

            Write-Output ("Firmware phase: {0}" -f $phase)
            Write-Output ("Fault bits: 0x{0:X8}" -f $fault)
            Write-Output ("TC count: {0}" -f $tc)
            Write-Output ("Rebind count: {0}" -f $rebind)
        }
        catch {
        }
    }

    Write-Output $_.Exception.Message
    Write-Output ''
    Write-Output 'Do not commit W3 or begin W4.'
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
