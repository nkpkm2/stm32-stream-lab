[CmdletBinding()]
param(
    [string]$Repo = 'E:\Projects\stm32-stream-lab',
    [string]$W4BuildDir = 'E:\Projects\stm32-stream-lab\build\r2-w4-w4target-e2e7b31f',
    [string]$NativeBuildDir = 'E:\Projects\stm32-stream-lab\build\r2-w4-native-bcc86e30',
    [string]$W3RegressionBuildDir = 'E:\Projects\stm32-stream-lab\build\r2-w4-w3regression-faab4bfb',
    [string]$R1BaselineBuildDir = 'E:\Projects\stm32-stream-lab\build\r2-w4-r1baseline-a6ea31b0'
)

$ErrorActionPreference = 'Stop'

$git = 'C:\Program Files\Git\cmd\git.exe'
$objcopy = 'E:\DevTools\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi\bin\arm-none-eabi-objcopy.exe'

$expectedHead = 'f5d0f086dae94d2f244f8366a65aa32c511fd5be'
$expectedR1Pass = '5ebf62e90b31e262f44013afb594a430061f139a'
$expectedR1ElfHash = '67040A73D2072C569918FA3E3AB9B3F88B52C0901E211665ED99462BF33E6CBA'
$expectedW3ElfHash = '1F9CACD7838F3985C89DA7F7AFEE6428CB70BB9C1939890D41C4FBF0490A77E1'

$subject = 'feat: establish R2 ownership round trip'

$evidenceDir = Join-Path $Repo 'docs\evidence\r2\w4'
$sourceClassification = Join-Path $W4BuildDir 'hardware-classification.txt'
$sourceTrace = Join-Path $W4BuildDir 'hardware-trace.csv'
$sourceInspection = Join-Path $W4BuildDir 'hardware-inspection.txt'
$w4Elf = Join-Path $W4BuildDir 'cubemx.elf'

$evidenceClassification = Join-Path $evidenceDir 'hardware-classification-01.txt'
$evidenceTrace = Join-Path $evidenceDir 'hardware-trace-01.csv'
$evidenceInspection = Join-Path $evidenceDir 'hardware-inspection-01.txt'
$prehardwareEvidence = Join-Path $evidenceDir 'prehardware-regression-01.txt'
$readme = Join-Path $evidenceDir 'README.md'
$manifest = Join-Path $evidenceDir 'w4-evidence-manifest.sha256'

Set-Location -LiteralPath $Repo

