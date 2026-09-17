param()

$ErrorActionPreference = 'Stop'

$repo = 'E:\Projects\stm32-stream-lab'
$downloads = Join-Path $HOME 'Downloads'
$tempa = Join-Path $downloads 'tempa'
$buildRoot = Join-Path $repo 'build'

$firmwareMilestone = 'bbc3bf6821c4f6e4dc73beabd5685b8b0828205c'
$w5Milestone = '2752c0e915ab4725cc13e400d16fe47b14adfd4e'

$precommitRepro =
    Join-Path $buildRoot 'r2-w6-precommit-repro-v3-20260918-010615'

$committedRegression =
    Join-Path $buildRoot 'r2-w6-committed-regression-20260918-011211'

$r2Root = Join-Path $repo 'docs\evidence\r2'
$w6Root = Join-Path $r2Root 'w6'
$cellsRoot = Join-Path $w6Root 'cells'
$regressionRoot = Join-Path $w6Root 'regression'
$archiveRoot = Join-Path $r2Root 'archive'
$toolingRoot = Join-Path $archiveRoot 'tooling'
$architectureRoot = Join-Path $archiveRoot 'architecture'
$inventoryRoot = Join-Path $archiveRoot 'inventory'
$anchorsRoot = Join-Path $archiveRoot 'hardware-anchors'

$cells = @(
    [pscustomobject]@{
        Slug='k1-normal'; K=1; Mode='NORMAL'
        Build='r2-w6-w6target-k1-normal-16185105'
        Elf='311835D2C505F239C0791F0C144548B24CC5E7BD5E177C5DB2FAF6FA8DF62A93'
        Trace='0A02ADF3465F913F3B16ACBE574EF962DB39E294BF1EAC7915B644EA44776CCD'
    },
    [pscustomobject]@{
        Slug='k1-drop'; K=1; Mode='DROP'
        Build='r2-w6-w6target-k1-drop-eeb059ed'
        Elf='0998BEC54FDCFBA81A5E995DB892A3CEF0B1E6C0C910622CB5B3E7311FB429CE'
        Trace='ABB4414C175478190235026A7B4BB23E38723B9762DFB02E130CD5E3DB8D24B9'
    },
    [pscustomobject]@{
        Slug='k2-normal'; K=2; Mode='NORMAL'
        Build='r2-w6-w6target-k2-normal-1518c429'
        Elf='1D44D800B5940882F00D1E6CCCD1FF6E2A30B3661BB19EAE37292E022EE8C3F7'
        Trace='03C735C8368F4025D965B7C46555377758E45CC68D3378F55F94D4E373B81070'
    },
    [pscustomobject]@{
        Slug='k2-drop'; K=2; Mode='DROP'
        Build='r2-w6-w6target-k2-drop-e9491d79'
        Elf='B9D7222ADF3E4BEAD78F53E0F5FACC8C43691E1899E36DC72DCE0155BC3C1FA1'
        Trace='606A9995DEB8B30B7B827DCFFD450B51A99CBE6C3D2149C0765A10D503E2C857'
    },
    [pscustomobject]@{
        Slug='k4-normal'; K=4; Mode='NORMAL'
        Build='r2-w6-w6target-k4-normal-77d5741f'
        Elf='68599AA9820108398F80538286070E54DA3A53EC6968FBDBFD64B2E4C3FF32BF'
        Trace='0704CDB78C6A9B37E2F9CC11DFB7E319E0AE5D7E5B5F4C854BB237EB0FE134C1'
    },
    [pscustomobject]@{
        Slug='k4-drop'; K=4; Mode='DROP'
        Build='r2-w6-w6target-k4-drop-64e98478'
        Elf='C732A527A7B49BCAC109C3BCF5E6E402723A457574D447C616FA3D7B7C1E9134'
        Trace='216628C0B066C645E53A64A199E325665D12B40470A8E35FA8DE9A754677251D'
    },
    [pscustomobject]@{
        Slug='k8-normal'; K=8; Mode='NORMAL'
        Build='r2-w6-w6target-k8-normal-a6b96a0b'
        Elf='E102911CC6A2C81D65ED03BD63B1558018D59BB507436DD7D2E7B67D69F11B18'
        Trace='011D5953755288F595FD7FA415CF6D7D52D5AEF5EABF0685D2D342039D40D6A4'
    },
    [pscustomobject]@{
        Slug='k8-drop'; K=8; Mode='DROP'
        Build='r2-w6-w6target-k8-drop-7a95200b'
        Elf='B687A7D83E51533622AE77AABFC97E23FF0131964C6B7A2AD041AB48BB756581'
        Trace='8EB3A3281918AB4A984C7C549AD3C7604D9ECDB1F0051F6BCFD812990B60D555'
    }
)

$anchors = @(
    [pscustomobject]@{
        Label='R1'
        Build='r2-w5-r1baseline-230b5252'
    },
    [pscustomobject]@{
        Label='W3'
        Build='r2-w3-w3target-23c9886a'
    },
    [pscustomobject]@{
        Label='W4'
        Build='r2-w4-w4target-e2e7b31f'
    },
    [pscustomobject]@{
        Label='W5'
        Build='r2-w5-w5target-c4b79468'
    }
)

function Ensure-NewDirectory {
    param([Parameter(Mandatory=$true)][string]$Path)

    if (Test-Path -LiteralPath $Path) {
        throw "Refusing to overwrite existing archive directory: $Path"
    }

    New-Item -ItemType Directory -Path $Path | Out-Null
}

