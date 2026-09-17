$ErrorActionPreference = 'Stop'

$repo = 'E:\Projects\stm32-stream-lab'
$git = 'C:\Program Files\Git\cmd\git.exe'
$expectedHead = 'cfd15c812a8f043f19fa4ee22b3bb48392a80726'

Set-Location -LiteralPath $repo

Write-Output '=== R1 SHORT-RUN BRINGUP HARNESS INTEGRATION ==='

try {
    $head = (& $git rev-parse HEAD).Trim()
    if ($head -ne $expectedHead) {
        throw "Unexpected HEAD: $head"
    }

    $expectedBefore = @(
        'firmware/acquisition/r1_acquisition.c',
        'firmware/acquisition/r1_acquisition.h',
        'firmware/cubemx/CMakeLists.txt',
        'firmware/cubemx/Core/Src/stm32f4xx_it.c'
    ) | Sort-Object

    $stateBefore = @(& $git status --porcelain=v1 --untracked-files=all)
    $actualBefore = @(
        $stateBefore | ForEach-Object {
            if ($_.Length -ge 4) { $_.Substring(3) }
        }
    ) | Sort-Object

    $beforeDiff = Compare-Object -ReferenceObject $expectedBefore -DifferenceObject $actualBefore
    if ($beforeDiff) {
        $beforeDiff | Format-Table
        throw 'Unexpected repository state before harness integration.'
    }

    $scriptDir = $PSScriptRoot
    $incomingHeader = Join-Path $scriptDir 'r1_bringup.h'
    $incomingSource = Join-Path $scriptDir 'r1_bringup.c'

    if (-not (Test-Path -LiteralPath $incomingHeader -PathType Leaf)) {
        throw 'r1_bringup.h is missing next to this script.'
    }

    if (-not (Test-Path -LiteralPath $incomingSource -PathType Leaf)) {
        throw 'r1_bringup.c is missing next to this script.'
    }

    $acqDir = Join-Path $repo 'firmware\acquisition'
    $headerPath = Join-Path $acqDir 'r1_acquisition.h'
    $sourcePath = Join-Path $acqDir 'r1_acquisition.c'
    $bringupHeaderPath = Join-Path $acqDir 'r1_bringup.h'
    $bringupSourcePath = Join-Path $acqDir 'r1_bringup.c'
    $cmakePath = Join-Path $repo 'firmware\cubemx\CMakeLists.txt'
    $mainPath = Join-Path $repo 'firmware\cubemx\Core\Src\main.c'

    if (Test-Path -LiteralPath $bringupHeaderPath) {
        throw 'Repository r1_bringup.h already exists.'
    }

    if (Test-Path -LiteralPath $bringupSourcePath) {
        throw 'Repository r1_bringup.c already exists.'
    }

    Copy-Item -LiteralPath $incomingHeader -Destination $bringupHeaderPath
    Copy-Item -LiteralPath $incomingSource -Destination $bringupSourcePath

    Write-Output 'Bringup harness files: installed'

    $enc = New-Object System.Text.UTF8Encoding($false)

    # Add startup register evidence fields to diagnostics.
    $headerText = [System.IO.File]::ReadAllText($headerPath)
    $headerNeedle = '    uint32_t trace_count;'
    if (-not $headerText.Contains($headerNeedle)) {
        throw 'Could not locate trace_count in r1_acquisition.h.'
    }

    $headerInsert = @'
    uint32_t trace_count;

    uint32_t start_dma_cr;
    uint32_t start_dma_ndtr;
    uint32_t start_dma_m0ar;
    uint32_t start_dma_m1ar;
    uint32_t start_dma_ct;
    uint32_t start_adc_cr2;
    uint32_t start_tim2_cr1;
'@

    $headerText = $headerText.Replace($headerNeedle, $headerInsert.TrimEnd())
    [System.IO.File]::WriteAllText($headerPath, $headerText, $enc)

    $sourceText = [System.IO.File]::ReadAllText($sourcePath)

    $resetNeedle = '    r1_diag.trace_count = 0U;'
    if (-not $sourceText.Contains($resetNeedle)) {
        throw 'Could not locate trace_count reset in r1_acquisition.c.'
    }

    $resetInsert = @'
    r1_diag.trace_count = 0U;

    r1_diag.start_dma_cr = 0U;
    r1_diag.start_dma_ndtr = 0U;
    r1_diag.start_dma_m0ar = 0U;
    r1_diag.start_dma_m1ar = 0U;
    r1_diag.start_dma_ct = 0U;
    r1_diag.start_adc_cr2 = 0U;
    r1_diag.start_tim2_cr1 = 0U;
'@

    $sourceText = $sourceText.Replace($resetNeedle, $resetInsert.TrimEnd())

    $startNeedle = '    r1_state = R1_ACQUISITION_STATE_RUNNING;'
    if (-not $sourceText.Contains($startNeedle)) {
        throw 'Could not locate RUNNING transition in r1_acquisition.c.'
    }

    $startInsert = @'
    r1_diag.start_dma_cr = hdma_adc1.Instance->CR;
    r1_diag.start_dma_ndtr = hdma_adc1.Instance->NDTR;
    r1_diag.start_dma_m0ar = hdma_adc1.Instance->M0AR;
    r1_diag.start_dma_m1ar = hdma_adc1.Instance->M1AR;
    r1_diag.start_dma_ct = R1_ReadActiveTarget();
    r1_diag.start_adc_cr2 = hadc1.Instance->CR2;
    r1_diag.start_tim2_cr1 = htim2.Instance->CR1;

    r1_state = R1_ACQUISITION_STATE_RUNNING;
'@

    $sourceText = $sourceText.Replace($startNeedle, $startInsert.TrimEnd())
    [System.IO.File]::WriteAllText($sourcePath, $sourceText, $enc)

    Write-Output 'Acquisition startup register evidence: added'

    # Add harness source to target.
    $cmakeText = [System.IO.File]::ReadAllText($cmakePath)
    if ($cmakeText.Contains('# === R1 BRINGUP HARNESS BEGIN ===')) {
        throw 'R1 bringup harness CMake section already exists.'
    }

    $targetMatch = [regex]::Match($cmakeText, 'target_sources\(\s*([^\s\)]+)')
    if (-not $targetMatch.Success) {
        throw 'Could not determine firmware target from target_sources().' 
    }

    $target = $targetMatch.Groups[1].Value
    $cmakeBlock = "`r`n# === R1 BRINGUP HARNESS BEGIN ===`r`n" +
        "target_sources($target PRIVATE`r`n" +
        '    "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/r1_bringup.c"' + "`r`n" +
        ")`r`n" +
        "# === R1 BRINGUP HARNESS END ===`r`n"

    $cmakeText = $cmakeText.TrimEnd() + $cmakeBlock
    [System.IO.File]::WriteAllText($cmakePath, $cmakeText, $enc)

    Write-Output 'Bringup harness CMake integration: added'

    # Patch main.c only inside USER CODE areas.
    $mainText = [System.IO.File]::ReadAllText($mainPath)

    if (-not $mainText.Contains('#include "r1_bringup.h"')) {
        $includePattern = '(?s)/\* USER CODE BEGIN Includes \*/\s*'
        $includeReplacement = "/* USER CODE BEGIN Includes */`r`n#include `"r1_bringup.h`"`r`n"
        $updated = [regex]::Replace($mainText, $includePattern, $includeReplacement, 1)
        if ($updated -eq $mainText) {
            throw 'Could not insert r1_bringup.h into main.c USER CODE Includes.'
        }
        $mainText = $updated
    }

    if (-not $mainText.Contains('R1_Bringup_CreateTask();')) {
        $user2Pattern = '/\* USER CODE BEGIN 2 \*/'
        $user2Replacement = "/* USER CODE BEGIN 2 */`r`n  R1_Bringup_CreateTask();"
        $updated = [regex]::Replace($mainText, $user2Pattern, $user2Replacement, 1)
        if ($updated -eq $mainText) {
            throw 'Could not insert R1_Bringup_CreateTask() into main.c.'
        }
        $mainText = $updated
    }

    [System.IO.File]::WriteAllText($mainPath, $mainText, $enc)

    Write-Output 'main.c bringup task creation: added'

    # Verification.
    $h = [System.IO.File]::ReadAllText($headerPath)
    $s = [System.IO.File]::ReadAllText($sourcePath)
    $b = [System.IO.File]::ReadAllText($bringupSourcePath)
    $m = [System.IO.File]::ReadAllText($mainPath)
    $c = [System.IO.File]::ReadAllText($cmakePath)

    foreach ($needle in @(
        'uint32_t start_dma_cr;',
        'uint32_t start_dma_ndtr;',
        'uint32_t start_dma_m0ar;',
        'uint32_t start_dma_m1ar;',
        'uint32_t start_dma_ct;',
        'uint32_t start_adc_cr2;',
        'uint32_t start_tim2_cr1;'
    )) {
        if (-not $h.Contains($needle)) {
            throw "Header evidence field missing: $needle"
        }
    }

    foreach ($needle in @(
        'r1_diag.start_dma_cr = hdma_adc1.Instance->CR;',
        'r1_diag.start_dma_ndtr = hdma_adc1.Instance->NDTR;',
        'r1_diag.start_dma_m0ar = hdma_adc1.Instance->M0AR;',
        'r1_diag.start_dma_m1ar = hdma_adc1.Instance->M1AR;',
        'r1_diag.start_dma_ct = R1_ReadActiveTarget();',
        'r1_diag.start_adc_cr2 = hadc1.Instance->CR2;',
        'r1_diag.start_tim2_cr1 = htim2.Instance->CR1;'
    )) {
        if (-not $s.Contains($needle)) {
            throw "Source evidence snapshot missing: $needle"
        }
    }

    foreach ($needle in @(
        'R1_Acquisition_Init();',
        'R1_Acquisition_Start();',
        'R1_Acquisition_Stop();',
        'R1_Acquisition_GetDiagnostics(&diagnostics);',
        'g_r1_bringup_result.short_run_pass = short_run_pass;'
    )) {
        if (-not $b.Contains($needle)) {
            throw "Bringup harness contract missing: $needle"
        }
    }

    if (-not $m.Contains('#include "r1_bringup.h"')) {
        throw 'main.c bringup include missing.'
    }

    if (-not $m.Contains('R1_Bringup_CreateTask();')) {
        throw 'main.c bringup task creation call missing.'
    }

    if (-not $c.Contains('../acquisition/r1_bringup.c')) {
        throw 'Bringup source missing from CMake.'
    }

    $expectedAfter = @(
        'firmware/acquisition/r1_acquisition.c',
        'firmware/acquisition/r1_acquisition.h',
        'firmware/acquisition/r1_bringup.c',
        'firmware/acquisition/r1_bringup.h',
        'firmware/cubemx/CMakeLists.txt',
        'firmware/cubemx/Core/Src/main.c',
        'firmware/cubemx/Core/Src/stm32f4xx_it.c'
    ) | Sort-Object

    $stateAfter = @(& $git status --porcelain=v1 --untracked-files=all)
    $actualAfter = @(
        $stateAfter | ForEach-Object {
            if ($_.Length -ge 4) { $_.Substring(3) }
        }
    ) | Sort-Object

    $afterDiff = Compare-Object -ReferenceObject $expectedAfter -DifferenceObject $actualAfter
    if ($afterDiff) {
        $afterDiff | Format-Table
        throw 'Unexpected repository state after harness integration.'
    }

    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R1 SHORT-RUN BRINGUP HARNESS INTEGRATION: PASS'
    Write-Output '============================================================'
    Write-Output 'Bringup task: statically allocated'
    Write-Output 'Run duration: 250 ms at configured 200 kS/s'
    Write-Output 'Startup DBM register snapshot: added'
    Write-Output 'Post-stop diagnostics snapshot: added'
    Write-Output 'Raw buffer min/max summary: added'
    Write-Output 'No per-block UART telemetry: preserved'
    Write-Output 'No R2 functionality: added'
    Write-Output ''
    Write-Output 'No build, flash, commit, or push was performed.'
}
catch {
    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R1 SHORT-RUN BRINGUP HARNESS INTEGRATION: FAIL'
    Write-Output '============================================================'
    Write-Output $_.Exception.Message
    Write-Output ''
    Write-Output 'Do not build or flash yet.'
    exit 1
}