Write-Output '=== R2-W4 HARDWARE CHECKPOINT / EVIDENCE SEAL ==='

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
    # 1. Repository / gate guard
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
        throw 'main and origin/main are not synchronized before W4 checkpoint.'
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
        'docs/evidence/r2/w4/PLAN.md',
        'firmware/acquisition/r2_w4_guard.h',
        'firmware/acquisition/r2_w4_roundtrip.c',
        'firmware/acquisition/r2_w4_roundtrip.h',
        'firmware/cubemx/CMakeLists.txt',
        'firmware/cubemx/Core/Inc/FreeRTOSConfig.h',
        'firmware/cubemx/Core/Src/main.c',
        'firmware/cubemx/Core/Src/stm32f4xx_it.c',
        'tests/native/CMakeLists.txt',
        'tests/native/test_r2_w4_model.c',
        'tools/r2/verify_w4.ps1'
    ) | Sort-Object

    $actualInstalledChanges = Get-ChangedPaths

    $scopeDiff = Compare-Object `
        -ReferenceObject $expectedInstalledChanges `
        -DifferenceObject $actualInstalledChanges

    if ($scopeDiff) {
        $scopeDiff | Format-Table
        throw 'Working tree is not the exact hardware-tested W4 source state.'
    }

    Write-Output "HEAD: $head"
    Write-Output "r1-pass -> $r1"
    Write-Output 'r2-pass: NOT CREATED'
    Write-Output 'Hardware-tested W4 source scope: verified'

    # -------------------------------------------------------------------------
    # 2. W4 hardware-result guard
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 2. W4 hardware-result guard ==='

    foreach ($path in @(
        $sourceClassification,
        $sourceTrace,
        $sourceInspection,
        $w4Elf
    )) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required W4 hardware artifact missing: $path"
        }
    }

    $classificationText = [System.IO.File]::ReadAllText($sourceClassification)

    foreach ($required in @(
        'Source HEAD: f5d0f086dae94d2f244f8366a65aa32c511fd5be',
        'K: 4',
        'P: 6',
        'Admitted blocks: 64',
        'Processed blocks: 64',
        'Released blocks: 64',
        'Illegal FreeBufferQueue sends: 0',
        'FreeBufferQueue empty events: 0',
        'ReadyQueue send failures: 0',
        'Token ledger errors: 0',
        'DMA error flags seen: 0',
        'ADC OVR seen: 0',
        'Final FREE buffers: 4',
        'Final DMA_OWNED buffers: 2',
        'Ownership violations: 0',
        'DMA-slot violations: 0',
        'Completed samples validated: 16384',
        'Sample errors: 0',
        'Canary errors: 0',
        'Classification: PASS'
    )) {
        if (-not $classificationText.Contains($required)) {
            throw "W4 hardware classification is missing: $required"
        }
    }

    $elfHashMatch = [regex]::Match(
        $classificationText,
        '(?m)^W4 ELF SHA256: ([0-9A-F]{64})\r?$'
    )

    if (-not $elfHashMatch.Success) {
        throw 'W4 hardware classification does not contain a valid ELF SHA256.'
    }

    $w4ElfHash = $elfHashMatch.Groups[1].Value
    $actualW4ElfHash = (
        Get-FileHash -LiteralPath $w4Elf -Algorithm SHA256
    ).Hash

    if ($actualW4ElfHash -ne $w4ElfHash) {
        throw "W4 hardware-tested ELF SHA256 mismatch: $actualW4ElfHash"
    }

    $traceLines = [System.IO.File]::ReadAllLines($sourceTrace)

    if ($traceLines.Count -ne 65) {
        throw "Expected one W4 trace header plus 64 records; found $($traceLines.Count)."
    }

    Write-Output "Hardware-tested W4 ELF SHA256: $w4ElfHash"
    Write-Output 'Hardware classification: PASS'
    Write-Output 'Hardware trace records: 64 / 64 present'

    # -------------------------------------------------------------------------
    # 3. Pre-hardware build-matrix and lower-layer regression guard
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 3. Pre-hardware build/regression guard ==='

    $buildChecks = @(
        @{ Name = 'Native'; Path = $NativeBuildDir; Required = 'W1/W2/W3/W4 native tests: 72 / 72 PASS' },
        @{ Name = 'W4Target'; Path = $W4BuildDir; Required = 'Profile: W4Target' },
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

    foreach ($path in @(
        $objcopy,
        (Join-Path $Repo 'build\r1-soak-01\cubemx.elf'),
        (Join-Path $Repo 'build\r2-w3-w3target-23c9886a\cubemx.elf'),
        (Join-Path $R1BaselineBuildDir 'cubemx.elf'),
        (Join-Path $W3RegressionBuildDir 'cubemx.elf')
    )) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Lower-layer regression artifact missing: $path"
        }
    }

    $originalR1Elf = Join-Path $Repo 'build\r1-soak-01\cubemx.elf'
    $originalW3Elf = Join-Path $Repo 'build\r2-w3-w3target-23c9886a\cubemx.elf'
    $currentR1Elf = Join-Path $R1BaselineBuildDir 'cubemx.elf'
    $currentW3Elf = Join-Path $W3RegressionBuildDir 'cubemx.elf'

    if ((Get-FileHash -LiteralPath $originalR1Elf -Algorithm SHA256).Hash -ne
        $expectedR1ElfHash) {
        throw 'Original R1 hardware-tested ELF identity mismatch.'
    }

    if ((Get-FileHash -LiteralPath $originalW3Elf -Algorithm SHA256).Hash -ne
        $expectedW3ElfHash) {
        throw 'Original W3 hardware-tested ELF identity mismatch.'
    }

    $temp = Join-Path $env:TEMP (
        'r2-w4-evidence-regression-' + [guid]::NewGuid().ToString('N')
    )

    New-Item -ItemType Directory -Path $temp | Out-Null

    $r1OriginalBin = Join-Path $temp 'r1-original.bin'
    $r1CurrentBin = Join-Path $temp 'r1-current.bin'
    $w3OriginalBin = Join-Path $temp 'w3-original.bin'
    $w3CurrentBin = Join-Path $temp 'w3-current.bin'

    Export-Binary $originalR1Elf $r1OriginalBin
    Export-Binary $currentR1Elf $r1CurrentBin
    Export-Binary $originalW3Elf $w3OriginalBin
    Export-Binary $currentW3Elf $w3CurrentBin

    $r1OriginalBinHash = (
        Get-FileHash -LiteralPath $r1OriginalBin -Algorithm SHA256
    ).Hash
    $r1CurrentBinHash = (
        Get-FileHash -LiteralPath $r1CurrentBin -Algorithm SHA256
    ).Hash
    $w3OriginalBinHash = (
        Get-FileHash -LiteralPath $w3OriginalBin -Algorithm SHA256
    ).Hash
    $w3CurrentBinHash = (
        Get-FileHash -LiteralPath $w3CurrentBin -Algorithm SHA256
    ).Hash

    if ($r1OriginalBinHash -ne $r1CurrentBinHash) {
        throw 'W4-era R1 programmed bytes differ from the R1 hardware-tested baseline.'
    }

    if ($w3OriginalBinHash -ne $w3CurrentBinHash) {
        throw 'W4-era W3 programmed bytes differ from the W3 hardware-tested baseline.'
    }

    Remove-Item -LiteralPath $temp -Recurse -Force

    Write-Output 'R1 programmed-byte regression: BYTE-IDENTICAL'
    Write-Output 'W3 programmed-byte regression: BYTE-IDENTICAL'

    # -------------------------------------------------------------------------
    # 4. Preserve authoritative W4 evidence
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 4. Preserve authoritative W4 evidence ==='

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
            throw "Refusing to overwrite existing W4 evidence path: $path"
        }
    }

    Copy-Item -LiteralPath $sourceClassification -Destination $evidenceClassification
    Copy-Item -LiteralPath $sourceTrace -Destination $evidenceTrace
    Copy-Item -LiteralPath $sourceInspection -Destination $evidenceInspection

    $enc = New-Object System.Text.UTF8Encoding($false)

    $prehardwareLines = @(
        'R2-W4 pre-hardware verification and lower-layer regression',
        '',
        'Native W1/W2/W3/W4 suite: 72 / 72 PASS',
        'W4Target ARM/CubeF4 build: PASS',
        'W3Regression ARM/CubeF4 build: PASS',
        'R1Baseline ARM/CubeF4 build: PASS',
        ('Original R1 hardware-tested ELF SHA256: {0}' -f $expectedR1ElfHash),
        ('Original W3 hardware-tested ELF SHA256: {0}' -f $expectedW3ElfHash),
        ('R1 programmed-image SHA256: {0}' -f $r1OriginalBinHash),
        ('W3 programmed-image SHA256: {0}' -f $w3OriginalBinHash),
        'R1 programmed bytes under W4=OFF: BYTE-IDENTICAL',
        'W3 programmed bytes under W4=OFF: BYTE-IDENTICAL',
        '',
        'Classification: PASS'
    )

    [System.IO.File]::WriteAllLines(
        $prehardwareEvidence,
        $prehardwareLines,
        $enc
    )

    $maxCommitMatch = [regex]::Match(
        $classificationText,
        '(?m)^Maximum nominal-to-commit cycles: ([0-9]+)\r?$'
    )
    $maxExitMatch = [regex]::Match(
        $classificationText,
        '(?m)^Maximum nominal-to-IRQ-exit cycles: ([0-9]+)\r?$'
    )
    $maxFinalMatch = [regex]::Match(
        $classificationText,
        '(?m)^Maximum protected final-window cycles: ([0-9]+)\r?$'
    )
    $maxReadyMatch = [regex]::Match(
        $classificationText,
        '(?m)^Maximum ReadyQueue depth: ([0-9]+)\r?$'
    )
    $minFreeMatch = [regex]::Match(
        $classificationText,
        '(?m)^Minimum FreeBufferQueue depth after take: ([0-9]+)\r?$'
    )

    foreach ($match in @(
        $maxCommitMatch,
        $maxExitMatch,
        $maxFinalMatch,
        $maxReadyMatch,
        $minFreeMatch
    )) {
        if (-not $match.Success) {
            throw 'Unable to extract W4 hardware metric for README.'
        }
    }

    $readmeLines = @(
        '# R2-W4 READY / PROCESSING / FREE Round-Trip Evidence',
        '',
        '## Status',
        '',
        '- Work package: R2-W4',
        '- Classification: PASS',
        '- R2 overall: IN PROGRESS',
        '- `r2-pass` tag: NOT CREATED',
        '',
        '## Scope',
        '',
        'This work package validates the minimal real-hardware ownership round trip:',
        '',
        '`DMA_OWNED -> READY -> PROCESSING -> FREE -> DMA_OWNED`',
        '',
        'It intentionally does not force FreeBufferQueue exhaustion or validate the controlled capacity-drop path. That remains R2-W5.',
        '',
        '## Tested configuration',
        '',
        '- Platform: STM32 NUCLEO-F446RE',
        '- Sample rate: 200 kS/s',
        '- Block size: 256 samples',
        '- K: 4',
        '- Active physical buffers P: 6',
        '- FreeBufferQueue capacity: 4',
        '- ReadyQueue capacity: 4',
        '- Test events: 64',
        '- Processing operation: descriptor validation plus 256-sample raw scan and immediate controlled release',
        '- Input condition: PA0 / ADC1_IN0 connected to GND',
        '',
        '## Real-hardware result',
        '',
        '- DMA -> READY admissions: 64 / 64 PASS',
        '- READY -> PROCESSING claims: 64 / 64 PASS',
        '- PROCESSING -> FREE controlled release commits: 64 / 64 PASS',
        '- Reuse from FREE back into DMA ownership: PASS',
        '- Initialization FreeBufferQueue commit hooks: 4',
        '- Completion release commit hooks: 64',
        '- Illegal FreeBufferQueue sends: 0',
        '- FreeBufferQueue empty events: 0',
        '- ReadyQueue send failures: 0',
        '- Processing notification failures: 0',
        '- Token ledger errors: 0',
        '- DMA TE/DME/FE: 0',
        '- ADC OVR: 0',
        '- BufferPool violations: 0',
        '- DMA-slot mapping violations: 0',
        '- Completed samples validated: 16384',
        '- Sample errors: 0',
        '- Canary errors: 0',
        ('- Maximum nominal-to-commit latency: {0} cycles' -f $maxCommitMatch.Groups[1].Value),
        ('- Maximum nominal-to-IRQ-exit marker: {0} cycles' -f $maxExitMatch.Groups[1].Value),
        ('- Maximum protected write window: {0} cycles' -f $maxFinalMatch.Groups[1].Value),
        ('- Maximum ReadyQueue depth: {0}' -f $maxReadyMatch.Groups[1].Value),
        ('- Minimum FreeBufferQueue depth after take: {0}' -f $minFreeMatch.Groups[1].Value),
        '- Final ReadyQueue depth: 0',
        '- Final FreeBufferQueue depth: 4',
        '- Final ownership: 4 FREE + 2 DMA_OWNED',
        '- Post-stop quiet window: PASS',
        '',
        'The mandatory nominal completion-to-commit budget is 0.25 TB = 57600 cycles at N=256 and 200 kS/s.',
        '',
        '## Lower-layer regression',
        '',
        '- W1/W2/W3/W4 native suite: 72 / 72 PASS',
        '- W4Target ARM/CubeF4 build: PASS',
        '- W3 programmed firmware bytes with W4 disabled: BYTE-IDENTICAL to the W3 hardware-tested image',
        '- R1 programmed firmware bytes with W3/W4 disabled: BYTE-IDENTICAL to the R1 hardware-tested image',
        '',
        '## Build identity',
        '',
        ('- Source baseline before W4 checkpoint: `{0}`' -f $expectedHead),
        ('- Hardware-tested W4 ELF SHA256: `{0}`' -f $w4ElfHash),
        '- Build profile: `STREAM_LAB_R2_W3=OFF`, `STREAM_LAB_R2_W4=ON`',
        '',
        '## Evidence files',
        '',
        '- `PLAN.md`',
        '- `README.md`',
        '- `prehardware-regression-01.txt`',
        '- `hardware-classification-01.txt`',
        '- `hardware-trace-01.csv`',
        '- `hardware-inspection-01.txt`',
        '- `w4-evidence-manifest.sha256`',
        '',
        '## Boundary',
        '',
        'R2-W4 closes the normal ownership round-trip path only.',
        'R2-W5 must deliberately exhaust the FreeBufferQueue and prove controlled capacity drops without duplicate ownership, stale publication, active-slot writes, or DMA corruption.'
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

    Write-Output 'W4 hardware result: preserved'
    Write-Output 'W4 trace CSV: preserved'
    Write-Output 'W4 raw GDB inspection: preserved'
    Write-Output 'Pre-hardware regression evidence: preserved'
    Write-Output 'README and SHA256 manifest: created'

    # -------------------------------------------------------------------------
    # 5. Final change-scope / language / whitespace audit
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 5. Final W4 change-scope audit ==='

    $expectedFinalChanges = @(
        'docs/evidence/r2/w4/PLAN.md',
        'docs/evidence/r2/w4/README.md',
        'docs/evidence/r2/w4/hardware-classification-01.txt',
        'docs/evidence/r2/w4/hardware-inspection-01.txt',
        'docs/evidence/r2/w4/hardware-trace-01.csv',
        'docs/evidence/r2/w4/prehardware-regression-01.txt',
        'docs/evidence/r2/w4/w4-evidence-manifest.sha256',
        'firmware/acquisition/r2_w4_guard.h',
        'firmware/acquisition/r2_w4_roundtrip.c',
        'firmware/acquisition/r2_w4_roundtrip.h',
        'firmware/cubemx/CMakeLists.txt',
        'firmware/cubemx/Core/Inc/FreeRTOSConfig.h',
        'firmware/cubemx/Core/Src/main.c',
        'firmware/cubemx/Core/Src/stm32f4xx_it.c',
        'tests/native/CMakeLists.txt',
        'tests/native/test_r2_w4_model.c',
        'tools/r2/verify_w4.ps1'
    ) | Sort-Object

    $actualFinalChanges = Get-ChangedPaths

    $finalScopeDiff = Compare-Object `
        -ReferenceObject $expectedFinalChanges `
        -DifferenceObject $actualFinalChanges

    if ($finalScopeDiff) {
        $finalScopeDiff | Format-Table
        throw 'Final W4 checkpoint change scope is unexpected.'
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
    # 6. Stage exact checkpoint
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 6. Stage exact W4 checkpoint ==='

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
        throw 'Staged W4 file set is unexpected.'
    }

    & $git diff --cached --check

    if ($LASTEXITCODE -ne 0) {
        throw 'git diff --cached --check failed.'
    }

    Write-Output 'Staged W4 file set: exact'
    Write-Output 'git diff --cached --check: PASS'

    # -------------------------------------------------------------------------
    # 7. Commit / push
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 7. Commit and push ==='

    & $git commit -m $subject

    if ($LASTEXITCODE -ne 0) {
        throw 'W4 checkpoint commit failed.'
    }

    $commit = (& $git rev-parse HEAD).Trim()
    $actualSubject = (& $git log -1 --pretty=%s).Trim()

    if ($actualSubject -ne $subject) {
        throw "Unexpected W4 commit subject: $actualSubject"
    }

    $commitPaths = @(
        & $git diff-tree --no-commit-id --name-only -r HEAD
    ) | Sort-Object

    $commitDiff = Compare-Object `
        -ReferenceObject $expectedFinalChanges `
        -DifferenceObject $commitPaths

    if ($commitDiff) {
        $commitDiff | Format-Table
        throw 'W4 checkpoint commit contains unexpected paths.'
    }

    & $git push origin main

    if ($LASTEXITCODE -ne 0) {
        throw 'W4 checkpoint push failed.'
    }

    & $git fetch origin main --quiet

    if ($LASTEXITCODE -ne 0) {
        throw 'Fetch after W4 push failed.'
    }

    $localHead = (& $git rev-parse HEAD).Trim()
    $remoteHead = (& $git rev-parse origin/main).Trim()

    if ($localHead -ne $remoteHead) {
        throw 'main and origin/main are not synchronized after W4 push.'
    }

    # -------------------------------------------------------------------------
    # 8. Final invariants
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 8. Final invariants ==='

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
        throw 'Working tree is not clean after W4 checkpoint.'
    }

    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R2-W4 HARDWARE CHECKPOINT / EVIDENCE SEAL: PASS'
    Write-Output '============================================================'
    Write-Output "Checkpoint commit: $commit"
    Write-Output "Subject: $subject"
    Write-Output "Hardware-tested W4 ELF SHA256: $w4ElfHash"
    Write-Output 'Normal ownership round trips: 64 / 64 PASS'
    Write-Output 'Illegal FreeBufferQueue sends: 0'
    Write-Output 'FreeBufferQueue empty events: 0'
    Write-Output 'ReadyQueue send failures: 0'
    Write-Output 'Token ledger errors: 0'
    Write-Output 'DMA TE/DME/FE: 0'
    Write-Output 'ADC OVR: 0'
    Write-Output 'Ownership violations: 0'
    Write-Output 'DMA-slot violations: 0'
    Write-Output 'Completed samples validated: 16384'
    Write-Output 'Canary errors: 0'
    Write-Output 'R1 programmed-byte regression: BYTE-IDENTICAL'
    Write-Output 'W3 programmed-byte regression: BYTE-IDENTICAL'
    Write-Output 'W4 evidence package: preserved'
    Write-Output 'main / origin/main: synchronized'
    Write-Output 'Working tree: clean'
    Write-Output "r1-pass -> $r1After"
    Write-Output 'r2-pass: NOT CREATED'
    Write-Output ''
    Write-Output 'R2-W4: CLOSED / KNOWN-GOOD'
    Write-Output 'R2-W5: NOT STARTED'
    Write-Output 'R2 overall: IN PROGRESS'
}
catch {
    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R2-W4 HARDWARE CHECKPOINT / EVIDENCE SEAL: FAIL'
    Write-Output '============================================================'
    Write-Output $_.Exception.Message
    Write-Output ''
    Write-Output 'Do not begin R2-W5.'
    exit 1
}
