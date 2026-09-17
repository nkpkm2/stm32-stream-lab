param()

$ErrorActionPreference = 'Stop'

$repo = 'E:\Projects\stm32-stream-lab'
$verify = Join-Path $repo 'tools\r2\verify_w6.ps1'
$buildRoot = Join-Path $repo 'build'

$expectedHead = '2752c0e915ab4725cc13e400d16fe47b14adfd4e'
$expectedVerifierHash = 'BF632BB6C59998C5DBF00D36B734154AA261205847329728B13DDCC838C1AA42'

$objcopy =
    'E:\DevTools\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi\bin\arm-none-eabi-objcopy.exe'

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
    [pscustomobject]@{
        K=1
        Mode='NORMAL'
        HardwareBuild='r2-w6-w6target-k1-normal-16185105'
        HardwareElf='311835D2C505F239C0791F0C144548B24CC5E7BD5E177C5DB2FAF6FA8DF62A93'
    },
    [pscustomobject]@{
        K=1
        Mode='DROP'
        HardwareBuild='r2-w6-w6target-k1-drop-eeb059ed'
        HardwareElf='0998BEC54FDCFBA81A5E995DB892A3CEF0B1E6C0C910622CB5B3E7311FB429CE'
    },
    [pscustomobject]@{
        K=2
        Mode='NORMAL'
        HardwareBuild='r2-w6-w6target-k2-normal-1518c429'
        HardwareElf='1D44D800B5940882F00D1E6CCCD1FF6E2A30B3661BB19EAE37292E022EE8C3F7'
    },
    [pscustomobject]@{
        K=2
        Mode='DROP'
        HardwareBuild='r2-w6-w6target-k2-drop-e9491d79'
        HardwareElf='B9D7222ADF3E4BEAD78F53E0F5FACC8C43691E1899E36DC72DCE0155BC3C1FA1'
    },
    [pscustomobject]@{
        K=4
        Mode='NORMAL'
        HardwareBuild='r2-w6-w6target-k4-normal-77d5741f'
        HardwareElf='68599AA9820108398F80538286070E54DA3A53EC6968FBDBFD64B2E4C3FF32BF'
    },
    [pscustomobject]@{
        K=4
        Mode='DROP'
        HardwareBuild='r2-w6-w6target-k4-drop-64e98478'
        HardwareElf='C732A527A7B49BCAC109C3BCF5E6E402723A457574D447C616FA3D7B7C1E9134'
    },
    [pscustomobject]@{
        K=8
        Mode='NORMAL'
        HardwareBuild='r2-w6-w6target-k8-normal-a6b96a0b'
        HardwareElf='E102911CC6A2C81D65ED03BD63B1558018D59BB507436DD7D2E7B67D69F11B18'
    },
    [pscustomobject]@{
        K=8
        Mode='DROP'
        HardwareBuild='r2-w6-w6target-k8-drop-7a95200b'
        HardwareElf='B687A7D83E51533622AE77AABFC97E23FF0131964C6B7A2AD041AB48BB756581'
    }
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

