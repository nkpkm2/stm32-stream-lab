$ErrorActionPreference = 'Stop'

$repo = 'E:\Projects\stm32-stream-lab'
$git = 'C:\Program Files\Git\cmd\git.exe'

$programmerRoot = 'E:\DevTools\STM32CubeProgrammer-2.23.0'
$server = 'E:\DevTools\STM32CubeCLT-1.22.0\STLink-gdb-server\bin\ST-LINK_gdbserver.exe'
$armBin = 'E:\DevTools\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi\bin'
$gdb = Join-Path $armBin 'arm-none-eabi-gdb.exe'
$nm = Join-Path $armBin 'arm-none-eabi-nm.exe'

$serial = '067AFF545754655087043860'
$gdbPort = 61234

$expectedHead = '5ebf62e90b31e262f44013afb594a430061f139a'
$expectedElfHash = '67040A73D2072C569918FA3E3AB9B3F88B52C0901E211665ED99462BF33E6CBA'
$expectedR0Pass = '9ade715d6f3035cd60512bf2ec4dd1c226436af8'

$build = Join-Path $repo 'build\r1-soak-01'
$elf = Join-Path $build 'cubemx.elf'

$gdbScriptA = Join-Path $build 'r1-final-regression-v2-a.gdb'
$gdbScriptB = Join-Path $build 'r1-final-regression-v2-b.gdb'
$gdbOutA = Join-Path $build 'r1-final-regression-v2-a.txt'
$gdbOutB = Join-Path $build 'r1-final-regression-v2-b.txt'
$gdbErrA = Join-Path $build 'r1-final-regression-v2-a.stderr.txt'
$gdbErrB = Join-Path $build 'r1-final-regression-v2-b.stderr.txt'

$serverOutA = Join-Path $build 'r1-final-regression-v2-server-a.stdout.txt'
$serverErrA = Join-Path $build 'r1-final-regression-v2-server-a.stderr.txt'
$serverOutB = Join-Path $build 'r1-final-regression-v2-server-b.stdout.txt'
$serverErrB = Join-Path $build 'r1-final-regression-v2-server-b.stderr.txt'

$vcpCapturePath = Join-Path $build 'r1-final-regression-v2-vcp.txt'
$summaryPath = Join-Path $build 'r1-final-focused-regression-v2.txt'

$serialPort = $null

Set-Location -LiteralPath $repo

Write-Output '=== R1 FINAL FOCUSED HARDWARE REGRESSION V2 ==='

