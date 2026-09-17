$ErrorActionPreference = 'Stop'

$repo = 'E:\Projects\stm32-stream-lab'
$git = 'C:\Program Files\Git\cmd\git.exe'

$expectedHead = 'ec2cc4244837d54d423ca9e240b50fe0a66509f1'

$shortRunElfHash = 'C0493EC1B31D504F404BB2ABD732B1B82038102EA975099651E4D225932F4DA7'
$lifecycleElfHash = 'CDB12A342F507C7660E7C25DB8F4F640A3F0A2652A0AD3A3683A22EB680037FC'
$soakElfHash = '67040A73D2072C569918FA3E3AB9B3F88B52C0901E211665ED99462BF33E6CBA'
$rawCsvHash = 'B01105298CDA8782E74D5E6702B5716293C23978D9AE6A9AF8567AF6B9DE1400'

$shortRunElf = Join-Path $repo 'build\r1-bringup-01\cubemx.elf'
$lifecycleElf = Join-Path $repo 'build\r1-lifecycle-01\cubemx.elf'
$soakElf = Join-Path $repo 'build\r1-soak-01\cubemx.elf'

$shortRunResult = Join-Path $repo 'build\r1-bringup-01\r1-bringup-inspection.txt'
$lifecycleResult = Join-Path $repo 'build\r1-lifecycle-01\r1-lifecycle-inspection.txt'
$soakResult = Join-Path $repo 'build\r1-soak-01\r1-soak-inspection.txt'

$evidenceDir = Join-Path $repo 'docs\evidence\r1'
$shortRunEvidence = Join-Path $evidenceDir 'r1-short-run-result-01.txt'
$lifecycleEvidence = Join-Path $evidenceDir 'r1-lifecycle-result-01.txt'
$buildManifest = Join-Path $evidenceDir 'r1-tested-builds.txt'

$rawCsv = Join-Path $evidenceDir 'r1-raw-snapshot.csv'
$rawMetadata = Join-Path $evidenceDir 'r1-raw-snapshot-metadata.txt'
$soakEvidence = Join-Path $evidenceDir 'r1-soak-result-01.txt'

Set-Location -LiteralPath $repo

Write-Output '=== R1 TESTED ARTIFACT / EVIDENCE CHECKPOINT ==='

