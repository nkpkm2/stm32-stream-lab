[CmdletBinding()]
param(
    [string]$Repo = 'E:\Projects\stm32-stream-lab',
    [string]$BuildDir = 'E:\Projects\stm32-stream-lab\build\r2-w3-w3target-23c9886a'
)

$ErrorActionPreference = 'Stop'

$git = 'C:\Program Files\Git\cmd\git.exe'

$expectedHead = 'a966cd3e78b4f190ca731daea8388463d39f7486'
$expectedR1Pass = '5ebf62e90b31e262f44013afb594a430061f139a'
$expectedElfHash = '1F9CACD7838F3985C89DA7F7AFEE6428CB70BB9C1939890D41C4FBF0490A77E1'

$subject = 'feat: establish R2 bounded dynamic DMA rebinding'

$evidenceDir = Join-Path $Repo 'docs\evidence\r2\w3'
$sourceClassification = Join-Path $BuildDir 'hardware-classification.txt'
$sourceTrace = Join-Path $BuildDir 'hardware-trace.csv'
$sourceInspection = Join-Path $BuildDir 'hardware-inspection.txt'
$elf = Join-Path $BuildDir 'cubemx.elf'

$evidenceClassification = Join-Path $evidenceDir 'hardware-classification-01.txt'
$evidenceTrace = Join-Path $evidenceDir 'hardware-trace-01.csv'
$evidenceInspection = Join-Path $evidenceDir 'hardware-inspection-01.txt'
$readme = Join-Path $evidenceDir 'README.md'
$manifest = Join-Path $evidenceDir 'w3-evidence-manifest.sha256'

Set-Location -LiteralPath $Repo

