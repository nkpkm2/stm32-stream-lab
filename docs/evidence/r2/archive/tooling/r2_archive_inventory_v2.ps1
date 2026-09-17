param()

$ErrorActionPreference = 'Stop'

$repo = 'E:\Projects\stm32-stream-lab'
$tempa = Join-Path $HOME 'Downloads\tempa'
$outRoot = Join-Path $HOME ('Downloads\r2-archive-inventory-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))

$expectedHead = '2752c0e915ab4725cc13e400d16fe47b14adfd4e'

$cells = @(
    [pscustomobject]@{
        Cell='K1-NORMAL'
        Build='r2-w6-w6target-k1-normal-16185105'
        Elf='311835D2C505F239C0791F0C144548B24CC5E7BD5E177C5DB2FAF6FA8DF62A93'
        Trace='0A02ADF3465F913F3B16ACBE574EF962DB39E294BF1EAC7915B644EA44776CCD'
    },
    [pscustomobject]@{
        Cell='K1-DROP'
        Build='r2-w6-w6target-k1-drop-eeb059ed'
        Elf='0998BEC54FDCFBA81A5E995DB892A3CEF0B1E6C0C910622CB5B3E7311FB429CE'
        Trace='ABB4414C175478190235026A7B4BB23E38723B9762DFB02E130CD5E3DB8D24B9'
    },
    [pscustomobject]@{
        Cell='K2-NORMAL'
        Build='r2-w6-w6target-k2-normal-1518c429'
        Elf='1D44D800B5940882F00D1E6CCCD1FF6E2A30B3661BB19EAE37292E022EE8C3F7'
        Trace='03C735C8368F4025D965B7C46555377758E45CC68D3378F55F94D4E373B81070'
    },
    [pscustomobject]@{
        Cell='K2-DROP'
        Build='r2-w6-w6target-k2-drop-e9491d79'
        Elf='B9D7222ADF3E4BEAD78F53E0F5FACC8C43691E1899E36DC72DCE0155BC3C1FA1'
        Trace='606A9995DEB8B30B7B827DCFFD450B51A99CBE6C3D2149C0765A10D503E2C857'
    },
    [pscustomobject]@{
        Cell='K4-NORMAL'
        Build='r2-w6-w6target-k4-normal-77d5741f'
        Elf='68599AA9820108398F80538286070E54DA3A53EC6968FBDBFD64B2E4C3FF32BF'
        Trace='0704CDB78C6A9B37E2F9CC11DFB7E319E0AE5D7E5B5F4C854BB237EB0FE134C1'
    },
    [pscustomobject]@{
        Cell='K4-DROP'
        Build='r2-w6-w6target-k4-drop-64e98478'
        Elf='C732A527A7B49BCAC109C3BCF5E6E402723A457574D447C616FA3D7B7C1E9134'
        Trace='216628C0B066C645E53A64A199E325665D12B40470A8E35FA8DE9A754677251D'
    },
    [pscustomobject]@{
        Cell='K8-NORMAL'
        Build='r2-w6-w6target-k8-normal-a6b96a0b'
        Elf='E102911CC6A2C81D65ED03BD63B1558018D59BB507436DD7D2E7B67D69F11B18'
        Trace='011D5953755288F595FD7FA415CF6D7D52D5AEF5EABF0685D2D342039D40D6A4'
    },
    [pscustomobject]@{
        Cell='K8-DROP'
        Build='r2-w6-w6target-k8-drop-7a95200b'
        Elf='B687A7D83E51533622AE77AABFC97E23FF0131964C6B7A2AD041AB48BB756581'
        Trace='8EB3A3281918AB4A984C7C549AD3C7604D9ECDB1F0051F6BCFD812990B60D555'
    }
)

function Get-HashRecord {
    param(
        [Parameter(Mandatory=$true)][string]$Root,
        [Parameter(Mandatory=$true)][System.IO.FileInfo]$File
    )

    $relative = $File.FullName.Substring($Root.Length).TrimStart('\')

    try {
        $hash = (Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash
        $status = 'OK'
        $errorText = ''
    }
    catch {
        $hash = ''
        $status = 'HASH_ERROR'
        $errorText = $_.Exception.Message
    }

    [pscustomobject]@{
        RelativePath = $relative
        FullPath = $File.FullName
        Length = $File.Length
        LastWriteTime = $File.LastWriteTime.ToString('o')
        SHA256 = $hash
        HashStatus = $status
        Error = $errorText
    }
}

if (-not (Test-Path -LiteralPath $repo -PathType Container)) {
    throw "Repository missing: $repo"
}

if (-not (Test-Path -LiteralPath $tempa -PathType Container)) {
    throw "Tempa directory missing: $tempa"
}

New-Item -ItemType Directory -Path $outRoot | Out-Null

Set-Location -LiteralPath $repo

Write-Output '=== R2 ARCHIVE INVENTORY PREFLIGHT ==='
Write-Output "Repository: $repo"
Write-Output "Tempa:      $tempa"
Write-Output "Output:     $outRoot"

$head = (git rev-parse HEAD).Trim()
Write-Output "HEAD:       $head"

if ($head -ne $expectedHead) {
    throw "Unexpected HEAD: $head"
}

$repoStatusPath = Join-Path $outRoot 'repo-status.txt'
$repoDiffPath = Join-Path $outRoot 'repo-diff-stat.txt'
$r2TrackedPath = Join-Path $outRoot 'repo-r2-tracked-files.txt'
$repoEvidenceCsv = Join-Path $outRoot 'repo-r2-evidence-files.csv'
$buildCsv = Join-Path $outRoot 'r2-build-evidence-files.csv'
$tempaCsv = Join-Path $outRoot 'tempa-files.csv'
$cellCsv = Join-Path $outRoot 'w6-cell-verification.csv'
$architectureCandidatesPath = Join-Path $outRoot 'architecture-candidates.txt'
$manifestPath = Join-Path $outRoot 'inventory-manifest.sha256'

Write-Output "`n[1/7] Git identity / status"

@(
    "HEAD=$head"
    "BRANCH=$((git branch --show-current).Trim())"
    "ORIGIN_MAIN=$((git rev-parse origin/main).Trim())"
    ''
    'STATUS:'
    (git status --short --branch)
    ''
    'RECENT COMMITS:'
    (git --no-pager log -12 --decorate --oneline)
) | Set-Content -LiteralPath $repoStatusPath -Encoding UTF8

@(
    'DIFF --STAT:'
    (git diff --stat)
    ''
    'DIFF --NAME-STATUS:'
    (git diff --name-status)
    ''
    'DIFF --CHECK:'
    (git diff --check)
) | Set-Content -LiteralPath $repoDiffPath -Encoding UTF8

git ls-files |
    Where-Object {
        $_ -match '(^|/)(r2|R2)(/|_|-)|docs/evidence/r2|tools/r2'
    } |
    Sort-Object |
    Set-Content -LiteralPath $r2TrackedPath -Encoding UTF8

Write-Output "[2/7] Repository R2 evidence hashes"

$repoEvidenceFiles = @(
    Get-ChildItem -LiteralPath (Join-Path $repo 'docs\evidence\r2') -File -Recurse -ErrorAction SilentlyContinue
)

$repoEvidenceRecords = @(
    foreach ($file in $repoEvidenceFiles) {
        Get-HashRecord -Root $repo -File $file
    }
)

$repoEvidenceRecords |
    Export-Csv -LiteralPath $repoEvidenceCsv -NoTypeInformation -Encoding UTF8

Write-Output "[3/7] W6 eight-cell identity verification"

$cellResults = @()

foreach ($cell in $cells) {
    $buildDir = Join-Path (Join-Path $repo 'build') $cell.Build
    $elf = Join-Path $buildDir 'cubemx.elf'

    if (-not (Test-Path -LiteralPath $buildDir -PathType Container)) {
        throw "Required W6 build directory missing: $buildDir"
    }

    if (-not (Test-Path -LiteralPath $elf -PathType Leaf)) {
        throw "Required W6 ELF missing: $elf"
    }

    $elfActual = (Get-FileHash -LiteralPath $elf -Algorithm SHA256).Hash

    $traceCandidates = @(
        Get-ChildItem -LiteralPath $buildDir -File -Filter 'hardware-trace-*.csv' -ErrorAction SilentlyContinue |
        Sort-Object Name
    )

    $matchingTrace = $null

    foreach ($candidate in $traceCandidates) {
        $candidateHash = (Get-FileHash -LiteralPath $candidate.FullName -Algorithm SHA256).Hash
        if ($candidateHash -eq $cell.Trace) {
            $matchingTrace = $candidate
            break
        }
    }

    $requiredEvidence = @(
        'verification-summary.txt',
        'elf.sha256',
        'hardware-flash-verify-01.txt',
        'hardware-reset-run-01.txt',
        'hardware-inspection-summary-01.txt'
    )

    $missing = @()

    foreach ($name in $requiredEvidence) {
        if (-not (Test-Path -LiteralPath (Join-Path $buildDir $name) -PathType Leaf)) {
            $missing += $name
        }
    }

    $cellPass = (
        $elfActual -eq $cell.Elf -and
        $null -ne $matchingTrace -and
        $missing.Count -eq 0
    )

    $cellResults += [pscustomobject]@{
        Cell = $cell.Cell
        BuildDirectory = $buildDir
        ExpectedElfSHA256 = $cell.Elf
        ActualElfSHA256 = $elfActual
        ElfMatch = ($elfActual -eq $cell.Elf)
        ExpectedTraceSHA256 = $cell.Trace
        MatchingTraceFile = $(if ($null -ne $matchingTrace) { $matchingTrace.FullName } else { '' })
        TraceMatch = ($null -ne $matchingTrace)
        MissingRequiredEvidence = ($missing -join ';')
        Verification = $(if ($cellPass) { 'PASS' } else { 'FAIL' })
    }

    Write-Output ("{0,-10} ELF={1} TRACE={2} REQUIRED={3}" -f `
        $cell.Cell,
        $(if ($elfActual -eq $cell.Elf) { 'PASS' } else { 'FAIL' }),
        $(if ($null -ne $matchingTrace) { 'PASS' } else { 'FAIL' }),
        $(if ($missing.Count -eq 0) { 'PASS' } else { 'FAIL' }))
}

$cellResults |
    Export-Csv -LiteralPath $cellCsv -NoTypeInformation -Encoding UTF8

if (@($cellResults | Where-Object { $_.Verification -ne 'PASS' }).Count -ne 0) {
    throw 'At least one W6 cell failed archive identity verification. Preserve inventory and stop.'
}

Write-Output "[4/7] R2 build/evidence inventory"

$buildRoots = @(
    Get-ChildItem -LiteralPath (Join-Path $repo 'build') -Directory -ErrorAction SilentlyContinue |
    Where-Object {
        $_.Name -match '^r2-(w3|w4|w5|w6)-'
    } |
    Sort-Object Name
)

$interestingExtensions = @(
    '.txt','.csv','.json','.sha256','.gdb','.map','.elf','.log','.ninja'
)

$buildRecords = @()

foreach ($dir in $buildRoots) {
    $files = @(
        Get-ChildItem -LiteralPath $dir.FullName -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object {
            $interestingExtensions -contains $_.Extension -or
            $_.Name -in @(
                'CMakeCache.txt',
                'compile_commands.json',
                'verification-summary.txt',
                'build.ninja'
            )
        }
    )

    foreach ($file in $files) {
        $record = Get-HashRecord -Root $repo -File $file
        $record | Add-Member -NotePropertyName BuildDirectory -NotePropertyValue $dir.Name
        $buildRecords += $record
    }
}

$buildRecords |
    Export-Csv -LiteralPath $buildCsv -NoTypeInformation -Encoding UTF8

Write-Output "[5/7] Full tempa inventory"

$tempaFiles = @(
    Get-ChildItem -LiteralPath $tempa -File -Recurse -Force -ErrorAction Stop |
    Sort-Object FullName
)

$tempaRecords = @(
    foreach ($file in $tempaFiles) {
        $record = Get-HashRecord -Root $tempa -File $file

        $likelyProject = (
            $file.Name -match '(?i)r2|w[1-6]|stm32|handoff|trace|verify|evidence|architecture|cubemx|dma|buffer'
        )

        $record | Add-Member -NotePropertyName LikelyProjectArtifact -NotePropertyValue $likelyProject
        $record
    }
)

$tempaRecords |
    Export-Csv -LiteralPath $tempaCsv -NoTypeInformation -Encoding UTF8

Write-Output ("Tempa files: {0}" -f $tempaRecords.Count)
Write-Output ("Likely project artifacts: {0}" -f @($tempaRecords | Where-Object { $_.LikelyProjectArtifact }).Count)

Write-Output "[6/7] Architecture candidate search"

$architectureCandidates = @()

foreach ($root in @($repo, $tempa, (Join-Path $HOME 'Downloads'))) {
    if (Test-Path -LiteralPath $root -PathType Container) {
        $architectureCandidates += @(
            Get-ChildItem -LiteralPath $root -File -Recurse -ErrorAction SilentlyContinue |
            Where-Object {
                $_.Name -match '(?i)Architecture.*v3\.2\.2|v3\.2\.2.*Architecture|Implementation_Baseline'
            } |
            Select-Object -ExpandProperty FullName
        )
    }
}

$architectureCandidates |
    Sort-Object -Unique |
    Set-Content -LiteralPath $architectureCandidatesPath -Encoding UTF8

Write-Output ("Architecture candidates found: {0}" -f @($architectureCandidates | Sort-Object -Unique).Count)

Write-Output "[7/8] Seal inventory outputs"

$inventoryFiles = @(
    Get-ChildItem -LiteralPath $outRoot -File |
    Where-Object { $_.FullName -ne $manifestPath } |
    Sort-Object Name
)

$manifestLines = @()

foreach ($file in $inventoryFiles) {
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    $manifestLines += "$hash  $($file.Name)"
}

[System.IO.File]::WriteAllLines(
    $manifestPath,
    $manifestLines,
    (New-Object System.Text.UTF8Encoding($false))
)

Write-Output "[8/8] Package inventory"

$zipPath = $outRoot + '.zip'

if (Test-Path -LiteralPath $zipPath) {
    throw "Inventory ZIP already exists unexpectedly: $zipPath"
}

Compress-Archive `
    -LiteralPath $outRoot `
    -DestinationPath $zipPath `
    -CompressionLevel Optimal

$zipHash = (
    Get-FileHash `
        -LiteralPath $zipPath `
        -Algorithm SHA256
).Hash

Write-Output ''
Write-Output '=== R2 ARCHIVE INVENTORY SUMMARY ==='
Write-Output 'W6 cells:                 8 / 8 identity PASS'
Write-Output "Repository R2 evidence:   $($repoEvidenceRecords.Count) files"
Write-Output "R2 build evidence index:  $($buildRecords.Count) files"
Write-Output "Tempa inventory:          $($tempaRecords.Count) files"
Write-Output "Architecture candidates:  $(@($architectureCandidates | Sort-Object -Unique).Count)"
Write-Output "Inventory directory:      $outRoot"
Write-Output "Manifest:                 $manifestPath"
Write-Output "Inventory ZIP:            $zipPath"
Write-Output "Inventory ZIP SHA256:     $zipHash"
Write-Output ''
Write-Output 'Repository modified: NO'
Write-Output 'Build/flash/reset:   NO'
Write-Output 'Commit/push:         NO'
