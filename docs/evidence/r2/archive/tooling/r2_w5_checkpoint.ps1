[CmdletBinding()]
param(
    [string]$Repo = 'E:\Projects\stm32-stream-lab',
    [string]$W5BuildDir = 'E:\Projects\stm32-stream-lab\build\r2-w5-w5target-c4b79468',
    [string]$NativeBuildDir = 'E:\Projects\stm32-stream-lab\build\r2-w5-native-403092f6',
    [string]$W4RegressionBuildDir = 'E:\Projects\stm32-stream-lab\build\r2-w5-w4regression-c424aaf2',
    [string]$W3RegressionBuildDir = 'E:\Projects\stm32-stream-lab\build\r2-w5-w3regression-7949d275',
    [string]$R1BaselineBuildDir = 'E:\Projects\stm32-stream-lab\build\r2-w5-r1baseline-230b5252'
)

$ErrorActionPreference = 'Stop'

$git = 'C:\Program Files\Git\cmd\git.exe'
$objcopy = 'E:\DevTools\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi\bin\arm-none-eabi-objcopy.exe'

$expectedHead = '7ae9a562ff0eaeeeda4a80c1bea16a6d22d7203c'
$expectedR1Pass = '5ebf62e90b31e262f44013afb594a430061f139a'

$expectedR1ElfHash = '67040A73D2072C569918FA3E3AB9B3F88B52C0901E211665ED99462BF33E6CBA'
$expectedW3ElfHash = '1F9CACD7838F3985C89DA7F7AFEE6428CB70BB9C1939890D41C4FBF0490A77E1'
$expectedW4ElfHash = 'D2018A7B03F65E66E66CCCE1045234DB346AED684BB7B6895D5F2A2097EA4A88'
$expectedW5ElfHash = '301417FD7E57F3B74927286F37359035D8FF932CDEA526775E6FB9F243E4DEAE'

$subject = 'feat: establish R2 controlled capacity drop'

$evidenceDir = Join-Path $Repo 'docs\evidence\r2\w5'

$sourceClassification = Join-Path $W5BuildDir 'hardware-classification.txt'
$sourceTrace = Join-Path $W5BuildDir 'hardware-trace.csv'
$sourceInspection = Join-Path $W5BuildDir 'hardware-inspection.txt'
$w5Elf = Join-Path $W5BuildDir 'cubemx.elf'

$evidenceClassification = Join-Path $evidenceDir 'hardware-classification-01.txt'
$evidenceTrace = Join-Path $evidenceDir 'hardware-trace-01.csv'
$evidenceInspection = Join-Path $evidenceDir 'hardware-inspection-01.txt'
$prehardwareEvidence = Join-Path $evidenceDir 'prehardware-regression-01.txt'
$readme = Join-Path $evidenceDir 'README.md'
$manifest = Join-Path $evidenceDir 'w5-evidence-manifest.sha256'

Set-Location -LiteralPath $Repo

Write-Output '=== R2-W5 HARDWARE CHECKPOINT / EVIDENCE SEAL ==='

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

function Export-Binary {
    param(
        [string]$Elf,
        [string]$Bin
    )

    $oldPreference = $ErrorActionPreference

    try {
        $ErrorActionPreference = 'Continue'
        & $objcopy -O binary $Elf $Bin
        $rc = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $oldPreference
    }

    if ($rc -ne 0 -or
        -not (Test-Path -LiteralPath $Bin -PathType Leaf)) {
        throw "objcopy failed for: $Elf"
    }
}