function Invoke-GdbSnapshot {
    param(
        [string]$CommandFile,
        [string]$GdbOut,
        [string]$GdbErr,
        [string]$ServerOut,
        [string]$ServerErr
    )

    $serverProc = $null

    try {
        $existingServer = @(
            Get-Process -Name 'ST-LINK_gdbserver' -ErrorAction SilentlyContinue
        )

        if ($existingServer.Count -ne 0) {
            throw 'An ST-LINK GDB server is already running.'
        }

        $serverArgs = @(
            '-d',
            '-m', '0',
            '-p', "$gdbPort",
            '-l', '1',
            '-i', $serial,
            '-cp', (Join-Path $programmerRoot 'bin'),
            '-g'
        )

        $serverProc = Start-Process `
            -FilePath $server `
            -ArgumentList $serverArgs `
            -PassThru `
            -NoNewWindow `
            -RedirectStandardOutput $ServerOut `
            -RedirectStandardError $ServerErr

        Start-Sleep -Seconds 2
        $serverProc.Refresh()

        if ($serverProc.HasExited) {
            if (Test-Path -LiteralPath $ServerOut) {
                Get-Content $ServerOut | Select-Object -Last 80
            }

            if (Test-Path -LiteralPath $ServerErr) {
                Get-Content $ServerErr | Select-Object -Last 80
            }

            throw "ST-LINK GDB server exited before GDB connected."
        }

        $gdbProc = Start-Process `
            -FilePath $gdb `
            -ArgumentList @('-q', '-batch', '-x', $CommandFile, $elf) `
            -Wait `
            -PassThru `
            -NoNewWindow `
            -RedirectStandardOutput $GdbOut `
            -RedirectStandardError $GdbErr

        $gdbProc.Refresh()

        if ($gdbProc.ExitCode -ne 0) {
            if (Test-Path -LiteralPath $GdbOut) {
                Get-Content $GdbOut | Select-Object -Last 100
            }

            if (Test-Path -LiteralPath $GdbErr) {
                Get-Content $GdbErr | Select-Object -Last 100
            }

            throw "GDB snapshot failed with exit code $($gdbProc.ExitCode)."
        }
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

        Start-Sleep -Milliseconds 300
    }
}

try {
    # -------------------------------------------------------------------------
    # 1. Exact committed-state guard
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 1. Exact committed-state guard ==='

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

    $r0Pass = (& $git rev-parse 'r0-pass^{commit}').Trim()

    if ($r0Pass -ne $expectedR0Pass) {
        throw "r0-pass moved unexpectedly: $r0Pass"
    }

    $localR1Pass = @(& $git tag --list 'r1-pass')

    if ($localR1Pass.Count -ne 0) {
        throw 'r1-pass exists unexpectedly.'
    }

    if (-not (Test-Path -LiteralPath $elf -PathType Leaf)) {
        throw "Committed-state ELF is missing: $elf"
    }

    $elfHash = (
        Get-FileHash `
            -LiteralPath $elf `
            -Algorithm SHA256
    ).Hash

    if ($elfHash -ne $expectedElfHash) {
        throw "Committed-state ELF SHA256 mismatch: $elfHash"
    }

    Write-Output "HEAD: $head"
    Write-Output "ELF SHA256: $elfHash"
    Write-Output "r0-pass: $r0Pass"
    Write-Output 'r1-pass: NOT CREATED'

    # -------------------------------------------------------------------------
    # 2. Tool and debug-symbol guard
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 2. Tool and debug-symbol guard ==='

    $cliCandidate = Join-Path $programmerRoot 'bin\STM32_Programmer_CLI.exe'

    if (Test-Path -LiteralPath $cliCandidate -PathType Leaf) {
        $cli = $cliCandidate
    }
    else {
        $matches = @(
            Get-ChildItem `
                -LiteralPath $programmerRoot `
                -Recurse `
                -File `
                -Filter 'STM32_Programmer_CLI.exe'
        )

        if ($matches.Count -ne 1) {
            throw "Expected one STM32_Programmer_CLI.exe, found: $($matches.Count)"
        }

        $cli = $matches[0].FullName
    }

    foreach ($path in @($cli, $server, $gdb, $nm)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required tool is missing: $path"
        }
    }

    $symbols = @(& $nm -a $elf)

    if ($LASTEXITCODE -ne 0) {
        throw 'nm failed on committed-state ELF.'
    }

    foreach ($symbol in @(
        'SystemCoreClock',
        'uwTick',
        'xTickCount',
        'xSchedulerRunning',
        'g_r1_soak_result',
        'r1_buffer0',
        'r1_buffer1'
    )) {
        $hit = @(
            $symbols |
            Where-Object {
                $_ -match ("\b{0}$" -f [regex]::Escape($symbol))
            }
        )

        if ($hit.Count -ne 1) {
            throw "Expected one ELF symbol for $symbol, found: $($hit.Count)"
        }

        Write-Output "PASS  symbol: $symbol"
    }

    # -------------------------------------------------------------------------
    # 3. Detect ST-LINK VCP
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 3. Detect ST-LINK Virtual COM port ==='

    $comPort = $null

    if (-not [string]::IsNullOrWhiteSpace($env:R1_VCP_PORT)) {
        $comPort = $env:R1_VCP_PORT.Trim()
        Write-Output "Using R1_VCP_PORT override: $comPort"
    }
    else {
        $serialCandidates = @(
            Get-CimInstance Win32_SerialPort |
            Where-Object {
                ($_.Name -match 'STLink|ST-LINK|STMicroelectronics') -or
                ($_.Description -match 'STLink|ST-LINK|STMicroelectronics')
            }
        )

        if ($serialCandidates.Count -eq 1) {
            $comPort = $serialCandidates[0].DeviceID
        }
        elseif ($serialCandidates.Count -gt 1) {
            $serialCandidates |
                Select-Object DeviceID, Name, Description |
                Format-Table

            throw 'Multiple ST-LINK serial ports were found. Set R1_VCP_PORT and retry.'
        }
        else {
            $pnpCandidates = @(
                Get-CimInstance Win32_PnPEntity |
                Where-Object {
                    ($_.Name -match 'STLink|ST-LINK') -and
                    ($_.Name -match '\(COM[0-9]+\)')
                }
            )

            if ($pnpCandidates.Count -eq 1) {
                $match = [regex]::Match(
                    $pnpCandidates[0].Name,
                    '\((COM[0-9]+)\)'
                )

                if ($match.Success) {
                    $comPort = $match.Groups[1].Value
                }
            }
        }
    }

    if ([string]::IsNullOrWhiteSpace($comPort)) {
        throw 'ST-LINK Virtual COM port could not be identified automatically.'
    }

    Write-Output "ST-LINK VCP: $comPort"

    # -------------------------------------------------------------------------
    # 4. Open VCP before reset
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 4. Arm VCP startup capture ==='

    $serialPort = New-Object System.IO.Ports.SerialPort
    $serialPort.PortName = $comPort
    $serialPort.BaudRate = 115200
    $serialPort.Parity = [System.IO.Ports.Parity]::None
    $serialPort.DataBits = 8
    $serialPort.StopBits = [System.IO.Ports.StopBits]::One
    $serialPort.Handshake = [System.IO.Ports.Handshake]::None
    $serialPort.ReadTimeout = 200
    $serialPort.WriteTimeout = 200

    $serialPort.Open()
    $serialPort.DiscardInBuffer()

    Write-Output 'VCP opened before target reset'

    # -------------------------------------------------------------------------
    # 5. Flash / verify / reset exact ELF
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 5. Flash exact committed-state ELF ==='

    $flashOut = Join-Path $build 'r1-final-regression-v2-flash.stdout.txt'
    $flashErr = Join-Path $build 'r1-final-regression-v2-flash.stderr.txt'

    $flashArgs = @(
        '-c',
        'port=SWD',
        'freq=4000',
        "sn=$serial",
        '-w',
        $elf,
        '-v',
        '-rst'
    )

    $flashProc = Start-Process `
        -FilePath $cli `
        -ArgumentList $flashArgs `
        -Wait `
        -PassThru `
        -NoNewWindow `
        -RedirectStandardOutput $flashOut `
        -RedirectStandardError $flashErr

    $flashProc.Refresh()

    if ($flashProc.ExitCode -ne 0) {
        if (Test-Path -LiteralPath $flashOut) {
            Get-Content $flashOut | Select-Object -Last 80
        }

        if (Test-Path -LiteralPath $flashErr) {
            Get-Content $flashErr | Select-Object -Last 40
        }

        throw 'Exact committed-state flash / verify / reset failed.'
    }

    Start-Sleep -Seconds 2

    $vcpText = $serialPort.ReadExisting()

    [System.IO.File]::WriteAllText(
        $vcpCapturePath,
        $vcpText,
        (New-Object System.Text.UTF8Encoding($false))
    )

    $serialPort.Close()
    $serialPort.Dispose()
    $serialPort = $null

    if (-not $vcpText.Contains('P0-B VCP READY')) {
        throw 'R0 VCP startup banner was not observed after reset.'
    }

    Write-Output 'SWD flash/verify: PASS'
    Write-Output 'Target reset: issued once'
    Write-Output 'VCP startup banner: PASS'

    # -------------------------------------------------------------------------
    # 6. Create snapshot scripts
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 6. Create focused debugger snapshots ==='

    $enc = New-Object System.Text.UTF8Encoding($false)

    $commandsA = @(
        'set pagination off',
        'set confirm off',
        ('target remote 127.0.0.1:{0}' -f $gdbPort),
        'printf "a_system_core_clock=%u\n", (unsigned int)SystemCoreClock',
        'printf "a_uwtick=%u\n", (unsigned int)uwTick',
        'printf "a_xtick=%u\n", (unsigned int)xTickCount',
        'printf "a_scheduler_running=%u\n", (unsigned int)xSchedulerRunning',
        'printf "a_r1_magic=0x%08x\n", (unsigned int)g_r1_soak_result.magic',
        'printf "a_r1_task_created=%u\n", (unsigned int)g_r1_soak_result.task_created',
        'printf "a_r1_phase=%u\n", (unsigned int)g_r1_soak_result.phase',
        'printf "a_r1_start_status=%u\n", (unsigned int)g_r1_soak_result.start_status',
        'printf "a_r1_tim2_running=%u\n", (unsigned int)g_r1_soak_result.tim2_running_after_start',
        'printf "a_buffer0=0x%08x\n", (unsigned int)&r1_buffer0[0]',
        'printf "a_buffer1=0x%08x\n", (unsigned int)&r1_buffer1[0]',
        'printf "a_tim2_cr1=0x%08x\n", *(unsigned int*)0x40000000',
        'printf "a_tim2_cr2=0x%08x\n", *(unsigned int*)0x40000004',
        'printf "a_tim2_psc=%u\n", *(unsigned int*)0x40000028',
        'printf "a_tim2_arr=%u\n", *(unsigned int*)0x4000002c',
        'printf "a_adc_sr=0x%08x\n", *(unsigned int*)0x40012000',
        'printf "a_adc_cr2=0x%08x\n", *(unsigned int*)0x40012008',
        'printf "a_dma_lisr=0x%08x\n", *(unsigned int*)0x40026400',
        'printf "a_dma_cr=0x%08x\n", *(unsigned int*)0x40026410',
        'printf "a_dma_ndtr=%u\n", *(unsigned int*)0x40026414',
        'printf "a_dma_m0ar=0x%08x\n", *(unsigned int*)0x4002641c',
        'printf "a_dma_m1ar=0x%08x\n", *(unsigned int*)0x40026420',
        'printf "a_tim7_cr1=0x%08x\n", *(unsigned int*)0x40001400',
        'printf "a_tim7_psc=%u\n", *(unsigned int*)0x40001428',
        'printf "a_tim7_arr=%u\n", *(unsigned int*)0x4000142c',
        'printf "a_aircr=0x%08x\n", *(unsigned int*)0xe000ed0c',
        'printf "a_systick_ctrl=0x%08x\n", *(unsigned int*)0xe000e010',
        'printf "a_systick_load=%u\n", *(unsigned int*)0xe000e014',
        'printf "a_svc_priority=%u\n", *(unsigned char*)0xe000ed1f',
        'printf "a_pendsv_priority=%u\n", *(unsigned char*)0xe000ed22',
        'printf "a_systick_priority=%u\n", *(unsigned char*)0xe000ed23',
        'printf "a_tim7_priority=%u\n", *(unsigned char*)0xe000e437',
        'printf "a_dma2_stream0_priority=%u\n", *(unsigned char*)0xe000e438',
        'detach',
        'quit'
    )

    $commandsB = @(
        'set pagination off',
        'set confirm off',
        ('target remote 127.0.0.1:{0}' -f $gdbPort),
        'printf "b_uwtick=%u\n", (unsigned int)uwTick',
        'printf "b_xtick=%u\n", (unsigned int)xTickCount',
        'printf "b_scheduler_running=%u\n", (unsigned int)xSchedulerRunning',
        'printf "b_r1_phase=%u\n", (unsigned int)g_r1_soak_result.phase',
        'printf "b_tim2_cr1=0x%08x\n", *(unsigned int*)0x40000000',
        'printf "b_adc_sr=0x%08x\n", *(unsigned int*)0x40012000',
        'printf "b_dma_lisr=0x%08x\n", *(unsigned int*)0x40026400',
        'printf "b_dma_cr=0x%08x\n", *(unsigned int*)0x40026410',
        'printf "b_dma_ndtr=%u\n", *(unsigned int*)0x40026414',
        'printf "b_tim7_cr1=0x%08x\n", *(unsigned int*)0x40001400',
        'detach',
        'quit'
    )

    [System.IO.File]::WriteAllLines($gdbScriptA, $commandsA, $enc)
    [System.IO.File]::WriteAllLines($gdbScriptB, $commandsB, $enc)

    Write-Output 'Debugger snapshot scripts: created'

    # -------------------------------------------------------------------------
    # 7. Snapshot A with dedicated server
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 7. Runtime snapshot A ==='

    Invoke-GdbSnapshot `
        -CommandFile $gdbScriptA `
        -GdbOut $gdbOutA `
        -GdbErr $gdbErrA `
        -ServerOut $serverOutA `
        -ServerErr $serverErrA

    Write-Output 'Snapshot A: PASS'

    # Let the detached target run for a real interval.
    Start-Sleep -Milliseconds 500

    # -------------------------------------------------------------------------
    # 8. Snapshot B with a new dedicated server
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 8. Runtime snapshot B ==='

    Invoke-GdbSnapshot `
        -CommandFile $gdbScriptB `
        -GdbOut $gdbOutB `
        -GdbErr $gdbErrB `
        -ServerOut $serverOutB `
        -ServerErr $serverErrB

    Write-Output 'Snapshot B: PASS'

    $textA = [System.IO.File]::ReadAllText($gdbOutA)
    $textB = [System.IO.File]::ReadAllText($gdbOutB)

    # -------------------------------------------------------------------------
    # 9. Parse helpers
    # -------------------------------------------------------------------------
    function Get-DecimalField {
        param(
            [string]$Text,
            [string]$Name
        )

        $match = [regex]::Match(
            $Text,
            '(?m)^' + [regex]::Escape($Name) + '=([0-9]+)\r?$'
        )

        if (-not $match.Success) {
            throw "Missing decimal field: $Name"
        }

        return [uint64]$match.Groups[1].Value
    }

    function Get-HexField {
        param(
            [string]$Text,
            [string]$Name
        )

        $match = [regex]::Match(
            $Text,
            '(?m)^' + [regex]::Escape($Name) + '=0x([0-9a-fA-F]+)\r?$'
        )

        if (-not $match.Success) {
            throw "Missing hexadecimal field: $Name"
        }

        return [Convert]::ToUInt64($match.Groups[1].Value, 16)
    }

    # -------------------------------------------------------------------------
    # 10. R0 focused regression
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 9. R0 focused regression classification ==='

    $systemCoreClock = Get-DecimalField $textA 'a_system_core_clock'
    $aircr = Get-HexField $textA 'a_aircr'
    $prigroup = ($aircr -shr 8) -band 7

    $tim7Cr1 = Get-HexField $textA 'a_tim7_cr1'
    $tim7Psc = Get-DecimalField $textA 'a_tim7_psc'
    $tim7Arr = Get-DecimalField $textA 'a_tim7_arr'

    $systickCtrl = Get-HexField $textA 'a_systick_ctrl'
    $systickLoad = Get-DecimalField $textA 'a_systick_load'

    $svcPriority = Get-DecimalField $textA 'a_svc_priority'
    $pendsvPriority = Get-DecimalField $textA 'a_pendsv_priority'
    $systickPriority = Get-DecimalField $textA 'a_systick_priority'
    $tim7Priority = Get-DecimalField $textA 'a_tim7_priority'
    $dmaPriority = Get-DecimalField $textA 'a_dma2_stream0_priority'

    $schedulerA = Get-DecimalField $textA 'a_scheduler_running'
    $schedulerB = Get-DecimalField $textB 'b_scheduler_running'

    $uwTickA = Get-DecimalField $textA 'a_uwtick'
    $uwTickB = Get-DecimalField $textB 'b_uwtick'
    $xTickA = Get-DecimalField $textA 'a_xtick'
    $xTickB = Get-DecimalField $textB 'b_xtick'

    $uwTickDelta = [int64]$uwTickB - [int64]$uwTickA
    $xTickDelta = [int64]$xTickB - [int64]$xTickA

    if ($systemCoreClock -ne 180000000) {
        throw "R0 clock regression: SystemCoreClock=$systemCoreClock"
    }

    if ($prigroup -ne 3) {
        throw "R0 NVIC grouping regression: PRIGROUP=$prigroup"
    }

    if (($tim7Cr1 -band 1) -eq 0 -or
        $tim7Psc -ne 89 -or
        $tim7Arr -ne 999) {
        throw 'R0 TIM7 HAL-tick timer configuration regressed.'
    }

    if (($systickCtrl -band 7) -ne 7 -or
        $systickLoad -ne 179999) {
        throw 'R0 SysTick kernel-tick configuration regressed.'
    }

    if ($svcPriority -ne 0 -or
        $pendsvPriority -ne 240 -or
        $systickPriority -ne 240 -or
        $tim7Priority -ne 0 -or
        $dmaPriority -ne 80) {
        throw 'R0/R1 interrupt-priority contract regressed.'
    }

    if ($schedulerA -ne 1 -or $schedulerB -ne 1) {
        throw 'FreeRTOS scheduler is not running.'
    }

    if ($uwTickDelta -lt 100 -or $uwTickDelta -gt 5000) {
        throw "HAL tick did not advance correctly: delta=$uwTickDelta"
    }

    if ($xTickDelta -lt 100 -or $xTickDelta -gt 5000) {
        throw "FreeRTOS tick did not advance correctly: delta=$xTickDelta"
    }

    Write-Output "SystemCoreClock: $systemCoreClock"
    Write-Output "AIRCR.PRIGROUP: $prigroup"
    Write-Output "TIM7: CEN=1, PSC=$tim7Psc, ARR=$tim7Arr"
    Write-Output "SysTick: CTRL lower bits=7, LOAD=$systickLoad"
    Write-Output "HAL tick delta: $uwTickDelta"
    Write-Output "FreeRTOS tick delta: $xTickDelta"
    Write-Output 'FreeRTOS scheduler: RUNNING'
    Write-Output 'IRQ priority contract: PASS'
    Write-Output 'VCP startup banner: PASS'

    # -------------------------------------------------------------------------
    # 11. R1 live acquisition sanity
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 10. R1 live acquisition sanity classification ==='

    $r1Magic = Get-HexField $textA 'a_r1_magic'
    $r1TaskCreated = Get-DecimalField $textA 'a_r1_task_created'
    $r1PhaseA = Get-DecimalField $textA 'a_r1_phase'
    $r1PhaseB = Get-DecimalField $textB 'b_r1_phase'
    $r1StartStatus = Get-DecimalField $textA 'a_r1_start_status'
    $r1Tim2Running = Get-DecimalField $textA 'a_r1_tim2_running'

    $buffer0 = Get-HexField $textA 'a_buffer0'
    $buffer1 = Get-HexField $textA 'a_buffer1'

    $tim2Cr1 = Get-HexField $textA 'a_tim2_cr1'
    $tim2Cr2 = Get-HexField $textA 'a_tim2_cr2'
    $tim2Psc = Get-DecimalField $textA 'a_tim2_psc'
    $tim2Arr = Get-DecimalField $textA 'a_tim2_arr'

    $adcSrA = Get-HexField $textA 'a_adc_sr'
    $adcSrB = Get-HexField $textB 'b_adc_sr'
    $adcCr2 = Get-HexField $textA 'a_adc_cr2'

    $dmaLisrA = Get-HexField $textA 'a_dma_lisr'
    $dmaLisrB = Get-HexField $textB 'b_dma_lisr'
    $dmaCrA = Get-HexField $textA 'a_dma_cr'
    $dmaCrB = Get-HexField $textB 'b_dma_cr'
    $dmaNdtrA = Get-DecimalField $textA 'a_dma_ndtr'
    $dmaNdtrB = Get-DecimalField $textB 'b_dma_ndtr'
    $dmaM0ar = Get-HexField $textA 'a_dma_m0ar'
    $dmaM1ar = Get-HexField $textA 'a_dma_m1ar'

    if ($r1Magic -ne 0x52315331 -or
        $r1TaskCreated -ne 1 -or
        $r1PhaseA -ne 3 -or
        $r1PhaseB -ne 3 -or
        $r1StartStatus -ne 0 -or
        $r1Tim2Running -ne 1) {
        throw 'R1 soak task did not reach and remain in RUNNING state.'
    }

    if (($tim2Cr1 -band 1) -eq 0 -or
        ($tim2Cr2 -band 0x70) -ne 0x20 -or
        $tim2Psc -ne 0 -or
        $tim2Arr -ne 449) {
        throw 'R1 TIM2 200 kHz trigger configuration regressed.'
    }

    if ($adcCr2 -ne 0x16000701) {
        throw ("R1 ADC1 CR2 regression: observed 0x{0:x8}" -f $adcCr2)
    }

    if (($adcSrA -band 0x20) -ne 0 -or
        ($adcSrB -band 0x20) -ne 0) {
        throw 'R1 ADC overrun flag was observed.'
    }

    foreach ($dmaCr in @($dmaCrA, $dmaCrB)) {
        $ct = ($dmaCr -shr 19) -band 1
        $withoutCt = $dmaCr - ($ct * 0x80000)

        if ($withoutCt -ne 0x62d17) {
            throw ("R1 DMA2 Stream0 CR regression: observed 0x{0:x8}" -f $dmaCr)
        }
    }

    foreach ($ndtr in @($dmaNdtrA, $dmaNdtrB)) {
        if ($ndtr -lt 1 -or $ndtr -gt 256) {
            throw "R1 DMA NDTR is outside active DBM range: $ndtr"
        }
    }

    if ($dmaM0ar -ne $buffer0 -or
        $dmaM1ar -ne $buffer1) {
        throw 'R1 DMA M0AR/M1AR no longer match the fixed buffers.'
    }

    if (($dmaLisrA -band 0x0d) -ne 0 -or
        ($dmaLisrB -band 0x0d) -ne 0) {
        throw 'R1 DMA FE/DME/TE error flag was observed.'
    }

    $tim2Cr1B = Get-HexField $textB 'b_tim2_cr1'
    $tim7Cr1B = Get-HexField $textB 'b_tim7_cr1'

    if (($tim2Cr1B -band 1) -eq 0 -or
        ($tim7Cr1B -band 1) -eq 0) {
        throw 'R1/R0 timers were not still running at snapshot B.'
    }

    Write-Output 'R1 soak task phase: RUNNING'
    Write-Output 'TIM2 trigger configuration: PASS'
    Write-Output 'ADC1 external-trigger/DMA state: PASS'
    Write-Output "DMA NDTR snapshot A: $dmaNdtrA"
    Write-Output "DMA NDTR snapshot B: $dmaNdtrB"
    Write-Output 'DMA genuine DBM / fixed M0AR / M1AR: PASS'
    Write-Output 'ADC OVR flag: 0'
    Write-Output 'DMA FE/DME/TE flags: 0'

    # -------------------------------------------------------------------------
    # 12. Preserve local build result
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 11. Preserve focused-regression result ==='

    $summary = @(
        'R1 final focused hardware regression',
        '',
        ('Committed HEAD: {0}' -f $expectedHead),
        ('ELF SHA256: {0}' -f $expectedElfHash),
        'Exact committed-state flash/verify: PASS',
        'VCP startup banner: PASS',
        '',
        'R0 regression',
        ('SystemCoreClock: {0}' -f $systemCoreClock),
        ('AIRCR.PRIGROUP: {0}' -f $prigroup),
        ('TIM7 PSC: {0}' -f $tim7Psc),
        ('TIM7 ARR: {0}' -f $tim7Arr),
        ('SysTick LOAD: {0}' -f $systickLoad),
        ('HAL tick delta across snapshots: {0}' -f $uwTickDelta),
        ('FreeRTOS tick delta across snapshots: {0}' -f $xTickDelta),
        'FreeRTOS scheduler: RUNNING',
        'SVC priority: 0',
        'PendSV priority: 15',
        'SysTick priority: 15',
        'TIM7 priority: 0',
        'DMA2 Stream0 priority: 5',
        '',
        'R1 live sanity',
        'R1 task phase: RUNNING',
        'TIM2 PSC: 0',
        'TIM2 ARR: 449',
        ('DMA NDTR snapshot A: {0}' -f $dmaNdtrA),
        ('DMA NDTR snapshot B: {0}' -f $dmaNdtrB),
        'Genuine DBM fixed-address check: PASS',
        'ADC OVR flag: 0',
        'DMA FE/DME/TE flags: 0',
        '',
        'Classification: PASS'
    )

    [System.IO.File]::WriteAllLines(
        $summaryPath,
        $summary,
        $enc
    )

    if (-not (Test-Path -LiteralPath $summaryPath -PathType Leaf)) {
        throw 'Focused-regression summary was not created.'
    }

    # -------------------------------------------------------------------------
    # 13. Final repository guard
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 12. Final repository guard ==='

    $statusAfter = @(
        & $git status --porcelain=v1 --untracked-files=all
    )

    if ($statusAfter.Count -ne 0) {
        $statusAfter
        throw 'Focused regression changed repository source state.'
    }

    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R1 FINAL FOCUSED HARDWARE REGRESSION V2: PASS'
    Write-Output '============================================================'
    Write-Output "Committed HEAD: $expectedHead"
    Write-Output "Exact ELF SHA256: $expectedElfHash"
    Write-Output 'Exact ELF flash/verify: PASS'
    Write-Output 'R1 live acquisition sanity: PASS'
    Write-Output 'R0 180 MHz clock: PASS'
    Write-Output 'R0 PRIGROUP=3: PASS'
    Write-Output 'TIM7 HAL tick: PASS'
    Write-Output 'SysTick FreeRTOS tick: PASS'
    Write-Output 'FreeRTOS scheduler: PASS'
    Write-Output 'R0 VCP startup banner: PASS'
    Write-Output 'IRQ priority contract: PASS'
    Write-Output 'ADC OVR / DMA FE/DME/TE: 0'
    Write-Output 'Working tree: clean'
    Write-Output 'r1-pass: NOT CREATED'
    Write-Output ''
    Write-Output "Local regression result: $summaryPath"
    Write-Output 'No commit or push was performed.'
}
catch {
    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R1 FINAL FOCUSED HARDWARE REGRESSION V2: FAIL'
    Write-Output '============================================================'
    Write-Output $_.Exception.Message
    Write-Output ''
    Write-Output 'Do not proceed to final R1 acceptance documentation yet.'
    exit 1
}
finally {
    if ($null -ne $serialPort) {
        try {
            if ($serialPort.IsOpen) {
                $serialPort.Close()
            }

            $serialPort.Dispose()
        }
        catch {
        }
    }
}