function Invoke-NativeVerifier {
    param([Parameter(Mandatory=$true)][string]$Log)

    $oldPreference = $ErrorActionPreference

    try {
        $ErrorActionPreference = 'Continue'

        $raw = @(
            & $verify `
                -Profile Native `
                -Repo $repo `
                2>&1
        )

        $rc = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $oldPreference
    }

    $lines = @(
        $raw |
        ForEach-Object { $_.ToString() }
    )

    $lines | ForEach-Object { Write-Output $_ }

    [System.IO.File]::WriteAllLines(
        $Log,
        $lines,
        (New-Object System.Text.UTF8Encoding($false))
    )

    if ($rc -ne 0) {
        throw "Native verifier failed with exit code $rc. Log: $Log"
    }

    $text = $lines -join "`n"

    if (
        $text -notmatch 'R2-W6 VERIFICATION: PASS' -or
        $text -notmatch '116 / 116 PASS'
    ) {
        throw 'Native verifier exited 0 but 116/116 PASS markers were not observed.'
    }

    return $lines
}

function Invoke-TargetVerifier {
    param(
        [Parameter(Mandatory=$true)][int]$K,
        [Parameter(Mandatory=$true)][ValidateSet('NORMAL','DROP')][string]$Mode,
        [Parameter(Mandatory=$true)][string]$Log
    )

    $oldPreference = $ErrorActionPreference

    try {
        $ErrorActionPreference = 'Continue'

        $raw = @(
            & $verify `
                -Profile W6Target `
                -K $K `
                -Mode $Mode `
                -Repo $repo `
                2>&1
        )

        $rc = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $oldPreference
    }

    $lines = @(
        $raw |
        ForEach-Object { $_.ToString() }
    )

    $lines | ForEach-Object { Write-Output $_ }

    [System.IO.File]::WriteAllLines(
        $Log,
        $lines,
        (New-Object System.Text.UTF8Encoding($false))
    )

    if ($rc -ne 0) {
        throw "Target verifier K=$K $Mode failed with exit code $rc. Log: $Log"
    }

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
        throw "Target verifier K=$K $Mode did not expose ELF SHA256."
    }

    if (-not $buildMatch.Success) {
        throw "Target verifier K=$K $Mode did not expose build directory."
    }

    return [pscustomobject]@{
        ElfSHA256 = $hashMatch.Groups[1].Value.ToUpperInvariant()
        BuildDirectory = $buildMatch.Groups[1].Value.Trim()
        Lines = $lines
    }
}

function Get-ProgrammedBytesHash {
    param(
        [Parameter(Mandatory=$true)][string]$Elf,
        [Parameter(Mandatory=$true)][string]$OutputBin
    )

    if (-not (Test-Path -LiteralPath $Elf -PathType Leaf)) {
        throw "ELF missing: $Elf"
    }

    if (Test-Path -LiteralPath $OutputBin) {
        Remove-Item -LiteralPath $OutputBin -Force
    }

    $oldPreference = $ErrorActionPreference

    try {
        $ErrorActionPreference = 'Continue'

        $output = @(
            & $objcopy `
                -O binary `
                $Elf `
                $OutputBin `
                2>&1
        )

        $rc = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $oldPreference
    }

    if ($rc -ne 0) {
        $output | ForEach-Object { Write-Output $_.ToString() }
        throw "objcopy failed for $Elf with exit code $rc"
    }

    if (-not (Test-Path -LiteralPath $OutputBin -PathType Leaf)) {
        throw "objcopy returned success but binary missing: $OutputBin"
    }

    return (
        Get-FileHash -LiteralPath $OutputBin -Algorithm SHA256
    ).Hash
}

Set-Location -LiteralPath $repo

Write-Output '=== R2-W6 PRE-COMMIT REPRODUCIBILITY V2 ==='

$head = (git rev-parse HEAD).Trim()
Write-Output "HEAD: $head"

if ($head -ne $expectedHead) {
    throw "Unexpected HEAD: $head"
}

if (-not (Test-Path -LiteralPath $verify -PathType Leaf)) {
    throw "Missing verifier: $verify"
}

if (-not (Test-Path -LiteralPath $objcopy -PathType Leaf)) {
    throw "Missing objcopy: $objcopy"
}

$verifyHash = (
    Get-FileHash -LiteralPath $verify -Algorithm SHA256
).Hash

Write-Output "Verifier SHA256: $verifyHash"

if ($verifyHash -ne $expectedVerifierHash) {
    throw 'verify_w6.ps1 differs from hardware-tested verifier state.'
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

$gitDir = (git rev-parse --git-dir).Trim()
$indexPath = Join-Path $repo $gitDir
$indexPath = Join-Path $indexPath 'index'

$indexHashBefore = (
    Get-FileHash -LiteralPath $indexPath -Algorithm SHA256
).Hash

$outDir = Join-Path $buildRoot (
    'r2-w6-precommit-repro-v2-' +
    (Get-Date -Format 'yyyyMMdd-HHmmss')
)

New-Item -ItemType Directory -Path $outDir | Out-Null

Write-Output "Evidence directory: $outDir"

$beforeHashes = Get-SourceHashes -Paths $sourceFiles

@(
    foreach ($relative in $beforeHashes.Keys) {
        [pscustomobject]@{
            RelativePath = $relative
            SHA256 = $beforeHashes[$relative]
        }
    }
) | Export-Csv `
        -LiteralPath (Join-Path $outDir 'source-hashes-before.csv') `
        -NoTypeInformation `
        -Encoding UTF8

Write-Output "`n=== CURRENT SOURCE HASHES ==="

foreach ($relative in $beforeHashes.Keys) {
    Write-Output "$($beforeHashes[$relative])  $relative"
}

Write-Output "`n=== HISTORICAL r2_w6_matrix.c COMPARISON ==="

$handoffOld = Join-Path $HOME (
    'Downloads\tempa\stm32-handoff-20260917-232541-4289db94\' +
    'working-tree\firmware\acquisition\r2_w6_matrix.c'
)

$currentMatrix = Join-Path $repo 'firmware\acquisition\r2_w6_matrix.c'
$diffLog = Join-Path $outDir 'r2_w6_matrix-vs-handoff.diff.txt'

if (Test-Path -LiteralPath $handoffOld -PathType Leaf) {
    $oldHash = (
        Get-FileHash -LiteralPath $handoffOld -Algorithm SHA256
    ).Hash

    $currentHash = (
        Get-FileHash -LiteralPath $currentMatrix -Algorithm SHA256
    ).Hash

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

    Write-Output "Diff lines: $($diffLines.Count)"
    Write-Output "Diff saved: $diffLog"

    if ($diffLines.Count -le 200) {
        Write-Output "`n--- BEGIN MATRIX SOURCE DIFF ---"
        $diffLines | ForEach-Object { Write-Output $_ }
        Write-Output '--- END MATRIX SOURCE DIFF ---'
    }
    else {
        Write-Output 'Diff exceeds 200 lines; preserved in evidence file.'
    }
}
else {
    Write-Output 'Local handoff snapshot source not found; historical diff skipped.'
}

Write-Output "`n=== NATIVE REPRODUCTION ==="

$nativeLog = Join-Path $outDir 'native-verification.txt'

$null = Invoke-NativeVerifier -Log $nativeLog

Write-Output 'Native current-source reproduction: 116 / 116 PASS'

Write-Output "`n=== EIGHT TARGET REPRODUCTIONS ==="

$results = @()

foreach ($cell in $cells) {
    $label = "K$($cell.K)-$($cell.Mode)"
    $targetLog = Join-Path $outDir ("target-" + $label.ToLowerInvariant() + '.txt')

    Write-Output "`n--- $label ---"

    $result = Invoke-TargetVerifier `
        -K $cell.K `
        -Mode $cell.Mode `
        -Log $targetLog

    $rebuiltElf = $result.ElfSHA256
    $rebuiltBuild = $result.BuildDirectory

    $hardwareBuildDir = Join-Path $buildRoot $cell.HardwareBuild
    $hardwareElfPath = Join-Path $hardwareBuildDir 'cubemx.elf'
    $rebuiltElfPath = Join-Path $rebuiltBuild 'cubemx.elf'

    if (-not (Test-Path -LiteralPath $hardwareElfPath -PathType Leaf)) {
        throw "Original hardware-tested ELF missing: $hardwareElfPath"
    }

    $originalElfHash = (
        Get-FileHash -LiteralPath $hardwareElfPath -Algorithm SHA256
    ).Hash

    if ($originalElfHash -ne $cell.HardwareElf) {
        throw "$label original hardware ELF no longer matches recorded SHA256."
    }

    $elfIdentical = ($rebuiltElf -eq $cell.HardwareElf)

    $originalBin = Join-Path $outDir ("original-" + $label.ToLowerInvariant() + '.bin')
    $rebuiltBin = Join-Path $outDir ("rebuilt-" + $label.ToLowerInvariant() + '.bin')

    $originalProgrammedHash = Get-ProgrammedBytesHash `
        -Elf $hardwareElfPath `
        -OutputBin $originalBin

    $rebuiltProgrammedHash = Get-ProgrammedBytesHash `
        -Elf $rebuiltElfPath `
        -OutputBin $rebuiltBin

    $programmedIdentical = (
        $originalProgrammedHash -eq $rebuiltProgrammedHash
    )

    Write-Output "Hardware-tested ELF SHA256:  $($cell.HardwareElf)"
    Write-Output "Rebuilt ELF SHA256:          $rebuiltElf"
    Write-Output ("ELF byte-identical:           " + $(if ($elfIdentical) { 'PASS' } else { 'NO' }))
    Write-Output "Hardware programmed SHA256:  $originalProgrammedHash"
    Write-Output "Rebuilt programmed SHA256:   $rebuiltProgrammedHash"
    Write-Output ("Programmed bytes identical:   " + $(if ($programmedIdentical) { 'PASS' } else { 'FAIL' }))

    $results += [pscustomobject]@{
        Cell = $label
        HardwareBuildDirectory = $hardwareBuildDir
        RebuildDirectory = $rebuiltBuild
        HardwareElfSHA256 = $cell.HardwareElf
        RebuiltElfSHA256 = $rebuiltElf
        ElfByteIdentical = $elfIdentical
        HardwareProgrammedSHA256 = $originalProgrammedHash
        RebuiltProgrammedSHA256 = $rebuiltProgrammedHash
        ProgrammedBytesIdentical = $programmedIdentical
        VerifierLog = $targetLog
    }

    if (-not $programmedIdentical) {
        $results |
            Export-Csv `
                -LiteralPath (Join-Path $outDir 'eight-cell-repro.csv') `
                -NoTypeInformation `
                -Encoding UTF8

        throw "$label current source does NOT reproduce hardware-tested programmed bytes."
    }
}

$results |
    Export-Csv `
        -LiteralPath (Join-Path $outDir 'eight-cell-repro.csv') `
        -NoTypeInformation `
        -Encoding UTF8

$afterHashes = Get-SourceHashes -Paths $sourceFiles

@(
    foreach ($relative in $afterHashes.Keys) {
        [pscustomobject]@{
            RelativePath = $relative
            SHA256 = $afterHashes[$relative]
        }
    }
) | Export-Csv `
        -LiteralPath (Join-Path $outDir 'source-hashes-after.csv') `
        -NoTypeInformation `
        -Encoding UTF8

foreach ($relative in $beforeHashes.Keys) {
    if ($afterHashes[$relative] -ne $beforeHashes[$relative]) {
        throw "Source changed during reproduction campaign: $relative"
    }
}

$indexHashAfter = (
    Get-FileHash -LiteralPath $indexPath -Algorithm SHA256
).Hash

if ($indexHashAfter -ne $indexHashBefore) {
    throw 'Git index content changed during reproduction campaign.'
}

if ((git rev-parse HEAD).Trim() -ne $expectedHead) {
    throw 'HEAD changed during reproduction campaign.'
}

$elfIdenticalCount = @(
    $results |
    Where-Object { $_.ElfByteIdentical }
).Count

$programmedIdenticalCount = @(
    $results |
    Where-Object { $_.ProgrammedBytesIdentical }
).Count

Write-Output "`n=== REPRODUCIBILITY RESULT ==="
Write-Output 'Native suite:                           116 / 116 PASS'
Write-Output "ELF byte-identical cells:               $elfIdenticalCount / 8"
Write-Output "Programmed-byte-identical cells:        $programmedIdenticalCount / 8 PASS"
Write-Output 'Current source bytes during campaign:   UNCHANGED'
Write-Output 'Git HEAD:                               UNCHANGED'
Write-Output 'Git index bytes:                        UNCHANGED'
Write-Output "Evidence directory:                     $outDir"
Write-Output ''
Write-Output 'R2-W6 CURRENT SOURCE REPRODUCES ALL HARDWARE-TESTED PROGRAMMED IMAGES: PASS'
Write-Output 'No flash/reset/MCU operation performed.'
Write-Output 'No commit/push performed.'