try {
    # -------------------------------------------------------------------------
    # 1. Repository and gate guard
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
        throw 'main and origin/main are not synchronized before W5 checkpoint.'
    }

    $r1 = (& $git rev-parse 'r1-pass^{commit}').Trim()

    if ($r1 -ne $expectedR1Pass) {
        throw "r1-pass moved unexpectedly: $r1"
    }

    if (@(& $git tag --list 'r2-pass').Count -ne 0) {
        throw 'r2-pass exists unexpectedly.'
    }

    if (@(& $git diff --cached --name-only).Count -ne 0) {
        throw 'Staging area is not empty.'
    }

    $expectedInstalledChanges = @(
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

    $actualInstalledChanges = Get-ChangedPaths

    $scopeDiff = Compare-Object `
        -ReferenceObject $expectedInstalledChanges `
        -DifferenceObject $actualInstalledChanges

    if ($scopeDiff) {
        $scopeDiff | Format-Table
        throw 'Working tree is not the exact hardware-tested W5 source state.'
    }

    Write-Output "HEAD: $head"
    Write-Output "r1-pass -> $r1"
    Write-Output 'r2-pass: NOT CREATED'
    Write-Output 'Hardware-tested W5 source scope: verified'

    # -------------------------------------------------------------------------
    # 2. Hardware-result guard
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 2. W5 hardware-result guard ==='

    foreach ($path in @(
        $sourceClassification,
        $sourceTrace,
        $sourceInspection,
        $w5Elf
    )) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required W5 hardware artifact missing: $path"
        }
    }

    $actualW5ElfHash = (
        Get-FileHash -LiteralPath $w5Elf -Algorithm SHA256
    ).Hash

    if ($actualW5ElfHash -ne $expectedW5ElfHash) {
        throw "W5 hardware-tested ELF SHA256 mismatch: $actualW5ElfHash"
    }

    $classificationText = [System.IO.File]::ReadAllText($sourceClassification)

    foreach ($required in @(
        'Source HEAD: 7ae9a562ff0eaeeeda4a80c1bea16a6d22d7203c',
        'W5 ELF SHA256: 301417FD7E57F3B74927286F37359035D8FF932CDEA526775E6FB9F243E4DEAE',
        'K: 1',
        'P: 3',
        'Input TC events: 64',
        'Admitted blocks: 10',
        'Capacity drops: 54',
        'Processed blocks: 10',
        'Released blocks: 10',
        'Recovered admissions after drop streaks: 9',
        'Maximum consecutive drop streak: 6',
        'Illegal FreeBufferQueue sends: 0',
        'ReadyQueue send failures: 0',
        'Processing notification failures: 0',
        'Token ledger errors: 0',
        'DMA error flags seen: 0',
        'ADC OVR seen: 0',
        'Final FREE buffer: B0',
        'Final ownership: 1 FREE + 2 DMA_OWNED',
        'Ownership violations: 0',
        'DMA-slot violations: 0',
        'Completed admitted samples validated: 2560',
        'Sample errors: 0',
        'Canary errors: 0',
        'Every DROP left M0AR/M1AR and mapping epoch unchanged: PASS',
        'No dropped block entered Processing: PASS',
        'Admissions resumed after controlled drop streaks: PASS',
        'Classification: PASS'
    )) {
        if (-not $classificationText.Contains($required)) {
            throw "W5 hardware classification is missing: $required"
        }
    }

    $traceLines = [System.IO.File]::ReadAllLines($sourceTrace)

    if ($traceLines.Count -ne 65) {
        throw "Expected one W5 trace header plus 64 records; found $($traceLines.Count)."
    }

    $admitLines = 0
    $dropLines = 0

    for ($i = 1; $i -lt $traceLines.Count; $i++) {
        $columns = $traceLines[$i].Split(',')

        if ($columns.Count -ne 14) {
            throw "Trace record $i has an unexpected column count."
        }

        if ([int]$columns[0] -ne $i) {
            throw "Trace sequence mismatch at record $i."
        }

        $decision = [int]$columns[1]

        if ($decision -eq 1) {
            ++$admitLines
        }
        elseif ($decision -eq 2) {
            ++$dropLines

            if ([int]$columns[5] -ne 255 -or
                [int]$columns[11] -ne 0 -or
                [int]$columns[13] -ne 0) {
                throw "DROP trace record $i violates the no-replacement/no-processing contract."
            }
        }
        else {
            throw "Trace record $i has invalid decision code: $decision"
        }

        if ([int]$columns[7] -gt 57600) {
            throw "Trace record $i exceeds the 0.25 TB decision budget."
        }
    }

    if ($admitLines -ne 10 -or $dropLines -ne 54) {
        throw "Trace decision totals are unexpected: admits=$admitLines drops=$dropLines"
    }

    Write-Output "Hardware-tested W5 ELF SHA256: $actualW5ElfHash"
    Write-Output 'Hardware classification: PASS'
    Write-Output 'Trace records: 64 / 64 verified'
    Write-Output 'Trace decisions: 10 ADMIT / 54 DROP'

    # -------------------------------------------------------------------------
    # 3. Pre-hardware build matrix guard
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 3. Pre-hardware build matrix guard ==='

    $buildChecks = @(
        @{ Name = 'Native'; Path = $NativeBuildDir; Required = 'W1/W2/W3/W4/W5 native tests: 84 / 84 PASS' },
        @{ Name = 'W5Target'; Path = $W5BuildDir; Required = 'Profile: W5Target' },
        @{ Name = 'W4Regression'; Path = $W4RegressionBuildDir; Required = 'Profile: W4Regression' },
        @{ Name = 'W3Regression'; Path = $W3RegressionBuildDir; Required = 'Profile: W3Regression' },
        @{ Name = 'R1Baseline'; Path = $R1BaselineBuildDir; Required = 'Profile: R1Baseline' }
    )

    foreach ($entry in $buildChecks) {
        $summaryPath = Join-Path $entry.Path 'verification-summary.txt'

        if (-not (Test-Path -LiteralPath $summaryPath -PathType Leaf)) {
            throw "Verification summary missing for $($entry.Name): $summaryPath"
        }

        $summaryText = [System.IO.File]::ReadAllText($summaryPath)

        if (-not $summaryText.Contains('Build: PASS') -or
            -not $summaryText.Contains('Hardware: NOT RUN') -or
            -not $summaryText.Contains($entry.Required)) {
            throw "Verification summary is invalid for $($entry.Name)."
        }

        Write-Output ("PASS  {0}" -f $entry.Name)
    }

    # -------------------------------------------------------------------------
    # 4. Lower-layer programmed-byte regression guard
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 4. Lower-layer programmed-byte regression guard ==='

    foreach ($path in @(
        $objcopy,
        (Join-Path $Repo 'build\r1-soak-01\cubemx.elf'),
        (Join-Path $Repo 'build\r2-w3-w3target-23c9886a\cubemx.elf'),
        (Join-Path $Repo 'build\r2-w4-w4target-e2e7b31f\cubemx.elf'),
        (Join-Path $R1BaselineBuildDir 'cubemx.elf'),
        (Join-Path $W3RegressionBuildDir 'cubemx.elf'),
        (Join-Path $W4RegressionBuildDir 'cubemx.elf')
    )) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Lower-layer regression artifact missing: $path"
        }
    }

    $layers = @(
        @{
            Name = 'R1'
            Original = Join-Path $Repo 'build\r1-soak-01\cubemx.elf'
            Current = Join-Path $R1BaselineBuildDir 'cubemx.elf'
            ElfHash = $expectedR1ElfHash
        },
        @{
            Name = 'W3'
            Original = Join-Path $Repo 'build\r2-w3-w3target-23c9886a\cubemx.elf'
            Current = Join-Path $W3RegressionBuildDir 'cubemx.elf'
            ElfHash = $expectedW3ElfHash
        },
        @{
            Name = 'W4'
            Original = Join-Path $Repo 'build\r2-w4-w4target-e2e7b31f\cubemx.elf'
            Current = Join-Path $W4RegressionBuildDir 'cubemx.elf'
            ElfHash = $expectedW4ElfHash
        }
    )

    $temp = Join-Path $env:TEMP (
        'r2-w5-evidence-regression-' + [guid]::NewGuid().ToString('N')
    )

    New-Item -ItemType Directory -Path $temp | Out-Null

    $regressionLines = New-Object System.Collections.Generic.List[string]
    $regressionLines.Add('R2-W5 pre-hardware verification and lower-layer regression')
    $regressionLines.Add('')
    $regressionLines.Add('Native W1/W2/W3/W4/W5 suite: 84 / 84 PASS')
    $regressionLines.Add('W5Target ARM/CubeF4 build: PASS')
    $regressionLines.Add('W4Regression ARM/CubeF4 build: PASS')
    $regressionLines.Add('W3Regression ARM/CubeF4 build: PASS')
    $regressionLines.Add('R1Baseline ARM/CubeF4 build: PASS')
    $regressionLines.Add('')

    foreach ($layer in $layers) {
        $originalElfHash = (
            Get-FileHash -LiteralPath $layer.Original -Algorithm SHA256
        ).Hash

        if ($originalElfHash -ne $layer.ElfHash) {
            throw "$($layer.Name) original hardware-tested ELF identity mismatch."
        }

        $originalBin = Join-Path $temp ($layer.Name + '-original.bin')
        $currentBin = Join-Path $temp ($layer.Name + '-current.bin')

        Export-Binary $layer.Original $originalBin
        Export-Binary $layer.Current $currentBin

        $originalSize = (Get-Item -LiteralPath $originalBin).Length
        $currentSize = (Get-Item -LiteralPath $currentBin).Length

        $originalBinHash = (
            Get-FileHash -LiteralPath $originalBin -Algorithm SHA256
        ).Hash

        $currentBinHash = (
            Get-FileHash -LiteralPath $currentBin -Algorithm SHA256
        ).Hash

        if ($originalSize -ne $currentSize -or
            $originalBinHash -ne $currentBinHash) {
            throw "$($layer.Name) programmed firmware bytes differ."
        }

        Write-Output ("{0} programmed bytes: BYTE-IDENTICAL" -f $layer.Name)

        $regressionLines.Add(
            ('{0} original hardware-tested ELF SHA256: {1}' -f
                $layer.Name, $layer.ElfHash)
        )
        $regressionLines.Add(
            ('{0} programmed-image SHA256: {1}' -f
                $layer.Name, $originalBinHash)
        )
        $regressionLines.Add(
            ('{0} programmed bytes under W5-disabled regression profile: BYTE-IDENTICAL' -f
                $layer.Name)
        )
        $regressionLines.Add('')
    }

    Remove-Item -LiteralPath $temp -Recurse -Force

    $regressionLines.Add('Classification: PASS')

    # -------------------------------------------------------------------------
    # 5. Preserve authoritative W5 evidence
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 5. Preserve authoritative W5 evidence ==='

    if (-not (Test-Path -LiteralPath $evidenceDir -PathType Container)) {
        New-Item -ItemType Directory -Path $evidenceDir -Force | Out-Null
    }

    foreach ($path in @(
        $evidenceClassification,
        $evidenceTrace,
        $evidenceInspection,
        $prehardwareEvidence,
        $readme,
        $manifest
    )) {
        if (Test-Path -LiteralPath $path) {
            throw "Refusing to overwrite existing W5 evidence path: $path"
        }
    }

    Copy-Item -LiteralPath $sourceClassification -Destination $evidenceClassification
    Copy-Item -LiteralPath $sourceTrace -Destination $evidenceTrace
    Copy-Item -LiteralPath $sourceInspection -Destination $evidenceInspection

    $enc = New-Object System.Text.UTF8Encoding($false)

    [System.IO.File]::WriteAllLines(
        $prehardwareEvidence,
        $regressionLines,
        $enc
    )

    function Extract-Metric {
        param(
            [string]$Name
        )

        $match = [regex]::Match(
            $classificationText,
            '(?m)^' + [regex]::Escape($Name) + ': ([0-9]+)\r?$'
        )

        if (-not $match.Success) {
            throw "Unable to extract W5 metric: $Name"
        }

        return $match.Groups[1].Value
    }

    $maxDecision = Extract-Metric 'Maximum nominal-to-decision cycles'
    $maxExit = Extract-Metric 'Maximum nominal-to-IRQ-exit cycles'
    $maxFinal = Extract-Metric 'Maximum protected final-window cycles'
    $maxReady = Extract-Metric 'Maximum ReadyQueue depth'
    $recoveries = Extract-Metric 'Recovered admissions after drop streaks'
    $maxStreak = Extract-Metric 'Maximum consecutive drop streak'

    $readmeLines = @(
        '# R2-W5 Controlled Capacity-Drop Evidence',
        '',
        '## Status',
        '',
        '- Work package: R2-W5',
        '- Classification: PASS',
        '- R2 overall: IN PROGRESS',
        '- `r2-pass` tag: NOT CREATED',
        '',
        '## Scope',
        '',
        'This work package deliberately exhausts the FreeBufferQueue and validates the controlled drop path.',
        'A DROP does not rebind M0AR/M1AR, does not advance the DMA-slot mapping epoch, does not publish a READY descriptor, and does not enter Processing.',
        '',
        '## Tested configuration',
        '',
        '- Platform: STM32 NUCLEO-F446RE',
        '- Sample rate: 200 kS/s',
        '- Block size: 256 samples',
        '- K: 1',
        '- Active physical buffers P: 3',
        '- FreeBufferQueue capacity: 1',
        '- ReadyQueue capacity: 1',
        '- Input TC events: 64',
        '- Processing hold: approximately five block periods',
        '- Input condition: PA0 / ADC1_IN0 connected to GND',
        '',
        '## Real-hardware result',
        '',
        '- Input TC events: 64',
        '- Admitted blocks: 10',
        '- Controlled capacity drops: 54',
        '- Accounting: admitted + drops = 64',
        '- Processed blocks: 10',
        '- Released blocks: 10',
        ('- Recovered admissions after drop streaks: {0}' -f $recoveries),
        ('- Maximum consecutive drop streak: {0}' -f $maxStreak),
        '- Every DROP changed M0AR/M1AR: NEVER',
        '- Every DROP changed software mapping epoch: NEVER',
        '- Dropped block entered Processing: NEVER',
        '- FreeBufferQueue exhaustion observed: PASS',
        '- Illegal FreeBufferQueue sends: 0',
        '- ReadyQueue send failures: 0',
        '- Processing notification failures: 0',
        '- Token ledger errors: 0',
        '- DMA TE/DME/FE: 0',
        '- ADC OVR: 0',
        '- BufferPool violations: 0',
        '- DMA-slot mapping violations: 0',
        '- Validated admitted samples: 2560',
        '- Sample errors: 0',
        '- Canary errors: 0',
        ('- Maximum nominal-to-decision latency: {0} cycles' -f $maxDecision),
        ('- Maximum nominal-to-IRQ-exit marker: {0} cycles' -f $maxExit),
        ('- Maximum protected window: {0} cycles' -f $maxFinal),
        ('- Maximum ReadyQueue depth: {0}' -f $maxReady),
        '- Minimum FreeBufferQueue depth after take: 0',
        '- Final ownership: 1 FREE + 2 DMA_OWNED',
        '- Post-stop quiet window: PASS',
        '',
        'The mandatory nominal completion-to-decision budget is 0.25 TB = 57600 cycles at N=256 and 200 kS/s.',
        '',
        '## Lower-layer regression',
        '',
        '- W1/W2/W3/W4/W5 native suite: 84 / 84 PASS',
        '- W5Target ARM/CubeF4 build: PASS',
        '- W4 programmed firmware bytes with W5 disabled: BYTE-IDENTICAL to the W4 hardware-tested image',
        '- W3 programmed firmware bytes with W4/W5 disabled: BYTE-IDENTICAL to the W3 hardware-tested image',
        '- R1 programmed firmware bytes with W3/W4/W5 disabled: BYTE-IDENTICAL to the R1 hardware-tested image',
        '',
        '## Build identity',
        '',
        ('- Source baseline before W5 checkpoint: `{0}`' -f $expectedHead),
        ('- Hardware-tested W5 ELF SHA256: `{0}`' -f $expectedW5ElfHash),
        '- Build profile: `STREAM_LAB_R2_W3=OFF`, `STREAM_LAB_R2_W4=OFF`, `STREAM_LAB_R2_W5=ON`',
        '',
        '## Evidence files',
        '',
        '- `PLAN.md`',
        '- `README.md`',
        '- `prehardware-regression-01.txt`',
        '- `hardware-classification-01.txt`',
        '- `hardware-trace-01.csv`',
        '- `hardware-inspection-01.txt`',
        '- `w5-evidence-manifest.sha256`',
        '',
        '## Boundary',
        '',
        'R2-W5 closes the directed K=1 FreeBufferQueue exhaustion and controlled capacity-drop path.',
        'R2-W6 must execute the mandatory K = 1 / 2 / 4 / 8 matrix before R2 can become a PASS candidate.'
    )

    [System.IO.File]::WriteAllLines($readme, $readmeLines, $enc)

    $manifestInputs = @(
        'PLAN.md',
        'README.md',
        'prehardware-regression-01.txt',
        'hardware-classification-01.txt',
        'hardware-trace-01.csv',
        'hardware-inspection-01.txt'
    )

    $manifestLines = New-Object System.Collections.Generic.List[string]

    foreach ($name in $manifestInputs) {
        $path = Join-Path $evidenceDir $name

        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Manifest input missing: $name"
        }

        $hash = (
            Get-FileHash -LiteralPath $path -Algorithm SHA256
        ).Hash.ToLowerInvariant()

        $manifestLines.Add(('{0}  {1}' -f $hash, $name))
    }

    [System.IO.File]::WriteAllLines($manifest, $manifestLines, $enc)

    Write-Output 'W5 hardware result: preserved'
    Write-Output 'W5 trace CSV: preserved'
    Write-Output 'W5 raw GDB inspection: preserved'
    Write-Output 'Pre-hardware regression evidence: preserved'
    Write-Output 'README and SHA256 manifest: created'

    # -------------------------------------------------------------------------
    # 6. Final scope / language / whitespace audit
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 6. Final W5 change-scope audit ==='

    $expectedFinalChanges = @(
        'docs/evidence/r2/w5/PLAN.md',
        'docs/evidence/r2/w5/README.md',
        'docs/evidence/r2/w5/hardware-classification-01.txt',
        'docs/evidence/r2/w5/hardware-inspection-01.txt',
        'docs/evidence/r2/w5/hardware-trace-01.csv',
        'docs/evidence/r2/w5/prehardware-regression-01.txt',
        'docs/evidence/r2/w5/w5-evidence-manifest.sha256',
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

    $actualFinalChanges = Get-ChangedPaths

    $finalScopeDiff = Compare-Object `
        -ReferenceObject $expectedFinalChanges `
        -DifferenceObject $actualFinalChanges

    if ($finalScopeDiff) {
        $finalScopeDiff | Format-Table
        throw 'Final W5 checkpoint change scope is unexpected.'
    }

    foreach ($relative in $expectedFinalChanges) {
        $full = Join-Path $Repo $relative
        $text = [System.IO.File]::ReadAllText($full)

        if ([regex]::IsMatch(
            $text,
            '[\u3400-\u4dbf\u4e00-\u9fff]'
        )) {
            throw "CJK text found in repository artifact: $relative"
        }
    }

    & $git diff --check

    if ($LASTEXITCODE -ne 0) {
        throw 'git diff --check failed.'
    }

    Write-Output 'Exact 17-file change scope: verified'
    Write-Output 'Repository English-only policy: PASS'
    Write-Output 'git diff --check: PASS'

    # -------------------------------------------------------------------------
    # 7. Stage exact W5 checkpoint
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 7. Stage exact W5 checkpoint ==='

    foreach ($relative in $expectedFinalChanges) {
        & $git add -- $relative

        if ($LASTEXITCODE -ne 0) {
            throw "git add failed for: $relative"
        }
    }

    $staged = @(& $git diff --cached --name-only) | Sort-Object

    $stageDiff = Compare-Object `
        -ReferenceObject $expectedFinalChanges `
        -DifferenceObject $staged

    if ($stageDiff) {
        $stageDiff | Format-Table
        throw 'Staged W5 file set is unexpected.'
    }

    & $git diff --cached --check

    if ($LASTEXITCODE -ne 0) {
        throw 'git diff --cached --check failed.'
    }

    Write-Output 'Staged W5 file set: exact'
    Write-Output 'git diff --cached --check: PASS'

    # -------------------------------------------------------------------------
    # 8. Commit / push
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 8. Commit and push ==='

    & $git commit -m $subject

    if ($LASTEXITCODE -ne 0) {
        throw 'W5 checkpoint commit failed.'
    }

    $commit = (& $git rev-parse HEAD).Trim()
    $actualSubject = (& $git log -1 --pretty=%s).Trim()

    if ($actualSubject -ne $subject) {
        throw "Unexpected W5 commit subject: $actualSubject"
    }

    $commitPaths = @(
        & $git diff-tree --no-commit-id --name-only -r HEAD
    ) | Sort-Object

    $commitDiff = Compare-Object `
        -ReferenceObject $expectedFinalChanges `
        -DifferenceObject $commitPaths

    if ($commitDiff) {
        $commitDiff | Format-Table
        throw 'W5 checkpoint commit contains unexpected paths.'
    }

    & $git push origin main

    if ($LASTEXITCODE -ne 0) {
        throw 'W5 checkpoint push failed.'
    }

    & $git fetch origin main --quiet

    if ($LASTEXITCODE -ne 0) {
        throw 'Fetch after W5 push failed.'
    }

    $localHead = (& $git rev-parse HEAD).Trim()
    $remoteHead = (& $git rev-parse origin/main).Trim()

    if ($localHead -ne $remoteHead) {
        throw 'main and origin/main are not synchronized after W5 push.'
    }

    # -------------------------------------------------------------------------
    # 9. Final invariants
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 9. Final invariants ==='

    $r1After = (& $git rev-parse 'r1-pass^{commit}').Trim()

    if ($r1After -ne $expectedR1Pass) {
        throw "r1-pass moved unexpectedly: $r1After"
    }

    if (@(& $git tag --list 'r2-pass').Count -ne 0) {
        throw 'r2-pass exists unexpectedly.'
    }

    $finalStatus = @(& $git status --porcelain=v1 --untracked-files=all)

    if ($finalStatus.Count -ne 0) {
        $finalStatus
        throw 'Working tree is not clean after W5 checkpoint.'
    }

    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R2-W5 HARDWARE CHECKPOINT / EVIDENCE SEAL: PASS'
    Write-Output '============================================================'
    Write-Output "Checkpoint commit: $commit"
    Write-Output "Subject: $subject"
    Write-Output "Hardware-tested W5 ELF SHA256: $expectedW5ElfHash"
    Write-Output 'Directed TC events: 64'
    Write-Output 'Admitted blocks: 10'
    Write-Output 'Controlled capacity drops: 54'
    Write-Output 'Accounting admitted + drops: PASS'
    Write-Output 'Recovered admissions after drops: 9'
    Write-Output 'Max consecutive drop streak: 6'
    Write-Output 'DROP changed M0AR/M1AR: NEVER'
    Write-Output 'DROP changed mapping epoch: NEVER'
    Write-Output 'Dropped block entered Processing: NEVER'
    Write-Output 'DMA TE/DME/FE: 0'
    Write-Output 'ADC OVR: 0'
    Write-Output 'Ownership violations: 0'
    Write-Output 'DMA-slot violations: 0'
    Write-Output 'Token ledger errors: 0'
    Write-Output 'Validated admitted samples: 2560'
    Write-Output 'Canary errors: 0'
    Write-Output 'R1 programmed-byte regression: BYTE-IDENTICAL'
    Write-Output 'W3 programmed-byte regression: BYTE-IDENTICAL'
    Write-Output 'W4 programmed-byte regression: BYTE-IDENTICAL'
    Write-Output 'W5 evidence package: preserved'
    Write-Output 'main / origin/main: synchronized'
    Write-Output 'Working tree: clean'
    Write-Output "r1-pass -> $r1After"
    Write-Output 'r2-pass: NOT CREATED'
    Write-Output ''
    Write-Output 'R2-W5: CLOSED / KNOWN-GOOD'
    Write-Output 'R2-W6: NOT STARTED'
    Write-Output 'R2 overall: IN PROGRESS'
}
catch {
    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R2-W5 HARDWARE CHECKPOINT / EVIDENCE SEAL: FAIL'
    Write-Output '============================================================'
    Write-Output $_.Exception.Message
    Write-Output ''
    Write-Output 'Do not begin R2-W6.'
    exit 1
}