function Copy-ExactFile {
    param(
        [Parameter(Mandatory=$true)][string]$Source,
        [Parameter(Mandatory=$true)][string]$Destination
    )

    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required source file missing: $Source"
    }

    $parent = Split-Path -Parent $Destination

    if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }

    if (Test-Path -LiteralPath $Destination) {
        throw "Refusing to overwrite: $Destination"
    }

    Copy-Item -LiteralPath $Source -Destination $Destination

    $srcHash = (
        Get-FileHash -LiteralPath $Source -Algorithm SHA256
    ).Hash

    $dstHash = (
        Get-FileHash -LiteralPath $Destination -Algorithm SHA256
    ).Hash

    if ($srcHash -ne $dstHash) {
        throw "Copy hash mismatch: $Source -> $Destination"
    }

    return $dstHash
}

function Get-KeyValueFile {
    param([Parameter(Mandatory=$true)][string]$Path)

    $map = @{}

    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^([A-Za-z0-9_]+)=(0x[0-9A-Fa-f]+|[0-9]+)$') {
            $map[$Matches[1]] = $Matches[2]
        }
    }

    return $map
}

function Get-TraceByHash {
    param(
        [Parameter(Mandatory=$true)][string]$BuildDir,
        [Parameter(Mandatory=$true)][string]$ExpectedHash
    )

    $matches = @()

    foreach ($file in @(
        Get-ChildItem `
            -LiteralPath $BuildDir `
            -File `
            -Filter 'hardware-trace-*.csv' `
            -ErrorAction SilentlyContinue
    )) {
        $hash = (
            Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256
        ).Hash

        if ($hash -eq $ExpectedHash) {
            $matches += $file
        }
    }

    if ($matches.Count -ne 1) {
        throw "Expected exactly one trace with SHA256 $ExpectedHash in $BuildDir; found $($matches.Count)."
    }

    return $matches[0].FullName
}