Write-Output '=== R2-W3 HARDWARE CHECKPOINT / EVIDENCE SEAL ==='

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
        throw 'main and origin/main are not synchronized before checkpoint.'
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

    $actualInstalledChanges = Get-ChangedPaths
    $scopeDiff = Compare-Object `
        -ReferenceObject $expectedInstalledChanges `
        -DifferenceObject $actualInstalledChanges

    if ($scopeDiff) {
        $scopeDiff | Format-Table
        throw 'Working tree is not the exact hardware-tested W3 source state.'
    }

    Write-Output "HEAD: $head"
    Write-Output "r1-pass -> $r1"
    Write-Output 'r2-pass: NOT CREATED'
    Write-Output 'Hardware-tested W3 source scope: verified'

    # -------------------------------------------------------------------------
    # 2. Hardware-result guard
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 2. Hardware-result guard ==='

    foreach ($path in @(
        $sourceClassification,
        $sourceTrace,
        $sourceInspection,
        $elf
    )) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required W3 hardware artifact missing: $path"
        }
    }

    $elfHash = (
        Get-FileHash -LiteralPath $elf -Algorithm SHA256
    ).Hash

    if ($elfHash -ne $expectedElfHash) {
        throw "W3 hardware-tested ELF SHA256 mismatch: $elfHash"
    }

    $classificationText = [System.IO.File]::ReadAllText($sourceClassification)

    foreach ($required in @(
        'Source HEAD: a966cd3e78b4f190ca731daea8388463d39f7486',
        'W3 ELF SHA256: 1F9CACD7838F3985C89DA7F7AFEE6428CB70BB9C1939890D41C4FBF0490A77E1',
        'K: 8',
        'P: 10',
        'Rebind events: 8 / 8',
        'DMA error flags seen: 0',
        'ADC OVR seen: 0',
        'Final M0 binding: B8',
        'Final M1 binding: B9',
        'Final READY buffers: 8',
        'Final DMA_OWNED buffers: 2',
        'Ownership violations: 0',
        'DMA-slot violations: 0',
        'Completed samples validated: 2048',
        'Sample errors: 0',
        'Canary errors: 0',
        'Classification: PASS'
    )) {
        if (-not $classificationText.Contains($required)) {
            throw "W3 classification is missing: $required"
        }
    }

    $traceLines = [System.IO.File]::ReadAllLines($sourceTrace)
    if ($traceLines.Count -ne 9) {
        throw "Expected trace header plus eight records; found $($traceLines.Count) lines."
    }

    $expectedHeader =
        'sequence,ct,completed_slot,completed_id,replacement_id,ndtr_prewrite,nominal_to_commit_cycles,nominal_to_irq_exit_cycles,final_window_cycles,free_after,ready_after,mapping_epoch_after'

    if ($traceLines[0] -ne $expectedHeader) {
        throw 'W3 trace CSV header is unexpected.'
    }

    for ($i = 1; $i -le 8; $i++) {
        $columns = $traceLines[$i].Split(',')

        if ($columns.Count -ne 12) {
            throw "Trace record $i has an unexpected column count."
        }

        if ([int]$columns[0] -ne $i) {
            throw "Trace sequence mismatch at record $i."
        }

        $expectedCt = $i % 2
        $expectedCompletedSlot = $expectedCt -bxor 1

        if ([int]$columns[1] -ne $expectedCt -or
            [int]$columns[2] -ne $expectedCompletedSlot -or
            [int]$columns[3] -ne ($i - 1) -or
            [int]$columns[4] -ne ($i + 1) -or
            [int]$columns[9] -ne (8 - $i) -or
            [int]$columns[10] -ne $i -or
            [int]$columns[11] -ne ($i + 1)) {
            throw "Trace record $i does not match the expected bounded rotation."
        }

        if ([int]$columns[6] -gt 57600) {
            throw "Trace record $i exceeds the 0.25 TB nominal-to-commit budget."
        }
    }

    Write-Output "Hardware-tested ELF SHA256: $elfHash"
    Write-Output 'Hardware classification: PASS'
    Write-Output 'Trace records: 8 / 8 verified'

    # -------------------------------------------------------------------------
    # 3. Preserve authoritative W3 evidence
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 3. Preserve authoritative W3 evidence ==='

    if (-not (Test-Path -LiteralPath $evidenceDir -PathType Container)) {
        New-Item -ItemType Directory -Path $evidenceDir -Force | Out-Null
    }

    foreach ($path in @(
        $evidenceClassification,
        $evidenceTrace,
        $evidenceInspection,
        $readme,
        $manifest
    )) {
        if (Test-Path -LiteralPath $path) {
            throw "Refusing to overwrite existing W3 evidence path: $path"
        }
    }

    Copy-Item -LiteralPath $sourceClassification -Destination $evidenceClassification
    Copy-Item -LiteralPath $sourceTrace -Destination $evidenceTrace
    Copy-Item -LiteralPath $sourceInspection -Destination $evidenceInspection

    $enc = New-Object System.Text.UTF8Encoding($false)

    $readmeLines = @(
        '# R2-W3 Bounded Dynamic DMA DBM Rebinding Evidence',
        '',
        '## Status',
        '',
        '- Work package: R2-W3',
        '- Classification: PASS',
        '- R2 overall: IN PROGRESS',
        '- `r2-pass` tag: NOT CREATED',
        '',
        '## Scope',
        '',
        'This work package validates bounded real-hardware rebinding of the inactive DMA double-buffer target.',
        'It does not yet implement the READY / PROCESSING / FREE consumer round trip or the continuous capacity-drop path.',
        '',
        '## Tested configuration',
        '',
        '- Platform: STM32 NUCLEO-F446RE',
        '- Sample rate: 200 kS/s',
        '- Block size: 256 samples',
        '- K: 8',
        '- Active physical buffers P: 10',
        '- Initial mapping: M0 = B0, M1 = B1',
        '- Input condition: PA0 / ADC1_IN0 connected to GND',
        '',
        '## Hardware result',
        '',
        '- Dynamic inactive-MxAR rebinds: 8 / 8 PASS',
        '- Physical rotation: B0/B1 -> B2..B9',
        '- Active-slot address changed by software: NEVER',
        '- CT stable across every protected write: PASS',
        '- DMA TE/DME/FE: 0',
        '- ADC OVR: 0',
        '- BufferPool violations: 0',
        '- DMA-slot mapping violations: 0',
        '- Completed samples checked: 2048',
        '- Sample errors: 0',
        '- Canary errors: 0',
        '- Maximum nominal-to-commit latency: 5206 cycles',
        '- Maximum nominal-to-IRQ-exit marker: 9252 cycles',
        '- Maximum protected write window: 1340 cycles',
        '- Post-stop quiet window: PASS',
        '- Final mapping: M0 = B8, M1 = B9',
        '',
        'The R2 acceptance budget for nominal completion to commit is 0.25 TB = 57600 cycles at N=256 and 200 kS/s.',
        'The observed maximum was 5206 cycles.',
        '',
        '## Build identity',
        '',
        '- Source baseline before W3 checkpoint: `a966cd3e78b4f190ca731daea8388463d39f7486`',
        '- Hardware-tested W3 ELF SHA256: `1F9CACD7838F3985C89DA7F7AFEE6428CB70BB9C1939890D41C4FBF0490A77E1`',
        '- Build profile: `STREAM_LAB_R2_W3=ON`',
        '',
        '## Evidence files',
        '',
        '- `PLAN.md`',
        '- `hardware-classification-01.txt`',
        '- `hardware-trace-01.csv`',
        '- `hardware-inspection-01.txt`',
        '- `w3-evidence-manifest.sha256`',
        '',
        '## Boundary',
        '',
        'R2-W3 establishes that the inactive hardware DMA slot can be safely rebound to distinct physical buffers under a bounded real-hardware experiment.',
        'R2-W4 must add the minimal READY -> PROCESSING -> FREE ownership round trip.',
        'R2-W5 must separately validate capacity exhaustion and the controlled drop path.'
    )

    [System.IO.File]::WriteAllLines($readme, $readmeLines, $enc)

    $manifestInputs = @(
        'PLAN.md',
        'README.md',
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

    Write-Output 'Classification evidence: preserved'
    Write-Output 'Trace CSV: preserved'
    Write-Output 'Raw GDB inspection: preserved'
    Write-Output 'README: created'
    Write-Output 'Evidence SHA256 manifest: created'

    # -------------------------------------------------------------------------
    # 4. Final change-scope and language audit
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 4. Final change-scope and language audit ==='

    $expectedFinalChanges = @(
        'docs/evidence/r2/w3/PLAN.md',
        'docs/evidence/r2/w3/README.md',
        'docs/evidence/r2/w3/hardware-classification-01.txt',
        'docs/evidence/r2/w3/hardware-inspection-01.txt',
        'docs/evidence/r2/w3/hardware-trace-01.csv',
        'docs/evidence/r2/w3/w3-evidence-manifest.sha256',
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

    $actualFinalChanges = Get-ChangedPaths

    $finalScopeDiff = Compare-Object `
        -ReferenceObject $expectedFinalChanges `
        -DifferenceObject $actualFinalChanges

    if ($finalScopeDiff) {
        $finalScopeDiff | Format-Table
        throw 'Final W3 checkpoint change scope is unexpected.'
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

    Write-Output 'Exact 15-file change scope: verified'
    Write-Output 'Repository English-only policy: PASS'
    Write-Output 'git diff --check: PASS'

    # -------------------------------------------------------------------------
    # 5. Stage exact W3 checkpoint
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 5. Stage exact W3 checkpoint ==='

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
        throw 'Staged W3 file set is unexpected.'
    }

    & $git diff --cached --check
    if ($LASTEXITCODE -ne 0) {
        throw 'git diff --cached --check failed.'
    }

    Write-Output 'Staged W3 file set: exact'
    Write-Output 'git diff --cached --check: PASS'

    # -------------------------------------------------------------------------
    # 6. Commit / push
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 6. Commit and push ==='

    & $git commit -m $subject
    if ($LASTEXITCODE -ne 0) {
        throw 'W3 checkpoint commit failed.'
    }

    $commit = (& $git rev-parse HEAD).Trim()
    $actualSubject = (& $git log -1 --pretty=%s).Trim()

    if ($actualSubject -ne $subject) {
        throw "Unexpected W3 commit subject: $actualSubject"
    }

    $commitPaths = @(
        & $git diff-tree --no-commit-id --name-only -r HEAD
    ) | Sort-Object

    $commitDiff = Compare-Object `
        -ReferenceObject $expectedFinalChanges `
        -DifferenceObject $commitPaths

    if ($commitDiff) {
        $commitDiff | Format-Table
        throw 'W3 checkpoint commit contains unexpected paths.'
    }

    & $git push origin main
    if ($LASTEXITCODE -ne 0) {
        throw 'W3 checkpoint push failed.'
    }

    & $git fetch origin main --quiet
    if ($LASTEXITCODE -ne 0) {
        throw 'Fetch after W3 push failed.'
    }

    $localHead = (& $git rev-parse HEAD).Trim()
    $remoteHead = (& $git rev-parse origin/main).Trim()

    if ($localHead -ne $remoteHead) {
        throw 'main and origin/main are not synchronized after W3 push.'
    }

    # -------------------------------------------------------------------------
    # 7. Final invariants
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 7. Final invariants ==='

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
        throw 'Working tree is not clean after W3 checkpoint.'
    }

    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R2-W3 HARDWARE CHECKPOINT / EVIDENCE SEAL: PASS'
    Write-Output '============================================================'
    Write-Output "Checkpoint commit: $commit"
    Write-Output "Subject: $subject"
    Write-Output "Hardware-tested W3 ELF SHA256: $expectedElfHash"
    Write-Output 'Real-hardware rebinds: 8 / 8 PASS'
    Write-Output 'Nominal-to-commit maximum: 5206 cycles'
    Write-Output 'DMA TE/DME/FE: 0'
    Write-Output 'ADC OVR: 0'
    Write-Output 'Ownership violations: 0'
    Write-Output 'DMA-slot violations: 0'
    Write-Output 'Completed samples validated: 2048'
    Write-Output 'Canary errors: 0'
    Write-Output 'W3 evidence package: preserved'
    Write-Output 'main / origin/main: synchronized'
    Write-Output 'Working tree: clean'
    Write-Output "r1-pass -> $r1After"
    Write-Output 'r2-pass: NOT CREATED'
    Write-Output ''
    Write-Output 'R2-W3: CLOSED / KNOWN-GOOD'
    Write-Output 'R2-W4: NOT STARTED'
    Write-Output 'R2 overall: IN PROGRESS'
}
catch {
    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R2-W3 HARDWARE CHECKPOINT / EVIDENCE SEAL: FAIL'
    Write-Output '============================================================'
    Write-Output $_.Exception.Message
    Write-Output ''
    Write-Output 'Do not begin R2-W4.'
    exit 1
}
