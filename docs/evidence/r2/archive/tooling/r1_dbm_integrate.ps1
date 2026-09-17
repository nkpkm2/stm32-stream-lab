$ErrorActionPreference = 'Stop'

$repo = 'E:\Projects\stm32-stream-lab'
$git = 'C:\Program Files\Git\cmd\git.exe'
$expectedHead = 'cfd15c812a8f043f19fa4ee22b3bb48392a80726'
$r0Firmware = '9ade715d6f3035cd60512bf2ec4dd1c226436af8'

Set-Location -LiteralPath $repo

Write-Output '=== R1 FIXED-BUFFER DBM DRIVER INTEGRATION ==='

try {
    Write-Output ''
    Write-Output '=== 1. Preconditions ==='

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

    $r0Tag = (& $git rev-parse 'r0-pass^{commit}').Trim()
    if ($r0Tag -ne $r0Firmware) {
        throw "r0-pass target is wrong: $r0Tag"
    }

    Write-Output 'Repository state: clean'
    Write-Output 'R1 generated baseline checkpoint: verified'
    Write-Output 'r0-pass target: preserved'

    $sourceFrom = Join-Path $PSScriptRoot 'r1_acquisition.c'
    $headerFrom = Join-Path $PSScriptRoot 'r1_acquisition.h'

    if (-not (Test-Path -LiteralPath $sourceFrom -PathType Leaf)) {
        throw "Missing sibling file: $sourceFrom"
    }

    if (-not (Test-Path -LiteralPath $headerFrom -PathType Leaf)) {
        throw "Missing sibling file: $headerFrom"
    }

    $acqDir = Join-Path $repo 'firmware\acquisition'
    $sourceTo = Join-Path $acqDir 'r1_acquisition.c'
    $headerTo = Join-Path $acqDir 'r1_acquisition.h'
    $cmakePath = Join-Path $repo 'firmware\cubemx\CMakeLists.txt'
    $irqPath = Join-Path $repo 'firmware\cubemx\Core\Src\stm32f4xx_it.c'

    if (Test-Path -LiteralPath $sourceTo) {
        throw 'Target already exists: firmware/acquisition/r1_acquisition.c'
    }

    if (Test-Path -LiteralPath $headerTo) {
        throw 'Target already exists: firmware/acquisition/r1_acquisition.h'
    }

    Write-Output ''
    Write-Output '=== 2. Install acquisition driver ==='

    New-Item -ItemType Directory -Path $acqDir -Force | Out-Null
    Copy-Item -LiteralPath $sourceFrom -Destination $sourceTo
    Copy-Item -LiteralPath $headerFrom -Destination $headerTo

    Write-Output 'r1_acquisition.c: installed'
    Write-Output 'r1_acquisition.h: installed'

    Write-Output ''
    Write-Output '=== 3. Integrate driver into top-level CMake ==='

    $cmakeText = [System.IO.File]::ReadAllText($cmakePath)

    if ($cmakeText.Contains('# === R1 ACQUISITION DRIVER BEGIN ===')) {
        throw 'R1 acquisition CMake section already exists.'
    }

    $targetMatch = [regex]::Match(
        $cmakeText,
        'target_sources\(\s*([^\s\)]+)')

    if (-not $targetMatch.Success) {
        throw 'Could not determine target from target_sources().' 
    }

    $target = $targetMatch.Groups[1].Value

    $cmakeBlock = @"

# === R1 ACQUISITION DRIVER BEGIN ===
target_sources($target PRIVATE
    "`$`{CMAKE_CURRENT_SOURCE_DIR`}/../acquisition/r1_acquisition.c"
)

target_include_directories($target PRIVATE
    "`$`{CMAKE_CURRENT_SOURCE_DIR`}/../acquisition"
)
# === R1 ACQUISITION DRIVER END ===
"@

    $enc = New-Object System.Text.UTF8Encoding($false)
    $cmakeText = $cmakeText.TrimEnd() + "`r`n" + $cmakeBlock + "`r`n"
    [System.IO.File]::WriteAllText($cmakePath, $cmakeText, $enc)

    Write-Output "CMake target: $target"
    Write-Output 'R1 acquisition driver source/include integration: added'

    Write-Output ''
    Write-Output '=== 4. Add DMA IRQ snapshot hook ==='

    $irqText = [System.IO.File]::ReadAllText($irqPath)

    if ($irqText.Contains('#include "r1_acquisition.h"')) {
        throw 'r1_acquisition.h is already included in stm32f4xx_it.c.'
    }

    $includeOld = "/* USER CODE BEGIN Includes */`r`n`r`n/* USER CODE END Includes */"
    if (-not $irqText.Contains($includeOld)) {
        $includeOld = "/* USER CODE BEGIN Includes */`n`n/* USER CODE END Includes */"
    }

    if (-not $irqText.Contains($includeOld)) {
        throw 'Could not locate IRQ include USER CODE block.'
    }

    $includeNew = "/* USER CODE BEGIN Includes */`r`n#include `"r1_acquisition.h`"`r`n/* USER CODE END Includes */"
    $irqText = $irqText.Replace($includeOld, $includeNew)

    $hookPattern = '(?s)(/\* USER CODE BEGIN DMA2_Stream0_IRQn 0 \*/)(.*?)(/\* USER CODE END DMA2_Stream0_IRQn 0 \*/\s*HAL_DMA_IRQHandler\(&hdma_adc1\);)'
    $hookMatches = [regex]::Matches($irqText, $hookPattern)

    if ($hookMatches.Count -ne 1) {
        throw "Expected one DMA2 Stream0 USER CODE hook block, found: $($hookMatches.Count)"
    }

    $hookReplacement = '$1' + "`r`n  R1_Acquisition_DmaIrqEnter(DMA2->LISR);`r`n  " + '$3'
    $irqText = [regex]::Replace($irqText, $hookPattern, $hookReplacement)

    [System.IO.File]::WriteAllText($irqPath, $irqText, $enc)

    Write-Output 'DMA2 Stream0 pre-dispatch snapshot hook: added'

    Write-Output ''
    Write-Output '=== 5. Static verification ==='

    $sourceText = [System.IO.File]::ReadAllText($sourceTo)
    $headerText = [System.IO.File]::ReadAllText($headerTo)
    $cmakeVerify = [System.IO.File]::ReadAllText($cmakePath)
    $irqVerify = [System.IO.File]::ReadAllText($irqPath)

    foreach ($required in @(
        'R1_ACQUISITION_BLOCK_SAMPLES 256U',
        'R1_ACQUISITION_TRACE_CAPACITY 128U',
        'R1_ACQUISITION_EXPECTED_BLOCK_CYCLES 230400U',
        'R1_Acquisition_Start',
        'R1_Acquisition_Stop',
        'R1_Acquisition_DmaIrqEnter'
    )) {
        if (-not $headerText.Contains($required)) {
            throw "Header verification failed: $required"
        }
    }

    foreach ($required in @(
        'HAL_DMAEx_MultiBufferStart_IT',
        'hdma_adc1.XferCpltCallback = R1_DmaM0Complete;',
        'hdma_adc1.XferM1CpltCallback = R1_DmaM1Complete;',
        'hdma_adc1.XferHalfCpltCallback = NULL;',
        'hdma_adc1.XferM1HalfCpltCallback = NULL;',
        '__HAL_DMA_DISABLE_IT(&hdma_adc1, DMA_IT_HT);',
        'HAL_ADC_Start(&hadc1)',
        'SET_BIT(hadc1.Instance->CR2, ADC_CR2_DMA);',
        'HAL_TIM_Base_Start(&htim2)',
        'HAL_TIM_Base_Stop(&htim2)',
        'HAL_ADC_Stop_DMA(&hadc1)'
    )) {
        if (-not $sourceText.Contains($required)) {
            throw "Source verification failed: $required"
        }
    }

    if (-not $cmakeVerify.Contains('# === R1 ACQUISITION DRIVER BEGIN ===')) {
        throw 'CMake integration section is missing.'
    }

    if (-not $irqVerify.Contains('#include "r1_acquisition.h"')) {
        throw 'IRQ header include is missing.'
    }

    if (-not $irqVerify.Contains('R1_Acquisition_DmaIrqEnter(DMA2->LISR);')) {
        throw 'DMA IRQ snapshot hook is missing.'
    }

    Write-Output 'Driver API: verified'
    Write-Output 'DBM startup path: verified'
    Write-Output 'Half-transfer callbacks disabled: verified'
    Write-Output 'Stop path: verified'
    Write-Output 'IRQ snapshot hook: verified'

    Write-Output ''
    Write-Output '=== 6. Repository change scope ==='

    $status = @(& $git status --porcelain=v1 --untracked-files=all)
    $status

    $actual = @(
        $status |
        ForEach-Object {
            if ($_.Length -ge 4) {
                $_.Substring(3)
            }
        }
    ) | Sort-Object

    $expected = @(
        'firmware/acquisition/r1_acquisition.c',
        'firmware/acquisition/r1_acquisition.h',
        'firmware/cubemx/CMakeLists.txt',
        'firmware/cubemx/Core/Src/stm32f4xx_it.c'
    ) | Sort-Object

    $scopeDiff = Compare-Object -ReferenceObject $expected -DifferenceObject $actual
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
    Write-Output 'R1 FIXED-BUFFER DBM DRIVER INTEGRATION: PASS'
    Write-Output '============================================================'
    Write-Output 'Two fixed uint16_t[256] buffers: created'
    Write-Output 'DMA M0/M1 multibuffer startup path: implemented'
    Write-Output 'Half-transfer callbacks: disabled'
    Write-Output 'DMA TC / CT diagnostics: implemented'
    Write-Output 'DMA error diagnostics: implemented'
    Write-Output 'ADC OVR observation: implemented'
    Write-Output 'Bounded 128-event trace: implemented'
    Write-Output 'Partial-stop bookkeeping: implemented'
    Write-Output 'TIM2 start-last ordering: implemented'
    Write-Output 'DMA IRQ pre-dispatch snapshot hook: implemented'
    Write-Output ''
    Write-Output 'No build, flash, commit, or push was performed.'
}
catch {
    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R1 FIXED-BUFFER DBM DRIVER INTEGRATION: FAIL'
    Write-Output '============================================================'
    Write-Output $_.Exception.Message
    Write-Output ''
    Write-Output 'Do not build or flash yet.'
    exit 1
}