function Write-Manifest {
    param(
        [Parameter(Mandatory=$true)][string]$Root,
        [Parameter(Mandatory=$true)][string]$Manifest
    )

    $lines = @()

    foreach ($file in @(
        Get-ChildItem `
            -LiteralPath $Root `
            -File `
            -Recurse `
            -Force |
        Where-Object {
            $_.FullName -ne $Manifest
        } |
        Sort-Object FullName
    )) {
        $relative = $file.FullName.Substring($Root.Length).TrimStart('\')

        $hash = (
            Get-FileHash `
                -LiteralPath $file.FullName `
                -Algorithm SHA256
        ).Hash

        $lines += "$hash  $relative"
    }

    [System.IO.File]::WriteAllLines(
        $Manifest,
        $lines,
        (New-Object System.Text.UTF8Encoding($false))
    )
}

Set-Location -LiteralPath $repo

Write-Output '=== R2 EVIDENCE CONSOLIDATION PREFLIGHT ==='

$head = (git rev-parse HEAD).Trim()
$parent = (git rev-parse HEAD^).Trim()
$subject = (git log -1 --pretty=%s).Trim()

Write-Output "HEAD:    $head"
Write-Output "Parent:  $parent"
Write-Output "Subject: $subject"

if ($head -ne $firmwareMilestone) {
    throw "Unexpected firmware milestone HEAD: $head"
}

if ($parent -ne $w5Milestone) {
    throw "Unexpected W6 parent: $parent"
}

if ($subject -ne 'feat: establish R2 mandatory K matrix') {
    throw "Unexpected W6 subject: $subject"
}

git diff --exit-code -- HEAD --

if ($LASTEXITCODE -ne 0) {
    throw 'Tracked working tree differs from firmware milestone.'
}

git diff --cached --exit-code

if ($LASTEXITCODE -ne 0) {
    throw 'Git index is not clean.'
}

$untracked = @(
    git ls-files `
        --others `
        --exclude-standard
)

$unexpected = @(
    $untracked |
    Where-Object {
        $_ -notlike 'docs/evidence/r2/w6/*'
    }
)

if ($unexpected.Count -ne 0) {
    Write-Output 'Unexpected untracked repo files:'
    $unexpected
    throw 'Unexpected untracked files present.'
}

if (-not (Test-Path -LiteralPath (Join-Path $w6Root 'PLAN.md') -PathType Leaf)) {
    throw 'Existing W6 PLAN.md is missing.'
}

foreach ($path in @(
    $tempa,
    $precommitRepro,
    $committedRegression
)) {
    if (-not (Test-Path -LiteralPath $path -PathType Container)) {
        throw "Required evidence directory missing: $path"
    }
}

Write-Output 'Preflight: PASS'

Write-Output "`n=== CREATE EVIDENCE DIRECTORY STRUCTURE ==="

foreach ($path in @(
    $cellsRoot,
    $regressionRoot,
    $archiveRoot,
    $toolingRoot,
    $architectureRoot,
    $inventoryRoot,
    $anchorsRoot
)) {
    if (-not (Test-Path -LiteralPath $path)) {
        New-Item -ItemType Directory -Path $path -Force | Out-Null
    }
}

# Refuse if a previous formal cell archive already exists.
if (@(Get-ChildItem -LiteralPath $cellsRoot -Force -ErrorAction SilentlyContinue).Count -ne 0) {
    throw "W6 cells archive is not empty: $cellsRoot"
}

Write-Output "`n=== COPY EIGHT W6 HARDWARE CELLS ==="

$matrixRows = @()

foreach ($cell in $cells) {
    $buildDir = Join-Path $buildRoot $cell.Build
    $cellDir = Join-Path $cellsRoot $cell.Slug

    if (-not (Test-Path -LiteralPath $buildDir -PathType Container)) {
        throw "W6 build missing: $buildDir"
    }

    New-Item -ItemType Directory -Path $cellDir | Out-Null

    $elf = Join-Path $buildDir 'cubemx.elf'
    $elfHash = (
        Get-FileHash -LiteralPath $elf -Algorithm SHA256
    ).Hash

    if ($elfHash -ne $cell.Elf) {
        throw "$($cell.Slug) ELF identity mismatch."
    }

    $trace = Get-TraceByHash `
        -BuildDir $buildDir `
        -ExpectedHash $cell.Trace

    $coreNames = @(
        'cubemx.elf',
        'cubemx.map',
        'elf.sha256',
        'verification-summary.txt',
        'hardware-flash-verify-01.txt',
        'hardware-reset-run-01.txt',
        'hardware-inspection-summary-01.txt'
    )

    foreach ($name in $coreNames) {
        $source = Join-Path $buildDir $name

        if (Test-Path -LiteralPath $source -PathType Leaf) {
            $null = Copy-ExactFile `
                -Source $source `
                -Destination (Join-Path $cellDir $name)
        }
        elseif ($name -in @('cubemx.elf','elf.sha256','verification-summary.txt','hardware-flash-verify-01.txt','hardware-reset-run-01.txt','hardware-inspection-summary-01.txt')) {
            throw "Required W6 evidence missing: $source"
        }
    }

    $null = Copy-ExactFile `
        -Source $trace `
        -Destination (Join-Path $cellDir 'hardware-trace.csv')

    # Preserve all raw trace export, validator, GDB script, and GDB server logs associated with the final trace.
    $traceBase = [System.IO.Path]::GetFileNameWithoutExtension($trace)
    $traceSuffix = $traceBase -replace '^hardware-trace-', ''

    $optionalMap = [ordered]@{
        "hardware-trace-export-$traceSuffix.txt" = 'hardware-trace-export.txt'
        "hardware-trace-export-$traceSuffix.gdb" = 'hardware-trace-export.gdb'
        "validate-hardware-trace-$traceSuffix.py" = 'validate-hardware-trace.py'
        "hardware-gdbserver-trace-$traceSuffix.stdout.txt" = 'hardware-gdbserver-trace.stdout.txt'
        "hardware-gdbserver-trace-$traceSuffix.stderr.txt" = 'hardware-gdbserver-trace.stderr.txt'
        'hardware-inspection-summary-01.gdb' = 'hardware-inspection-summary.gdb'
        'hardware-gdbserver-summary-01.stdout.txt' = 'hardware-gdbserver-summary.stdout.txt'
        'hardware-gdbserver-summary-01.stderr.txt' = 'hardware-gdbserver-summary.stderr.txt'
    }

    foreach ($sourceName in $optionalMap.Keys) {
        $source = Join-Path $buildDir $sourceName

        if (Test-Path -LiteralPath $source -PathType Leaf) {
            $null = Copy-ExactFile `
                -Source $source `
                -Destination (Join-Path $cellDir $optionalMap[$sourceName])
        }
    }

    $inspectionPath = Join-Path $buildDir 'hardware-inspection-summary-01.txt'
    $kv = Get-KeyValueFile -Path $inspectionPath

    $traceHash = (
        Get-FileHash -LiteralPath $trace -Algorithm SHA256
    ).Hash

    $matrixRows += [pscustomobject]@{
        Cell = "K=$($cell.K) $($cell.Mode)"
        K = $cell.K
        Mode = $cell.Mode
        BuildDirectory = $cell.Build
        FirmwareMilestone = $firmwareMilestone
        ElfSHA256 = $cell.Elf
        TraceSHA256 = $traceHash
        InputEvents = $kv['input_count']
        Admissions = $kv['admitted_count']
        CapacityDrops = $kv['capacity_drop_count']
        Processed = $kv['processed_count']
        Released = $kv['released_count']
        Recoveries = $kv['recovered_admission_after_drop_count']
        MaxDropStreak = $kv['max_drop_streak']
        MaxDecisionCycles = $kv['max_nominal_to_decision_cycles']
        MaxIrqExitCycles = $kv['max_nominal_to_irq_exit_cycles']
        MaxFinalWindowCycles = $kv['max_final_window_cycles']
        FullSampleCount = $kv['full_sample_count']
        SampleErrors = $kv['sample_errors']
        CanaryErrors = $kv['canary_errors']
        FaultBits = $kv['fault_bits']
        PoolViolations = $kv['pool_violation_count']
        SlotViolations = $kv['slots_violation_count']
        TokenLedgerErrors = $kv['token_ledger_errors']
        DmaErrorFlags = $kv['dma_error_flags_seen']
        AdcOverrun = $kv['adc_ovr_seen']
        Result = 'PASS'
    }

    $cellManifest = Join-Path $cellDir 'manifest.sha256'
    Write-Manifest -Root $cellDir -Manifest $cellManifest

    Write-Output "$($cell.Slug): PASS"
}

$matrixCsv = Join-Path $w6Root 'hardware-matrix.csv'

$matrixRows |
    Export-Csv `
        -LiteralPath $matrixCsv `
        -NoTypeInformation `
        -Encoding UTF8

Write-Output "`n=== COPY REPRODUCIBILITY / REGRESSION EVIDENCE ==="

$preDst = Join-Path $regressionRoot 'precommit-reproducibility'
$committedDst = Join-Path $regressionRoot 'committed-state'

if (Test-Path -LiteralPath $preDst) {
    throw "Regression archive already exists: $preDst"
}

if (Test-Path -LiteralPath $committedDst) {
    throw "Regression archive already exists: $committedDst"
}

Copy-Item `
    -LiteralPath $precommitRepro `
    -Destination $preDst `
    -Recurse

Copy-Item `
    -LiteralPath $committedRegression `
    -Destination $committedDst `
    -Recurse

Write-Manifest `
    -Root $preDst `
    -Manifest (Join-Path $preDst 'manifest.sha256')

# committed-state already contains regression-manifest.sha256; retain it and add archive-level manifest.
Write-Manifest `
    -Root $committedDst `
    -Manifest (Join-Path $committedDst 'archive-manifest.sha256')

Write-Output 'Regression evidence copied: PASS'

Write-Output "`n=== COPY HISTORICAL HARDWARE ANCHORS ==="

foreach ($anchor in $anchors) {
    $sourceDir = Join-Path $buildRoot $anchor.Build
    $destDir = Join-Path $anchorsRoot $anchor.Label.ToLowerInvariant()

    if (-not (Test-Path -LiteralPath $sourceDir -PathType Container)) {
        throw "Historical anchor build missing: $sourceDir"
    }

    New-Item -ItemType Directory -Path $destDir | Out-Null

    foreach ($name in @(
        'cubemx.elf',
        'cubemx.map',
        'elf.sha256',
        'verification-summary.txt'
    )) {
        $source = Join-Path $sourceDir $name

        if (Test-Path -LiteralPath $source -PathType Leaf) {
            $null = Copy-ExactFile `
                -Source $source `
                -Destination (Join-Path $destDir $name)
        }
    }

    Write-Manifest `
        -Root $destDir `
        -Manifest (Join-Path $destDir 'manifest.sha256')

    Write-Output "$($anchor.Label) anchor copied"
}

Write-Output "`n=== ARCHIVE TEMPA TOP-LEVEL DOWNLOADS ==="

$tempaTop = @(
    Get-ChildItem `
        -LiteralPath $tempa `
        -File `
        -Force |
    Sort-Object Name
)

$toolingRows = @()

foreach ($file in $tempaTop) {
    $destination = Join-Path $toolingRoot $file.Name
    $hash = Copy-ExactFile `
        -Source $file.FullName `
        -Destination $destination

    $status = 'PRESERVED'
    $role = 'DOWNLOAD_OR_TOOL'

    switch -Regex ($file.Name) {
        '^r2_w6_k1_drop_trace_retry\.ps1$' {
            $status='SUPERSEDED'; $role='HOST_TRACE_TOOL_FAILED'; break
        }
        '^r2_w6_run_summary\.ps1$' {
            $status='SUPERSEDED'; $role='HOST_SUMMARY_TOOL_BOUNDARY_BUG'; break
        }
        '^r2_w6_trace_validate\.ps1$' {
            $status='SUPERSEDED'; $role='HOST_TRACE_VALIDATOR_SUPERSEDED'; break
        }
        '^r2_w6_precommit_repro\.ps1$' {
            $status='SUPERSEDED'; $role='PRECOMMIT_WRAPPER_FAILED'; break
        }
        '^r2_w6_precommit_repro_v2\.ps1$' {
            $status='SUPERSEDED'; $role='PRECOMMIT_WRAPPER_FAILED'; break
        }
        '^r2_w6_precommit_repro_v3\.ps1$' {
            $status='SUPERSEDED'; $role='PRECOMMIT_PARSER_FAILED'; break
        }
        '^r2_w6_precommit_repro_v4\.ps1$' {
            $status='FORMAL'; $role='PRECOMMIT_REPRODUCIBILITY'; break
        }
        '^r2_w6_run_summary_v2\.ps1$' {
            $status='FORMAL'; $role='HARDWARE_SUMMARY_VALIDATOR'; break
        }
        '^r2_w6_trace_validate_v2\.ps1$' {
            $status='FORMAL'; $role='HARDWARE_TRACE_VALIDATOR'; break
        }
        '^r2_w6_committed_regression\.ps1$' {
            $status='FORMAL'; $role='COMMITTED_STATE_REGRESSION'; break
        }
        '^r2_archive_inventory_v2\.ps1$' {
            $status='FORMAL'; $role='ARCHIVE_INVENTORY'; break
        }
        '^r2_w6_k_matrix\.zip$' {
            $status='PRESERVED'; $role='W6_INSTALL_PACKAGE'; break
        }
        '^r2_w6_preflight_source\.zip$' {
            $status='PRESERVED'; $role='W6_PREFLIGHT_SOURCE_SNAPSHOT'; break
        }
        '^stm32-handoff-.*\.zip$' {
            $status='PRESERVED'; $role='HANDOFF_SNAPSHOT'; break
        }
    }

    $toolingRows += [pscustomobject]@{
        FileName = $file.Name
        OriginalPath = $file.FullName
        Length = $file.Length
        SHA256 = $hash
        Status = $status
        Role = $role
    }
}

$toolingRows |
    Export-Csv `
        -LiteralPath (Join-Path $archiveRoot 'tooling-provenance.csv') `
        -NoTypeInformation `
        -Encoding UTF8

Write-Output "Top-level tempa files archived: $($toolingRows.Count)"

Write-Output "`n=== RECORD FULL TEMPA RECURSIVE MANIFEST ==="

$tempaManifestRows = @()

foreach ($file in @(
    Get-ChildItem `
        -LiteralPath $tempa `
        -File `
        -Recurse `
        -Force |
    Sort-Object FullName
)) {
    $relative = $file.FullName.Substring($tempa.Length).TrimStart('\')

    $hash = (
        Get-FileHash `
            -LiteralPath $file.FullName `
            -Algorithm SHA256
    ).Hash

    $tempaManifestRows += [pscustomobject]@{
        RelativePath = $relative
        Length = $file.Length
        LastWriteTime = $file.LastWriteTime.ToString('o')
        SHA256 = $hash
        TopLevel = ($file.DirectoryName -eq $tempa)
    }
}

$tempaManifestRows |
    Export-Csv `
        -LiteralPath (Join-Path $archiveRoot 'tempa-full-manifest.csv') `
        -NoTypeInformation `
        -Encoding UTF8

$generatedDirs = @(
    Get-ChildItem `
        -LiteralPath $tempa `
        -Directory `
        -Force |
    Sort-Object Name
)

@(
    '# Generated / expanded tempa directories'
    ''
    'These directories were present under Downloads\tempa at archival time.'
    'They are not duplicated file-by-file in Git when their source ZIP/snapshot is preserved.'
    'Every contained file is still represented in tempa-full-manifest.csv by path, size, timestamp and SHA256.'
    ''
    foreach ($dir in $generatedDirs) {
        '- ' + $dir.Name
    }
) | Set-Content `
        -LiteralPath (Join-Path $archiveRoot 'TEMPA_EXPANDED_DIRECTORIES.md') `
        -Encoding UTF8

Write-Output "Recursive tempa files inventoried: $($tempaManifestRows.Count)"

Write-Output "`n=== ARCHIVE ARCHITECTURE BASELINES ==="

$architectureNames = @(
    'Architecture_v3.2.1_Implementation_Baseline.md',
    'Architecture_v3.2.1_Implementation_Baseline (1).md',
    'Architecture_v3.2.2_Change_Notes.md',
    'Architecture_v3.2.2_Implementation_Baseline.md'
)

$architectureRows = @()

foreach ($name in $architectureNames) {
    $source = Join-Path $downloads $name

    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Architecture candidate missing: $source"
    }

    $dest = Join-Path $architectureRoot $name
    $hash = Copy-ExactFile -Source $source -Destination $dest

    $architectureRows += [pscustomobject]@{
        FileName = $name
        SHA256 = $hash
        Status = $(if ($name -eq 'Architecture_v3.2.2_Implementation_Baseline.md') {
            'AUTHORITATIVE_BASELINE'
        } elseif ($name -eq 'Architecture_v3.2.2_Change_Notes.md') {
            'AUTHORITATIVE_SUPPORT'
        } else {
            'HISTORICAL_SUPERSEDED'
        })
    }
}

$architectureRows |
    Export-Csv `
        -LiteralPath (Join-Path $architectureRoot 'architecture-manifest.csv') `
        -NoTypeInformation `
        -Encoding UTF8

Write-Output 'Architecture archive: PASS'

Write-Output "`n=== ARCHIVE INVENTORY PACKAGES ==="

$inventoryZips = @(
    Get-ChildItem `
        -LiteralPath $downloads `
        -File `
        -Filter 'r2-archive-inventory-*.zip' |
    Sort-Object Name
)

foreach ($file in $inventoryZips) {
    $null = Copy-ExactFile `
        -Source $file.FullName `
        -Destination (Join-Path $inventoryRoot $file.Name)
}

Write-Output "Inventory ZIPs archived: $($inventoryZips.Count)"

Write-Output "`n=== GENERATE W6 README / R2 README / FAILURE RECORD ==="

$readmeLines = @(
    '# R2-W6 Mandatory K Matrix Evidence'
    ''
    '## Status'
    ''
    '- Work package: R2-W6'
    '- Firmware milestone: `' + $firmwareMilestone + '`'
    '- Mandatory hardware matrix: **8 / 8 PASS**'
    '- Native W1-W6 suite: **116 / 116 PASS**'
    '- Committed-state programmed-byte regression: **12 / 12 PASS** (8 W6 cells + W5/W4/W3/R1)'
    '- R2 overall: **IN PROGRESS**'
    '- `r2-pass`: **NOT CREATED**'
    ''
    '## Scope'
    ''
    'W6 validates the mandatory K = 1 / 2 / 4 / 8 matrix using the same BufferPool, dynamic DBM, queue ownership, and controlled-drop mechanisms established by W1-W5.'
    'Each K is exercised in NORMAL and DROP mode for 96 DMA transfer-complete input events.'
    'DROP mode uses a Processing hold of K+5 block periods to exhaust the K FREE tokens and force controlled capacity drops.'
    ''
    'The W6 timing gates are:'
    ''
    '- nominal completion -> rebind/drop decision <= 57600 cycles (0.25 TB)'
    '- nominal completion -> ISR end <= 80640 cycles (0.35 TB)'
    '- final protected window <= 3600 cycles'
    ''
    '## Hardware matrix'
    ''
    '| Cell | Admit | Drop | Recoveries | Max drop streak | Max decision | Max IRQ end | Max window | ELF SHA256 | Trace SHA256 |'
    '|---|---:|---:|---:|---:|---:|---:|---:|---|---|'
)

foreach ($row in $matrixRows) {
    $readmeLines += (
        '| ' + $row.Cell +
        ' | ' + $row.Admissions +
        ' | ' + $row.CapacityDrops +
        ' | ' + $row.Recoveries +
        ' | ' + $row.MaxDropStreak +
        ' | ' + $row.MaxDecisionCycles +
        ' | ' + $row.MaxIrqExitCycles +
        ' | ' + $row.MaxFinalWindowCycles +
        ' | `' + $row.ElfSHA256 + '`' +
        ' | `' + $row.TraceSHA256 + '` |'
    )
}

$readmeLines += @(
    ''
    '## Mandatory invariants verified'
    ''
    '- NORMAL: every input event is admitted; the completed DMA-owned buffer is published READY, processed, released FREE, and later reused.'
    '- ADMIT: only the inactive MxAR is rebound; the active DMA target is unchanged; mapping epoch advances exactly once.'
    '- DROP: M0AR/M1AR are unchanged; mapping epoch does not advance; no READY descriptor is published; the dropped buffer does not enter Processing.'
    '- DMA TE/DME/FE = 0 and ADC OVR = 0 in all eight cells.'
    '- BufferPool, DMA-slot, queue, notification, and token-ledger violations = 0.'
    '- Admitted samples and canaries validate with zero errors.'
    '- Final stable ownership is K FREE + 2 DMA_OWNED, READY=0, PROCESSING=0.'
    '- Post-stop quiet-window counters do not advance.'
    ''
    '## Reproducibility'
    ''
    'Before commit, the current source reproduced all eight hardware-tested MCU programmed images byte-for-byte.'
    'After firmware milestone commit, committed-state regression reproduced all eight W6 programmed images and the W5/W4/W3/R1 known-good programmed images: 12 / 12 PASS.'
    ''
    'The whole ELF containers were not byte-identical across rebuild directories, but MCU programmed bytes were identical. No stronger claim is made.'
    ''
    '## Evidence layout'
    ''
    '- `hardware-matrix.csv` - machine-readable 8-cell result table.'
    '- `cells/` - exact hardware-tested ELF/MAP identity, flash/reset logs, RAM inspection, raw trace, CSV trace, and validator artifacts for each cell.'
    '- `regression/precommit-reproducibility/` - source-provenance and eight-cell programmed-image reproduction evidence.'
    '- `regression/committed-state/` - committed-state Native/W6/lower-layer programmed-byte regression evidence.'
    '- `../archive/` - architecture baselines, original downloaded tooling/packages, historical hardware anchors, inventory packages, and complete tempa manifest.'
    '- `FAILURES_AND_DEVIATIONS.md` - host-side tooling deviations and superseded scripts; these are not represented as firmware failures.'
    ''
    '## Boundary'
    ''
    'W6 closes the mandatory K matrix. It does **not** create `r2-pass` and does not by itself close R2.'
    'Architecture v3.2.2 still requires the final worst-permitted-control-traffic / service-margin stress acceptance before R2 can become a final PASS candidate.'
)

[System.IO.File]::WriteAllLines(
    (Join-Path $w6Root 'README.md'),
    $readmeLines,
    (New-Object System.Text.UTF8Encoding($false))
)

$failureLines = @(
    '# R2-W6 Failures and Deviations'
    ''
    'This file records engineering-significant host/tooling deviations observed while executing W6.'
    'They are preserved for provenance and are not rewritten as firmware failures.'
    ''
    '## MSVC environment bootstrap'
    ''
    '- Initial Native verification failed before tests because `verify_w6.ps1` invoked `VsDevCmd.bat` inside an already initialized Visual Studio developer environment, producing `input line too long` / command syntax failure.'
    '- Direct `cl.exe` compilation proved the existing MSVC environment was valid.'
    '- The verifier was minimally changed to reuse an existing x64 developer environment when `cl.exe` and VSCMD x64 markers are already present; it falls back to VsDevCmd only when needed.'
    '- Final verifier SHA256: `BF632BB6C59998C5DBF00D36B734154AA261205847329728B13DDCC838C1AA42`.'
    ''
    '## Diagnostic-script mistakes'
    ''
    '- Early `cl.exe /Bv` probes were written incorrectly and produced host-side errors; they did not change firmware or MCU state.'
    '- The first K=1 DROP trace validation used C-style integer suffixes such as `1U` in Windows PowerShell 5.1. PowerShell rejected the script at parse time. The run was not repeated; trace export was retried from the existing RAM result using corrected tooling.'
    '- The first generic run-summary tool read per-buffer arrays beyond their valid K+2 length at K=2, causing a false host-side accounting failure. The underlying K=2 hardware summary was internally consistent for buffers 0..3. `r2_w6_run_summary_v2.ps1` fixed the boundary and later cells used it.'
    '- Several pre-commit wrapper revisions failed due host-side PowerShell argument/output parsing (`array splatting`, `Write-Host` marker capture, and `$Label:` parser syntax). These failures occurred before MCU operations and are preserved as superseded tools.'
    ''
    '## Source provenance deviation'
    ''
    '- The handoff snapshot copy of `r2_w6_matrix.c` had SHA256 `2BD42D4B477D99030EAACF892354EDCBB13EADC9B4A667A963945329D705795D`.'
    '- The final hardware-tested/current source has SHA256 `7C0F7DEFFCC885965A926B7A3737F684E1D7D951C4D44A96FF33B60CF8930B4F`.'
    '- Because the file was untracked during development, Git could not identify the exact edit moment.'
    '- This uncertainty was closed by pre-commit reconstruction: current source reproduced the programmed bytes of all eight hardware-tested W6 images (8/8 PASS).'
    '- The committed firmware milestone then passed committed-state programmed-byte regression against all eight W6 cells plus W5/W4/W3/R1 (12/12 PASS).'
    ''
    '## Evidence integrity rule'
    ''
    'No failed host wrapper output is used as positive hardware evidence. Formal claims are tied to the final passing hardware summaries/traces, exact ELF/programmed-image identities, and the committed-state regression.'
)

[System.IO.File]::WriteAllLines(
    (Join-Path $w6Root 'FAILURES_AND_DEVIATIONS.md'),
    $failureLines,
    (New-Object System.Text.UTF8Encoding($false))
)

$w1 = (git rev-parse 7e1ab05).Trim()
$w2 = (git rev-parse a966cd3).Trim()
$w3 = (git rev-parse f5d0f08).Trim()
$w4 = (git rev-parse 7ae9a56).Trim()
$w5 = (git rev-parse 2752c0e).Trim()
$w6 = (git rev-parse $firmwareMilestone).Trim()

$r2Lines = @(
    '# R2 - Dynamic K+2 DMA Ownership and Capacity Control'
    ''
    '## Status'
    ''
    '- Architecture baseline: **v3.2.2**'
    '- R2 overall: **IN PROGRESS**'
    '- Latest firmware milestone: `' + $w6 + '` (R2-W6 mandatory K matrix)'
    '- `r2-pass`: **NOT CREATED**'
    ''
    '## Work-package chronology'
    ''
    '| Package | Firmware milestone | Purpose | Status |'
    '|---|---|---|---|'
    '| W1 | `' + $w1 + '` | BufferPool ownership core and K+2 active topology | CLOSED / KNOWN-GOOD |'
    '| W2 | `' + $w2 + '` | DMA M0/M1 slot abstraction, CT decoding, planned rebind and mapping epoch | CLOSED / KNOWN-GOOD |'
    '| W3 | `' + $w3 + '` | Bounded real dynamic inactive-slot DMA rebinding | CLOSED / KNOWN-GOOD |'
    '| W4 | `' + $w4 + '` | DMA -> READY -> PROCESSING -> FREE -> DMA ownership round trip | CLOSED / KNOWN-GOOD |'
    '| W5 | `' + $w5 + '` | Directed FreeBufferQueue exhaustion and controlled capacity drop | CLOSED / KNOWN-GOOD |'
    '| W6 | `' + $w6 + '` | Mandatory K=1/2/4/8 NORMAL/DROP hardware matrix | CLOSED / KNOWN-GOOD |'
    ''
    '## What R2 has established'
    ''
    'Starting from the R1 fixed double-buffer acquisition baseline, R2 established a static K+2 physical buffer pool with explicit ownership, a CT-aware M0/M1 DMA slot model, real dynamic rebinding of the inactive DMA target, a complete queue/Processing/free-buffer ownership round trip, and a controlled capacity-drop path when no FREE token is available.'
    ''
    'W6 then demonstrated the same mechanism across K=1,2,4,8 in both NORMAL and DROP mode on real STM32F446RE hardware. All eight mandatory hardware cells passed their accounting, ownership, DMA/ADC, sample/canary, timing, final-state, and trace invariants.'
    ''
    '## Evidence'
    ''
    '- W3: `docs/evidence/r2/w3/`'
    '- W4: `docs/evidence/r2/w4/`'
    '- W5: `docs/evidence/r2/w5/`'
    '- W6: `docs/evidence/r2/w6/`'
    '- Provenance/archive: `docs/evidence/r2/archive/`'
    ''
    'W1 and W2 do not have standalone hardware evidence packages because they are lower-layer software abstractions; their behavior is exercised by later Native and hardware packages. Their implementation commits remain part of the Git history.'
    ''
    '## Remaining R2 acceptance work'
    ''
    'W6 closes the mandatory K matrix but not the whole R2 gate.'
    'Architecture v3.2.2 still requires final worst-permitted-control-traffic / service-margin stress acceptance, including approved control-traffic interference in the DBM service budget.'
    'Only after the remaining R2 acceptance work, final evidence review, and Principal acceptance may `r2-pass` be created.'
)

[System.IO.File]::WriteAllLines(
    (Join-Path $r2Root 'README.md'),
    $r2Lines,
    (New-Object System.Text.UTF8Encoding($false))
)

$archiveReadme = @(
    '# R2 Provenance Archive'
    ''
    'This directory preserves R2 provenance without duplicating every expanded temporary tree as normal Git files.'
    ''
    '## Policy'
    ''
    '- Every top-level file present in `Downloads\tempa` at archival time is copied verbatim into `tooling/` and committed.'
    '- `tempa-full-manifest.csv` records every recursively present tempa file by relative path, size, timestamp and SHA256.'
    '- Expanded installer/handoff directories are not duplicated file-by-file when their source ZIP/snapshot is already preserved; their complete contents remain represented in the recursive manifest.'
    '- Architecture v3.2.2 is the authoritative baseline. v3.2.1 copies are retained only as historical/superseded records.'
    '- Historical hardware anchors preserve exact ELF/MAP/summary identities used for R1/W3/W4/W5 programmed-byte regressions.'
    '- Superseded or failed host scripts are retained and classified; they are not treated as positive hardware evidence.'
)

[System.IO.File]::WriteAllLines(
    (Join-Path $archiveRoot 'README.md'),
    $archiveReadme,
    (New-Object System.Text.UTF8Encoding($false))
)

Write-Output 'Generated R2/W6 documentation: PASS'

Write-Output "`n=== UPDATE PROJECT STATUS ==="

$statusPath = Join-Path $repo 'docs\status.md'
$statusText = [System.IO.File]::ReadAllText($statusPath)

$statusText = [regex]::Replace(
    $statusText,
    '(?m)^\| R2 \|.*$',
    '| R2 | IN PROGRESS | `bbc3bf6` | W1-W6 implemented; W6 mandatory K matrix 8/8 hardware PASS; final control-traffic/service-margin acceptance pending |'
)

$statusText = $statusText.Replace(
    'R2 is authorized and remains NOT STARTED.',
    'R2 is IN PROGRESS. W1-W6 are implemented; R2-W6 mandatory K matrix is 8/8 hardware PASS at firmware milestone `bbc3bf6821c4f6e4dc73beabd5685b8b0828205c`. Final control-traffic/service-margin acceptance remains pending; `r2-pass` is not created.'
)

[System.IO.File]::WriteAllText(
    $statusPath,
    $statusText,
    (New-Object System.Text.UTF8Encoding($false))
)

Write-Output 'docs/status.md updated'

Write-Output "`n=== UPDATE EVIDENCE INDEX ==="

$indexPath = Join-Path $repo 'docs\evidence\index.md'
$indexText = [System.IO.File]::ReadAllText($indexPath)

$indexText = $indexText.Replace(
    'R2 is authorized and remains NOT STARTED.',
    'R2 is IN PROGRESS. W1-W6 are implemented; W6 mandatory K matrix is 8/8 hardware PASS. Final control-traffic/service-margin acceptance remains pending.'
)

if ($indexText -notmatch '(?m)^## R2 ') {
    $marker = '## Existing engineering / project logs'

    if (-not $indexText.Contains($marker)) {
        throw 'Could not find evidence-index insertion marker.'
    }

    $r2IndexSection = @'
## R2 - Dynamic K+2 DMA Ownership and Capacity Control

**Status:** IN PROGRESS  
**Latest firmware milestone:** `bbc3bf6821c4f6e4dc73beabd5685b8b0828205c`

W1-W6 establish the K+2 BufferPool ownership model, DMA M0/M1 slot abstraction, real dynamic inactive-slot rebinding, DMA/Processing/FREE ownership round trip, controlled capacity drop, and the mandatory K=1/2/4/8 NORMAL/DROP hardware matrix.

**W6 result:** 8 / 8 mandatory hardware cells PASS. Native W1-W6: 116 / 116 PASS. Committed-state programmed-byte regression: 12 / 12 PASS across W6 hardware images plus W5/W4/W3/R1 anchors.

**Primary evidence:**
- [`docs/evidence/r2/README.md`](r2/README.md)
- [`R2-W3 evidence`](r2/w3/README.md)
- [`R2-W4 evidence`](r2/w4/README.md)
- [`R2-W5 evidence`](r2/w5/README.md)
- [`R2-W6 evidence`](r2/w6/README.md)
- [`R2 provenance archive`](r2/archive/README.md)

R2 is not yet a final PASS. Architecture v3.2.2 final worst-permitted-control-traffic / service-margin acceptance remains pending, and `r2-pass` has not been created.

'@

    $indexText = $indexText.Replace(
        $marker,
        $r2IndexSection + "`r`n" + $marker
    )
}

[System.IO.File]::WriteAllText(
    $indexPath,
    $indexText,
    (New-Object System.Text.UTF8Encoding($false))
)

Write-Output 'docs/evidence/index.md updated'

Write-Output "`n=== GENERATE EVIDENCE MANIFESTS ==="

$w6Manifest = Join-Path $w6Root 'w6-evidence-manifest.sha256'
Write-Manifest -Root $w6Root -Manifest $w6Manifest

$archiveManifest = Join-Path $archiveRoot 'archive-manifest.sha256'
Write-Manifest -Root $archiveRoot -Manifest $archiveManifest

$r2Manifest = Join-Path $r2Root 'r2-evidence-manifest.sha256'
Write-Manifest -Root $r2Root -Manifest $r2Manifest

Write-Output 'Manifests generated: PASS'

Write-Output "`n=== FINAL CONSOLIDATION AUDIT ==="

git diff --check

if ($LASTEXITCODE -ne 0) {
    throw 'git diff --check failed after evidence consolidation.'
}

$status = @(
    git status --short --branch
)

$status | ForEach-Object { Write-Output $_ }

$w6CellCount = @(
    Get-ChildItem -LiteralPath $cellsRoot -Directory
).Count

$toolingCount = @(
    Get-ChildItem -LiteralPath $toolingRoot -File
).Count

$architectureCount = @(
    Get-ChildItem -LiteralPath $architectureRoot -File |
    Where-Object { $_.Name -ne 'architecture-manifest.csv' }
).Count

Write-Output ''
Write-Output '=== R2 EVIDENCE CONSOLIDATION SUMMARY ==='
Write-Output "Firmware milestone:             $firmwareMilestone"
Write-Output "W6 hardware cell archives:      $w6CellCount / 8"
Write-Output "Tempa top-level files archived: $toolingCount"
Write-Output "Architecture source files:      $architectureCount"
Write-Output "Recursive tempa manifest rows:  $($tempaManifestRows.Count)"
Write-Output "Inventory ZIPs archived:        $($inventoryZips.Count)"
Write-Output 'W6 matrix CSV:                  CREATED'
Write-Output 'W6 failures/deviations:         CREATED'
Write-Output 'R2 overview:                    CREATED'
Write-Output 'Status/evidence index:          UPDATED'
Write-Output 'SHA256 manifests:               CREATED'
Write-Output ''
Write-Output 'No Git add/commit/push/tag performed.'
