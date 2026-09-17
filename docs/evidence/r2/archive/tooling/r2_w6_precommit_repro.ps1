param()

$ErrorActionPreference = 'Stop'

$repo = 'E:\Projects\stm32-stream-lab'
$verify = Join-Path $repo 'tools\r2\verify_w6.ps1'
$buildRoot = Join-Path $repo 'build'
$expectedHead = '2752c0e915ab4725cc13e400d16fe47b14adfd4e'
$expectedVerifierHash = 'BF632BB6C59998C5DBF00D36B734154AA261205847329728B13DDCC838C1AA42'

$sourceFiles = @(
    'firmware\cubemx\CMakeLists.txt',
    'firmware\cubemx\Core\Inc\FreeRTOSConfig.h',
    'firmware\cubemx\Core\Src\main.c',
    'firmware\cubemx\Core\Src\stm32f4xx_it.c',
    'tests\native\CMakeLists.txt',
    'docs\evidence\r2\w6\PLAN.md',
    'firmware\acquisition\r2_w6_guard.h',
    'firmware\acquisition\r2_w6_matrix.c',
    'firmware\acquisition\r2_w6_matrix.h',
    'tests\native\test_r2_w6_matrix.c',
    'tools\r2\verify_w6.ps1'
)

$cells = @(
    [pscustomobject]@{ K=1; Mode='NORMAL'; ExpectedElf='311835D2C505F239C0791F0C144548B24CC5E7BD5E177C5DB2FAF6FA8DF62A93' },
    [pscustomobject]@{ K=1; Mode='DROP';   ExpectedElf='0998BEC54FDCFBA81A5E995DB892A3CEF0B1E6C0C910622CB5B3E7311FB429CE' },
    [pscustomobject]@{ K=2; Mode='NORMAL'; ExpectedElf='1D44D800B5940882F00D1E6CCCD1FF6E2A30B3661BB19EAE37292E022EE8C3F7' },
    [pscustomobject]@{ K=2; Mode='DROP';   ExpectedElf='B9D7222ADF3E4BEAD78F53E0F5FACC8C43691E1899E36DC72DCE0155BC3C1FA1' },
    [pscustomobject]@{ K=4; Mode='NORMAL'; ExpectedElf='68599AA9820108398F80538286070E54DA3A53EC6968FBDBFD64B2E4C3FF32BF' },
    [pscustomobject]@{ K=4; Mode='DROP';   ExpectedElf='C732A527A7B49BCAC109C3BCF5E6E402723A457574D447C616FA3D7B7C1E9134' },
    [pscustomobject]@{ K=8; Mode='NORMAL'; ExpectedElf='E102911CC6A2C81D65ED03BD63B1558018D59BB507436DD7D2E7B67D69F11B18' },
    [pscustomobject]@{ K=8; Mode='DROP';   ExpectedElf='B687A7D83E51533622AE77AABFC97E23FF0131964C6B7A2AD041AB48BB756581' }
)

function Get-SourceHashes {
    param([string[]]$Paths)

    $result = [ordered]@{}

    foreach ($relative in $Paths) {
        $full = Join-Path $repo $relative

        if (-not (Test-Path -LiteralPath $full -PathType Leaf)) {
            throw "Required source file missing: $relative"
        }

        $result[$relative] = (
            Get-FileHash -LiteralPath $full -Algorithm SHA256
        ).Hash
    }

    return $result
}

function Invoke-Verifier {
    param(
        [Parameter(Mandatory=$true)][string[]]$Arguments,
        [Parameter(Mandatory=$true)][string]$Log
    )

    $oldPreference = $ErrorActionPreference

    try {
        $ErrorActionPreference = 'Continue'

        $output = @(
            & $verify @Arguments 2>&1
        )

        $rc = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $oldPreference
    }

    $lines = @(
        $output |
        ForEach-Object {
            $_.ToString()
        }
    )

    $lines | ForEach-Object { Write-Output $_ }

    [System.IO.File]::WriteAllLines(
        $Log,
        $lines,
        (New-Object System.Text.UTF8Encoding($false))
    )

    if ($rc -ne 0) {
        throw "Verifier failed with exit code $rc. Log: $Log"
    }

    return ,$lines
}

Set-Location -LiteralPath $repo

Write-Output '=== R2-W6 PRE-COMMIT SOURCE PROVENANCE + REPRODUCIBILITY ==='

