$ErrorActionPreference = 'Stop'

$repo = 'E:\Projects\stm32-stream-lab'
$git = 'C:\Program Files\Git\cmd\git.exe'
$expectedHead = '8475676b7ffd657c2bcb6a56328322a15a400530'

Set-Location -LiteralPath $repo

Write-Output '=== R1 LIFECYCLE REGRESSION HARNESS INTEGRATION ==='

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
    $incomingHeader = Join-Path $scriptDir 'r1_lifecycle_bringup.h'
    $incomingSource = Join-Path $scriptDir 'r1_lifecycle_bringup.c'

    if (-not (Test-Path -LiteralPath $incomingHeader -PathType Leaf)) {
        throw 'r1_lifecycle_bringup.h is missing next to this script.'
    }

    if (-not (Test-Path -LiteralPath $incomingSource -PathType Leaf)) {
        throw 'r1_lifecycle_bringup.c is missing next to this script.'
    }

    $repoHeader = Join-Path $repo 'firmware\acquisition\r1_bringup.h'
    $repoSource = Join-Path $repo 'firmware\acquisition\r1_bringup.c'

    foreach ($path in @($repoHeader, $repoSource)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Existing bringup harness file is missing: $path"
        }
    }

    $oldHeader = [System.IO.File]::ReadAllText($repoHeader)
    $oldSource = [System.IO.File]::ReadAllText($repoSource)

    if (-not $oldHeader.Contains('R1_BringupResult')) {
        throw 'Existing short-run bringup header does not match the expected checkpoint.'
    }

    if (-not $oldSource.Contains('R1_BRINGUP_RUN_MS 250U')) {
        throw 'Existing short-run bringup source does not match the expected checkpoint.'
    }

    Copy-Item -LiteralPath $incomingHeader -Destination $repoHeader -Force
    Copy-Item -LiteralPath $incomingSource -Destination $repoSource -Force

    Write-Output 'Lifecycle harness: installed over short-run harness'

    $headerText = [System.IO.File]::ReadAllText($repoHeader)
    $sourceText = [System.IO.File]::ReadAllText($repoSource)

    foreach ($needle in @(
        '#define R1_LIFECYCLE_CYCLE_COUNT 6U',
        'R1_LifecycleCycleResult',
        'g_r1_lifecycle_result',
        'void R1_Bringup_CreateTask(void);'
    )) {
        if (-not $headerText.Contains($needle)) {
            throw "Lifecycle header verification failed: $needle"
        }
    }

    foreach ($needle in @(
        '806400U',
        '864000U',
        '979200U',
        '1036800U',
        '1094400U',
        '1209600U',
        'R1_Acquisition_Start();',
        'R1_Acquisition_Stop();',
        'R1_Acquisition_CopyTrace(',
        'R1_LIFECYCLE_QUIET_MS 2U',
        'trace[0].completed_target != 0U',
        'trace[1].completed_target != 1U',
        'quiet_diagnostics->tc_count != diagnostics->tc_count',
        'diagnostics->partial_stop_count != 1U',
        'tskIDLE_PRIORITY + 2U'
    )) {
        if (-not $sourceText.Contains($needle)) {
            throw "Lifecycle source verification failed: $needle"
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
    Write-Output 'R1 LIFECYCLE REGRESSION HARNESS INTEGRATION: PASS'
    Write-Output '============================================================'
    Write-Output 'Regression cycles: 6'
    Write-Output 'Start/stop/restart sequence: implemented'
    Write-Output 'Targeted partial-stop timing: implemented'
    Write-Output 'Startup NDTR/CT/M0AR/M1AR checks: implemented'
    Write-Output 'First-completion stale-TC check: implemented'
    Write-Output 'M0/M1 alternation checks: implemented'
    Write-Output 'Post-stop quiet-window check: implemented'
    Write-Output 'OVR / DMA error checks: implemented'
    Write-Output 'DWT cadence checks: implemented'
    Write-Output 'R2 functionality: NOT ADDED'
    Write-Output ''
    Write-Output 'No build, flash, commit, or push was performed.'
}
catch {
    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R1 LIFECYCLE REGRESSION HARNESS INTEGRATION: FAIL'
    Write-Output '============================================================'
    Write-Output $_.Exception.Message
    Write-Output ''
    Write-Output 'Do not build or flash yet.'
    exit 1
}