try {
    # -------------------------------------------------------------------------
    # 1. Repository and tested-state guards
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 1. Repository and tested-state guards ==='

    $head = (& $git rev-parse HEAD).Trim()

    if ($head -ne $expectedHead) {
        throw "Unexpected HEAD: $head"
    }

    $stagedBefore = @(& $git diff --cached --name-only)

    if ($stagedBefore.Count -ne 0) {
        throw 'Staging area is not empty.'
    }

    $expectedCurrentPaths = @(
        'firmware/acquisition/r1_bringup.c',
        'firmware/acquisition/r1_bringup.h',
        'docs/evidence/r1/r1-raw-snapshot.csv',
        'docs/evidence/r1/r1-raw-snapshot-metadata.txt',
        'docs/evidence/r1/r1-soak-result-01.txt'
    ) | Sort-Object

    $statusBefore = @(
        & $git status --porcelain=v1 --untracked-files=all
    )

    $actualCurrentPaths = @(
        $statusBefore |
        ForEach-Object {
            if ($_.Length -ge 4) {
                $_.Substring(3)
            }
        }
    ) | Sort-Object

    $scopeDiff = Compare-Object `
        -ReferenceObject $expectedCurrentPaths `
        -DifferenceObject $actualCurrentPaths

    if ($scopeDiff) {
        $scopeDiff | Format-Table
        throw 'Current working-tree scope is not the tested formal-soak state.'
    }

    foreach ($path in @(
        $shortRunElf,
        $lifecycleElf,
        $soakElf,
        $shortRunResult,
        $lifecycleResult,
        $soakResult,
        $rawCsv,
        $rawMetadata,
        $soakEvidence
    )) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required tested artifact is missing: $path"
        }
    }

    Write-Output 'Repository scope: verified'
    Write-Output 'Required tested artifacts: present'

    # -------------------------------------------------------------------------
    # 2. Verify exact tested ELF hashes
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 2. Verify tested ELF hashes ==='

    $elfChecks = @(
        @('short-run', $shortRunElf, $shortRunElfHash),
        @('lifecycle', $lifecycleElf, $lifecycleElfHash),
        @('formal-soak', $soakElf, $soakElfHash)
    )

    foreach ($check in $elfChecks) {
        $name = $check[0]
        $path = $check[1]
        $expected = $check[2]

        $actual = (
            Get-FileHash `
                -LiteralPath $path `
                -Algorithm SHA256
        ).Hash

        if ($actual -ne $expected) {
            throw ("{0} ELF hash mismatch. Expected {1}, observed {2}" -f
                $name,
                $expected,
                $actual)
        }

        Write-Output ("PASS  {0} ELF SHA256: {1}" -f $name, $actual)
    }

    # -------------------------------------------------------------------------
    # 3. Verify hardware result records
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 3. Verify hardware result records ==='

    $shortText = [System.IO.File]::ReadAllText($shortRunResult)
    $lifeText = [System.IO.File]::ReadAllText($lifecycleResult)
    $soakText = [System.IO.File]::ReadAllText($soakResult)

    foreach ($required in @(
        'short_run_pass=1',
        'start_count=1',
        'tc_count=195',
        'ct_mismatch_count=0',
        'alternation_mismatch_count=0',
        'suspected_event_loss_count=0',
        'adc_ovr_count=0',
        'dma_te_count=0',
        'dma_dme_count=0',
        'dma_fe_count=0'
    )) {
        if (-not $shortText.Contains($required)) {
            throw "Short-run result is missing: $required"
        }
    }

    foreach ($required in @(
        'magic=0x52314c31',
        'task_created=1',
        'phase=4',
        'completed_cycles=6',
        'all_pass=1'
    )) {
        if (-not $lifeText.ToLowerInvariant().Contains(
            $required.ToLowerInvariant())) {
            throw "Lifecycle result is missing: $required"
        }
    }

    for ($i = 0; $i -lt 6; $i++) {
        foreach ($required in @(
            "c${i}_start_ndtr=256",
            "c${i}_start_ct=0",
            "c${i}_ct_mismatch_count=0",
            "c${i}_alternation_mismatch_count=0",
            "c${i}_suspected_event_loss_count=0",
            "c${i}_adc_ovr_count=0",
            "c${i}_dma_te_count=0",
            "c${i}_dma_dme_count=0",
            "c${i}_dma_fe_count=0",
            "c${i}_cycle_pass=1"
        )) {
            if (-not $lifeText.Contains($required)) {
                throw "Lifecycle result is missing: $required"
            }
        }
    }

    foreach ($required in @(
        'soak_pass=1',
        'phase=5',
        'elapsed_ms=600000',
        'expected_tc_count=468750',
        'actual_tc_count=468749',
        'tc_count_error=-1',
        'ct_mismatch_count=0',
        'alternation_mismatch_count=0',
        'suspected_event_loss_count=0',
        'adc_ovr_count=0',
        'dma_te_count=0',
        'dma_dme_count=0',
        'dma_fe_count=0',
        'system_core_clock=180000000'
    )) {
        if (-not $soakText.Contains($required)) {
            throw "Formal-soak result is missing: $required"
        }
    }

    Write-Output 'Short-run hardware result: verified'
    Write-Output 'Lifecycle regression result: verified'
    Write-Output 'Formal 10-minute soak result: verified'

    # -------------------------------------------------------------------------
    # 4. Verify raw snapshot evidence
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 4. Verify raw snapshot evidence ==='

    $actualRawHash = (
        Get-FileHash `
            -LiteralPath $rawCsv `
            -Algorithm SHA256
    ).Hash

    if ($actualRawHash -ne $rawCsvHash) {
        throw "Raw snapshot CSV hash mismatch: $actualRawHash"
    }

    $csvLines = [System.IO.File]::ReadAllLines($rawCsv)

    if ($csvLines.Count -ne 257) {
        throw "Raw snapshot CSV line count is wrong: $($csvLines.Count)"
    }

    if ($csvLines[0] -ne 'sample_index,adc_raw') {
        throw 'Raw snapshot CSV header is wrong.'
    }

    Write-Output "Raw snapshot CSV SHA256: $actualRawHash"
    Write-Output 'Raw snapshot sample count: 256'

    # -------------------------------------------------------------------------
    # 5. Preserve remaining hardware-result records
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 5. Preserve remaining hardware-result records ==='

    Copy-Item `
        -LiteralPath $shortRunResult `
        -Destination $shortRunEvidence `
        -Force

    Copy-Item `
        -LiteralPath $lifecycleResult `
        -Destination $lifecycleEvidence `
        -Force

    $enc = New-Object System.Text.UTF8Encoding($false)

    $manifest = @(
        'R1 tested-build manifest',
        '',
        'Short-run hardware bringup',
        'Source checkpoint commit: 8475676b7ffd657c2bcb6a56328322a15a400530',
        ('ELF SHA256: {0}' -f $shortRunElfHash),
        'Hardware classification: PASS',
        '',
        'Lifecycle regression',
        'Source checkpoint commit: ec2cc4244837d54d423ca9e240b50fe0a66509f1',
        ('ELF SHA256: {0}' -f $lifecycleElfHash),
        'Completed cycles: 6 / 6',
        'Hardware classification: PASS',
        '',
        'Formal 10-minute acquisition soak',
        'Base commit before soak-harness integration: ec2cc4244837d54d423ca9e240b50fe0a66509f1',
        ('ELF SHA256: {0}' -f $soakElfHash),
        'Duration: 600000 ms',
        'Expected transfer completions: 468750',
        'Observed transfer completions: 468749',
        'Transfer-completion count error: -1',
        'Hardware classification: PASS',
        '',
        'Raw snapshot',
        ('CSV SHA256: {0}' -f $rawCsvHash),
        'Samples: 256',
        'Input condition: PA0 / ADC1_IN0 connected to GND',
        'Selected fully completed DMA target: M0',
        'Observed raw range: 0 .. 8'
    )

    [System.IO.File]::WriteAllLines(
        $buildManifest,
        $manifest,
        $enc
    )

    Write-Output 'Short-run result: copied into repository evidence'
    Write-Output 'Lifecycle result: copied into repository evidence'
    Write-Output 'Tested-build manifest: created'

    # -------------------------------------------------------------------------
    # 6. Verify exact final change scope
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 6. Verify final change scope ==='

    $expectedFinalPaths = @(
        'firmware/acquisition/r1_bringup.c',
        'firmware/acquisition/r1_bringup.h',
        'docs/evidence/r1/r1-raw-snapshot.csv',
        'docs/evidence/r1/r1-raw-snapshot-metadata.txt',
        'docs/evidence/r1/r1-short-run-result-01.txt',
        'docs/evidence/r1/r1-lifecycle-result-01.txt',
        'docs/evidence/r1/r1-soak-result-01.txt',
        'docs/evidence/r1/r1-tested-builds.txt'
    ) | Sort-Object

    $statusAfter = @(
        & $git status --porcelain=v1 --untracked-files=all
    )

    $actualFinalPaths = @(
        $statusAfter |
        ForEach-Object {
            if ($_.Length -ge 4) {
                $_.Substring(3)
            }
        }
    ) | Sort-Object

    $finalScopeDiff = Compare-Object `
        -ReferenceObject $expectedFinalPaths `
        -DifferenceObject $actualFinalPaths

    if ($finalScopeDiff) {
        $finalScopeDiff | Format-Table
        throw 'Final evidence/checkpoint change scope is unexpected.'
    }

    Write-Output 'Final repository change scope: verified'

    # -------------------------------------------------------------------------
    # 7. Stage and validate
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 7. Stage and validate ==='

    & $git add -- `
        firmware/acquisition/r1_bringup.c `
        firmware/acquisition/r1_bringup.h `
        docs/evidence/r1/r1-raw-snapshot.csv `
        docs/evidence/r1/r1-raw-snapshot-metadata.txt `
        docs/evidence/r1/r1-short-run-result-01.txt `
        docs/evidence/r1/r1-lifecycle-result-01.txt `
        docs/evidence/r1/r1-soak-result-01.txt `
        docs/evidence/r1/r1-tested-builds.txt

    if ($LASTEXITCODE -ne 0) {
        throw 'git add failed.'
    }

    $staged = @(
        & $git diff --cached --name-only
    ) | Sort-Object

    $stageDiff = Compare-Object `
        -ReferenceObject $expectedFinalPaths `
        -DifferenceObject $staged

    if ($stageDiff) {
        $stageDiff | Format-Table
        throw 'Staged file set is incorrect.'
    }

    & $git diff --cached --check

    if ($LASTEXITCODE -ne 0) {
        throw 'Staged R1 tested artifacts failed git diff --check.'
    }

    Write-Output 'Staged file set: verified'
    Write-Output 'git diff --cached --check: PASS'

    # -------------------------------------------------------------------------
    # 8. Commit and push
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 8. Commit and push ==='

    $subject = 'test: record R1 acquisition integrity evidence'

    & $git commit -m $subject

    if ($LASTEXITCODE -ne 0) {
        throw 'R1 tested-artifact checkpoint commit failed.'
    }

    $commit = (& $git rev-parse HEAD).Trim()
    $actualSubject = (& $git log -1 --pretty=%s).Trim()

    if ($actualSubject -ne $subject) {
        throw "Unexpected commit subject: $actualSubject"
    }

    & $git push origin main

    if ($LASTEXITCODE -ne 0) {
        throw 'Push failed.'
    }

    & $git fetch origin main --quiet

    if ($LASTEXITCODE -ne 0) {
        throw 'Fetch after push failed.'
    }

    $localHead = (& $git rev-parse HEAD).Trim()
    $remoteHead = (& $git rev-parse origin/main).Trim()

    if ($localHead -ne $remoteHead) {
        throw 'main and origin/main are not synchronized.'
    }

    # -------------------------------------------------------------------------
    # 9. Final repository state
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 9. Final repository state ==='

    $finalStatus = @(
        & $git status --porcelain=v1 --untracked-files=all
    )

    if ($finalStatus.Count -ne 0) {
        $finalStatus
        throw 'Working tree is not clean.'
    }

    $localR1Pass = @(& $git tag --list 'r1-pass')

    if ($localR1Pass.Count -ne 0) {
        throw 'Local r1-pass tag exists unexpectedly.'
    }

    $remoteR1Pass = @(
        & $git ls-remote --tags origin 'refs/tags/r1-pass'
    )

    if ($remoteR1Pass.Count -ne 0) {
        throw 'Remote r1-pass tag exists unexpectedly.'
    }

    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R1 TESTED ARTIFACT / EVIDENCE CHECKPOINT: PASS'
    Write-Output '============================================================'
    Write-Output "Checkpoint commit: $commit"
    Write-Output "Subject: $subject"
    Write-Output "Short-run ELF SHA256: $shortRunElfHash"
    Write-Output "Lifecycle ELF SHA256: $lifecycleElfHash"
    Write-Output "Formal-soak ELF SHA256: $soakElfHash"
    Write-Output "Raw snapshot CSV SHA256: $rawCsvHash"
    Write-Output 'Short-run hardware evidence: preserved'
    Write-Output 'Lifecycle regression evidence: preserved'
    Write-Output 'Formal soak evidence: preserved'
    Write-Output '256-sample raw snapshot: preserved'
    Write-Output 'main / origin/main: synchronized'
    Write-Output 'Working tree: clean'
    Write-Output 'r1-pass: NOT CREATED'
    Write-Output 'R1 overall: IN PROGRESS'
}
catch {
    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R1 TESTED ARTIFACT / EVIDENCE CHECKPOINT: FAIL'
    Write-Output '============================================================'
    Write-Output $_.Exception.Message
    Write-Output ''
    Write-Output 'Do not begin final committed-state closeout yet.'
    exit 1
}