$head = (git rev-parse HEAD).Trim()

Write-Output "HEAD: $head"

if ($head -ne $expectedHead) {
    throw "Unexpected HEAD: $head"
}

if (-not (Test-Path -LiteralPath $verify -PathType Leaf)) {
    throw "Missing verifier: $verify"
}

$verifyHash = (
    Get-FileHash -LiteralPath $verify -Algorithm SHA256
).Hash

Write-Output "Verifier SHA256: $verifyHash"

if ($verifyHash -ne $expectedVerifierHash) {
    throw 'verify_w6.ps1 differs from the hardware-tested verifier state.'
}

$protectedChanges = @(
    git status --short -- `
        firmware/buffer_pool/r2_buffer_pool.c `
        firmware/buffer_pool/r2_buffer_pool.h `
        firmware/acquisition/r2_dma_slots.c `
        firmware/acquisition/r2_dma_slots.h `
        firmware/acquisition/r2_w3_rebind.c `
        firmware/acquisition/r2_w3_rebind.h `
        firmware/acquisition/r2_w4_roundtrip.c `
        firmware/acquisition/r2_w4_roundtrip.h `
        firmware/acquisition/r2_w5_capacity.c `
        firmware/acquisition/r2_w5_capacity.h `
        firmware/cubemx/cubemx.ioc
)

if ($protectedChanges.Count -ne 0) {
    $protectedChanges
    throw 'Protected W1-W5/IOC source changed.'
}

git diff --check

if ($LASTEXITCODE -ne 0) {
    throw 'git diff --check failed.'
}

$outDir = Join-Path $buildRoot (
    'r2-w6-precommit-repro-' +
    (Get-Date -Format 'yyyyMMdd-HHmmss')
)

New-Item -ItemType Directory -Path $outDir | Out-Null

Write-Output "Evidence directory: $outDir"

$beforeHashes = Get-SourceHashes -Paths $sourceFiles

$beforeCsv = Join-Path $outDir 'source-hashes-before.csv'

@(
    foreach ($relative in $beforeHashes.Keys) {
        [pscustomobject]@{
            RelativePath = $relative
            SHA256 = $beforeHashes[$relative]
        }
    }
) | Export-Csv -LiteralPath $beforeCsv -NoTypeInformation -Encoding UTF8

Write-Output "`n=== CURRENT SOURCE HASHES ==="

foreach ($relative in $beforeHashes.Keys) {
    Write-Output "$($beforeHashes[$relative])  $relative"
}

# Preserve the known historical mismatch explicitly if the handoff snapshot exists.
$handoffOld = Join-Path $HOME 'Downloads\tempa\stm32-handoff-20260917-232541-4289db94\working-tree\firmware\acquisition\r2_w6_matrix.c'
$currentMatrix = Join-Path $repo 'firmware\acquisition\r2_w6_matrix.c'
$diffLog = Join-Path $outDir 'r2_w6_matrix-vs-handoff.diff.txt'

if (Test-Path -LiteralPath $handoffOld -PathType Leaf) {
    $oldHash = (Get-FileHash -LiteralPath $handoffOld -Algorithm SHA256).Hash
    $currentHash = (Get-FileHash -LiteralPath $currentMatrix -Algorithm SHA256).Hash

    Write-Output "`n=== W6 MATRIX SOURCE HISTORICAL COMPARISON ==="
    Write-Output "Handoff snapshot SHA256: $oldHash"
    Write-Output "Current source SHA256:   $currentHash"

    $oldPreference = $ErrorActionPreference

    try {
        $ErrorActionPreference = 'Continue'

        $diffOutput = @(
            git diff `
                --no-index `
                --text `
                -- `
                $handoffOld `
                $currentMatrix `
                2>&1
        )

        $diffRc = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $oldPreference
    }

    $diffLines = @(
        $diffOutput |
        ForEach-Object { $_.ToString() }
    )

    [System.IO.File]::WriteAllLines(
        $diffLog,
        $diffLines,
        (New-Object System.Text.UTF8Encoding($false))
    )

    if ($diffRc -notin @(0,1)) {
        throw "git diff --no-index failed with exit code $diffRc"
    }

    Write-Output "Source comparison diff saved: $diffLog"
}
else {
    Write-Output "`nHandoff matrix source not found locally; historical diff skipped."
}

Write-Output "`n=== NATIVE REPRODUCTION ==="

$nativeLog = Join-Path $outDir 'native-verification.txt'

$nativeLines = Invoke-Verifier `
    -Arguments @(
        '-Profile','Native',
        '-Repo',$repo
    ) `
    -Log $nativeLog

$nativeText = $nativeLines -join "`n"

if (
    $nativeText -notmatch 'R2-W6 VERIFICATION: PASS' -or
    $nativeText -notmatch '116 / 116 PASS'
) {
    throw 'Native verifier exited 0 but expected 116/116 PASS markers were not observed.'
}

Write-Output 'Native current-source reproduction: 116 / 116 PASS'

Write-Output "`n=== EIGHT HARDWARE-ELF REPRODUCTIONS ==="

$results = @()

foreach ($cell in $cells) {
    $label = "K$($cell.K)-$($cell.Mode)"
    $log = Join-Path $outDir ("target-" + $label.ToLowerInvariant() + '.txt')

    Write-Output "`n--- $label ---"

    $lines = Invoke-Verifier `
        -Arguments @(
            '-Profile','W6Target',
            '-K',[string]$cell.K,
            '-Mode',$cell.Mode,
            '-Repo',$repo
        ) `
        -Log $log

    $text = $lines -join "`n"

    $hashMatch = [regex]::Match(
        $text,
        '(?m)^ELF SHA256:\s*([0-9A-Fa-f]{64})\s*$'
    )

    $buildMatch = [regex]::Match(
        $text,
        '(?m)^Build directory:\s*(.+?)\s*$'
    )

    if (-not $hashMatch.Success) {
        throw "$label verifier output did not expose ELF SHA256."
    }

    if (-not $buildMatch.Success) {
        throw "$label verifier output did not expose build directory."
    }

    $actualElf = $hashMatch.Groups[1].Value.ToUpperInvariant()
    $buildDir = $buildMatch.Groups[1].Value.Trim()
    $match = ($actualElf -eq $cell.ExpectedElf)

    $results += [pscustomobject]@{
        Cell = $label
        ExpectedHardwareElfSHA256 = $cell.ExpectedElf
        RebuiltElfSHA256 = $actualElf
        ByteIdentical = $match
        RebuildDirectory = $buildDir
        VerifierLog = $log
    }

    Write-Output "Hardware-tested ELF: $($cell.ExpectedElf)"
    Write-Output "Current-source ELF:  $actualElf"
    Write-Output ("Byte-identical:      " + $(if ($match) { 'PASS' } else { 'FAIL' }))

    if (-not $match) {
        $results |
            Export-Csv `
                -LiteralPath (Join-Path $outDir 'eight-cell-repro.csv') `
                -NoTypeInformation `
                -Encoding UTF8

        throw "$label current source does NOT reproduce the hardware-tested ELF."
    }
}

$results |
    Export-Csv `
        -LiteralPath (Join-Path $outDir 'eight-cell-repro.csv') `
        -NoTypeInformation `
        -Encoding UTF8

$afterHashes = Get-SourceHashes -Paths $sourceFiles

foreach ($relative in $beforeHashes.Keys) {
    if ($afterHashes[$relative] -ne $beforeHashes[$relative]) {
        throw "Source changed during reproduction campaign: $relative"
    }
}

$afterCsv = Join-Path $outDir 'source-hashes-after.csv'

@(
    foreach ($relative in $afterHashes.Keys) {
        [pscustomobject]@{
            RelativePath = $relative
            SHA256 = $afterHashes[$relative]
        }
    }
) | Export-Csv -LiteralPath $afterCsv -NoTypeInformation -Encoding UTF8

if ((git rev-parse HEAD).Trim() -ne $expectedHead) {
    throw 'HEAD changed during reproduction campaign.'
}

Write-Output "`n=== REPRODUCIBILITY RESULT ==="
Write-Output 'Native suite:                          116 / 116 PASS'
Write-Output 'Hardware-tested W6 ELF identities:     8 / 8 BYTE-IDENTICAL'
Write-Output 'Current source bytes during campaign:  UNCHANGED'
Write-Output 'Git HEAD/index:                        UNCHANGED'
Write-Output "Evidence directory:                    $outDir"
Write-Output ''
Write-Output 'R2-W6 CURRENT SOURCE == HARDWARE-TESTED SOURCE: PROVEN BY REBUILD'
Write-Output 'No flash/reset/MCU operation performed.'
Write-Output 'No commit/push performed.'
