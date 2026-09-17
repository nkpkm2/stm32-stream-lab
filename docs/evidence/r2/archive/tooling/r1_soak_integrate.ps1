$ErrorActionPreference = 'Stop'

$repo = 'E:\Projects\stm32-stream-lab'
$git = 'C:\Program Files\Git\cmd\git.exe'
$expectedHead = 'ec2cc4244837d54d423ca9e240b50fe0a66509f1'

Set-Location -LiteralPath $repo

Write-Output '=== R1 FORMAL SOAK HARNESS INTEGRATION ==='

try {
    $head = (& $git rev-parse HEAD).Trim()

    if ($head -ne $expectedHead) {
        throw "Unexpected HEAD: $head"
    }

    $state = @(& $git status --porcelain=v1 --untracked-files=all)

    if ($state.Count -ne 0) {
        $state
        throw 'Working tree is not clean.'
    }

    $staged = @(& $git diff --cached --name-only)

    if ($staged.Count -ne 0) {
        throw 'Staging area is not empty.'
    }

    $scriptDir = $PSScriptRoot
    $incomingHeader = Join-Path $scriptDir 'r1_soak_bringup.h'
    $incomingSource = Join-Path $scriptDir 'r1_soak_bringup.c'

    if (-not (Test-Path -LiteralPath $incomingHeader -PathType Leaf)) {
        throw 'r1_soak_bringup.h is missing next to this script.'
    }

    if (-not (Test-Path -LiteralPath $incomingSource -PathType Leaf)) {
        throw 'r1_soak_bringup.c is missing next to this script.'
    }

    $repoHeader = Join-Path $repo 'firmware\acquisition\r1_bringup.h'
    $repoSource = Join-Path $repo 'firmware\acquisition\r1_bringup.c'

    foreach ($path in @($repoHeader, $repoSource)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Existing lifecycle harness file is missing: $path"
        }
    }

    $oldHeader = [System.IO.File]::ReadAllText($repoHeader)
    $oldSource = [System.IO.File]::ReadAllText($repoSource)

    if (-not $oldHeader.Contains('R1_LIFECYCLE_CYCLE_COUNT 6U')) {
        throw 'Existing lifecycle header does not match the expected checkpoint.'
    }

    if (-not $oldSource.Contains('R1_LIFECYCLE_QUIET_MS 2U')) {
        throw 'Existing lifecycle source does not match the expected checkpoint.'
    }

    Copy-Item -LiteralPath $incomingHeader -Destination $repoHeader -Force
    Copy-Item -LiteralPath $incomingSource -Destination $repoSource -Force

    Write-Output 'Formal soak harness: installed over lifecycle harness'

    $headerText = [System.IO.File]::ReadAllText($repoHeader)
    $sourceText = [System.IO.File]::ReadAllText($repoSource)

    foreach ($needle in @(
        '#define R1_SOAK_DURATION_MS 600000U',
        'R1_SoakResult',
        'g_r1_soak_result',
        'observed_block_rate_millihz',
        'tc_count_error',
        'hal_tick_delta',
        'void R1_Bringup_CreateTask(void);'
    )) {
        if (-not $headerText.Contains($needle)) {
            throw "Soak header verification failed: $needle"
        }
    }

    foreach ($needle in @(
        'vTaskDelay(pdMS_TO_TICKS(R1_SOAK_DURATION_MS));',
        'R1_Acquisition_Start();',
        'R1_Acquisition_Stop();',
        'R1_SOAK_EXPECTED_BLOCK_RATE_MILLIHZ 781250U',
        'R1_SOAK_TC_COUNT_TOLERANCE 2U',
        'diagnostics->ct_mismatch_count != 0U',
        'diagnostics->suspected_event_loss_count != 0U',
        'diagnostics->adc_ovr_count != 0U',
        'quiet_diagnostics->tc_count != diagnostics->tc_count',
        'SystemCoreClock != 180000000U',
        'SCB_AIRCR_PRIGROUP_Msk',
        'R1_Soak_GetRawRange(',
        'tskIDLE_PRIORITY + 2U'
    )) {
        if (-not $sourceText.Contains($needle)) {
            throw "Soak source verification failed: $needle"
        }
    }

    $after = @(& $git status --porcelain=v1 --untracked-files=all)

    $actual = @(
        $after |
        ForEach-Object {
            if ($_.Length -ge 4) {
                $_.Substring(3)
            }
        }
    ) | Sort-Object

    $expected = @(
        'firmware/acquisition/r1_bringup.c',
        'firmware/acquisition/r1_bringup.h'
    ) | Sort-Object

    $scopeDiff = Compare-Object `
        -ReferenceObject $expected `
        -DifferenceObject $actual

    if ($scopeDiff) {
        $scopeDiff | Format-Table
        throw 'Unexpected repository change scope.'
    }

    $freeRtosState = @(
        & $git status --porcelain=v1 --untracked-files=all -- `
            firmware/cubemx/Middlewares/Third_Party/FreeRTOS-Kernel
    )

    if ($freeRtosState.Count -ne 0) {
        throw 'FreeRTOS subtree changed unexpectedly.'
    }

    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R1 FORMAL SOAK HARNESS INTEGRATION: PASS'
    Write-Output '============================================================'
    Write-Output 'Target configuration: 200 kS/s, N=256'
    Write-Output 'Formal soak duration: 600000 ms'
    Write-Output 'Expected block rate: 781.250 blocks/s'
    Write-Output 'Expected completions near 468750 for exactly 600 s'
    Write-Output 'No per-block UART telemetry: preserved'
    Write-Output 'Bounded trace only: preserved'
    Write-Output 'CT / alternation / event-loss checks: implemented'
    Write-Output 'ADC OVR / DMA error checks: implemented'
    Write-Output 'Observed cadence calculation: implemented'
    Write-Output 'Raw grounded-buffer range snapshot: implemented'
    Write-Output 'R0 clock / PRIGROUP sentinels: implemented'
    Write-Output 'R2 functionality: NOT ADDED'
    Write-Output ''
    Write-Output 'No build, flash, commit, or push was performed.'
}
catch {
    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R1 FORMAL SOAK HARNESS INTEGRATION: FAIL'
    Write-Output '============================================================'
    Write-Output $_.Exception.Message
    Write-Output ''
    Write-Output 'Do not build or flash yet.'
    exit 1
}
