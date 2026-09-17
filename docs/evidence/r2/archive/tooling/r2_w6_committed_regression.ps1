param()

$ErrorActionPreference = 'Stop'

$repo = 'E:\Projects\stm32-stream-lab'
$verify = Join-Path $repo 'tools\r2\verify_w6.ps1'
$buildRoot = Join-Path $repo 'build'

$expectedHead = 'bbc3bf6821c4f6e4dc73beabd5685b8b0828205c'
$expectedParent = '2752c0e915ab4725cc13e400d16fe47b14adfd4e'
$expectedVerifierHash = 'BF632BB6C59998C5DBF00D36B734154AA261205847329728B13DDCC838C1AA42'

$objcopy =
    'E:\DevTools\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi\bin\arm-none-eabi-objcopy.exe'

$w6Cells = @(
    [pscustomobject]@{
        K=1; Mode='NORMAL'
        HardwareBuild='r2-w6-w6target-k1-normal-16185105'
    },
    [pscustomobject]@{
        K=1; Mode='DROP'
        HardwareBuild='r2-w6-w6target-k1-drop-eeb059ed'
    },
    [pscustomobject]@{
        K=2; Mode='NORMAL'
        HardwareBuild='r2-w6-w6target-k2-normal-1518c429'
    },
    [pscustomobject]@{
        K=2; Mode='DROP'
        HardwareBuild='r2-w6-w6target-k2-drop-e9491d79'
    },
    [pscustomobject]@{
        K=4; Mode='NORMAL'
        HardwareBuild='r2-w6-w6target-k4-normal-77d5741f'
    },
    [pscustomobject]@{
        K=4; Mode='DROP'
        HardwareBuild='r2-w6-w6target-k4-drop-64e98478'
    },
    [pscustomobject]@{
        K=8; Mode='NORMAL'
        HardwareBuild='r2-w6-w6target-k8-normal-a6b96a0b'
    },
    [pscustomobject]@{
        K=8; Mode='DROP'
        HardwareBuild='r2-w6-w6target-k8-drop-7a95200b'
    }
)

$lowerLayers = @(
    [pscustomobject]@{
        Label='W5'
        Profile='W5Regression'
        AnchorBuild='r2-w5-w5target-c4b79468'
    },
    [pscustomobject]@{
        Label='W4'
        Profile='W4Regression'
        AnchorBuild='r2-w4-w4target-e2e7b31f'
    },
    [pscustomobject]@{
        Label='W3'
        Profile='W3Regression'
        AnchorBuild='r2-w3-w3target-23c9886a'
    },
    [pscustomobject]@{
        Label='R1'
        Profile='R1Baseline'
        AnchorBuild='r2-w5-r1baseline-230b5252'
    }
)

function Get-BuildMap {
    param([Parameter(Mandatory=$true)][string]$Pattern)

    $map = @{}

    Get-ChildItem `
        -LiteralPath $buildRoot `
        -Directory `
        -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Name -like $Pattern
        } |
        ForEach-Object {
            $map[$_.FullName] = $true
        }

    return $map
}

function Get-NewBuild {
    param(
        [Parameter(Mandatory=$true)][hashtable]$Before,
        [Parameter(Mandatory=$true)][string]$Pattern,
        [Parameter(Mandatory=$true)][string]$Label
    )

    $new = @(
        Get-ChildItem `
            -LiteralPath $buildRoot `
            -Directory `
            -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Name -like $Pattern -and
            -not $Before.ContainsKey($_.FullName)
        }
    )

    if ($new.Count -ne 1) {
        Write-Output ('New builds for {0}:' -f $Label)

        $new |
            Select-Object FullName, LastWriteTime |
            Format-Table -AutoSize

        throw "$Label expected exactly one new build directory; found $($new.Count)."
    }

    return $new[0].FullName
}

function Assert-Summary {
    param(
        [Parameter(Mandatory=$true)][string]$Build,
        [Parameter(Mandatory=$true)][string[]]$Anchors
    )

    $summary = Join-Path $Build 'verification-summary.txt'

    if (-not (Test-Path -LiteralPath $summary -PathType Leaf)) {
        throw "verification-summary.txt missing: $summary"
    }

    $text = [System.IO.File]::ReadAllText($summary)

    foreach ($anchor in $Anchors) {
        if ($text -notmatch [regex]::Escape($anchor)) {
            throw "Summary $summary missing: $anchor"
        }
    }

    return $summary
}

function Invoke-Native {
    $pattern = 'r2-w6-native-*'
    $before = Get-BuildMap -Pattern $pattern

    Write-Output "`n=== COMMITTED NATIVE REGRESSION ==="

    & $verify `
        -Profile Native `
        -Repo $repo

    $rc = $LASTEXITCODE

    Write-Output "Native verifier exit code: $rc"

    if ($rc -ne 0) {
        throw "Native verifier failed with exit code $rc."
    }

    $build = Get-NewBuild `
        -Before $before `
        -Pattern $pattern `
        -Label 'Native'

    $summary = Assert-Summary `
        -Build $build `
        -Anchors @(
            'Profile: Native',
            "Source HEAD: $expectedHead",
            'Build: PASS',
            'Source byte fingerprint: unchanged',
            'Git index content: unchanged',
            'Hardware: NOT RUN',
            'W1/W2/W3/W4/W5/W6 native tests: 116 / 116 PASS'
        )

    Write-Output "Native build: $build"
    Write-Output "Native summary: $summary"
    Write-Output 'Native committed-state regression: PASS'

    return $build
}

function Invoke-Target {
    param(
        [Parameter(Mandatory=$true)][string]$Profile,
        [string]$K = '1',
        [string]$Mode = 'NORMAL',
        [Parameter(Mandatory=$true)][string]$Pattern,
        [Parameter(Mandatory=$true)][string]$Label
    )

    $before = Get-BuildMap -Pattern $Pattern

    Write-Output "`n=== BUILD $Label ==="

    if ($Profile -eq 'W6Target') {
        & $verify `
            -Profile W6Target `
            -K $K `
            -Mode $Mode `
            -Repo $repo
    }
    else {
        & $verify `
            -Profile $Profile `
            -Repo $repo
    }

    $rc = $LASTEXITCODE

    Write-Output "$Label verifier exit code: $rc"

    if ($rc -ne 0) {
        throw "$Label verifier failed with exit code $rc."
    }

    $build = Get-NewBuild `
        -Before $before `
        -Pattern $Pattern `
        -Label $Label

    if ($Profile -eq 'W6Target') {
        $null = Assert-Summary `
            -Build $build `
            -Anchors @(
                'Profile: W6Target',
                "Source HEAD: $expectedHead",
                'Build: PASS',
                'Source byte fingerprint: unchanged',
                'Git index content: unchanged',
                'Hardware: NOT RUN',
                "W6 K: $K",
                "W6 mode: $Mode"
            )
    }
    else {
        $null = Assert-Summary `
            -Build $build `
            -Anchors @(
                "Profile: $Profile",
                "Source HEAD: $expectedHead",
                'Build: PASS',
                'Source byte fingerprint: unchanged',
                'Git index content: unchanged',
                'Hardware: NOT RUN'
            )
    }

    $elf = Join-Path $build 'cubemx.elf'
    $elfHashFile = Join-Path $build 'elf.sha256'

    foreach ($path in @($elf,$elfHashFile)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "$Label missing build artifact: $path"
        }
    }

    $actual = (
        Get-FileHash -LiteralPath $elf -Algorithm SHA256
    ).Hash

    $recorded = (
        Get-Content -LiteralPath $elfHashFile -Raw
    ).Trim()

    if ($actual -ne $recorded) {
        throw "$Label ELF does not match its own elf.sha256."
    }

    return [pscustomobject]@{
        Build = $build
        Elf = $elf
        ElfSHA256 = $actual
    }
}

function Get-ProgrammedHash {
    param(
        [Parameter(Mandatory=$true)][string]$Elf,
        [Parameter(Mandatory=$true)][string]$BinPath
    )

    if (-not (Test-Path -LiteralPath $Elf -PathType Leaf)) {
        throw "ELF missing: $Elf"
    }

    if (Test-Path -LiteralPath $BinPath) {
        throw "Refusing to overwrite programmed-byte file: $BinPath"
    }

    $oldPreference = $ErrorActionPreference

    try {
        $ErrorActionPreference = 'Continue'

        $output = @(
            & $objcopy `
                -O binary `
                $Elf `
                $BinPath `
                2>&1
        )

        $rc = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $oldPreference
    }

    if ($rc -ne 0) {
        $output |
            ForEach-Object { Write-Output $_.ToString() }

        throw "objcopy failed with exit code $rc for $Elf"
    }

    if (-not (Test-Path -LiteralPath $BinPath -PathType Leaf)) {
        throw "objcopy returned success but output is missing: $BinPath"
    }

    return (
        Get-FileHash -LiteralPath $BinPath -Algorithm SHA256
    ).Hash
}

Set-Location -LiteralPath $repo

Write-Output '=== R2-W6 COMMITTED-STATE REGRESSION ==='

$head = (git rev-parse HEAD).Trim()
$parent = (git rev-parse HEAD^).Trim()
$subject = (git log -1 --pretty=%s).Trim()

Write-Output "HEAD:    $head"
Write-Output "Parent:  $parent"
Write-Output "Subject: $subject"

if ($head -ne $expectedHead) {
    throw "Unexpected HEAD: $head"
}

if ($parent -ne $expectedParent) {
    throw "Unexpected parent: $parent"
}

if ($subject -ne 'feat: establish R2 mandatory K matrix') {
    throw "Unexpected HEAD subject: $subject"
}

$verifyHash = (
    Get-FileHash -LiteralPath $verify -Algorithm SHA256
).Hash

Write-Output "Verifier SHA256: $verifyHash"

if ($verifyHash -ne $expectedVerifierHash) {
    throw 'Committed verifier SHA256 mismatch.'
}

Write-Output "`n=== TRACKED WORKING TREE CHECK ==="

git diff --exit-code -- HEAD --

if ($LASTEXITCODE -ne 0) {
    throw 'Tracked working tree differs from committed HEAD.'
}

git diff --cached --exit-code

if ($LASTEXITCODE -ne 0) {
    throw 'Git index contains staged changes.'
}

Write-Output 'Tracked working tree: EXACT HEAD'
Write-Output 'Git index: clean'

$untracked = @(
    git ls-files `
        --others `
        --exclude-standard
)

Write-Output "`n=== UNTRACKED FILES ==="
$untracked

$unexpectedUntracked = @(
    $untracked |
    Where-Object {
        $_ -notlike 'docs/evidence/r2/w6/*'
    }
)

if ($unexpectedUntracked.Count -ne 0) {
    Write-Output 'Unexpected untracked files:'
    $unexpectedUntracked
    throw 'Unexpected untracked repository files present.'
}

Write-Output 'Untracked scope: W6 evidence only'

$outDir = Join-Path $buildRoot (
    'r2-w6-committed-regression-' +
    (Get-Date -Format 'yyyyMMdd-HHmmss')
)

New-Item -ItemType Directory -Path $outDir | Out-Null

Write-Output "Regression evidence directory: $outDir"

$indexTreeBefore = (git write-tree).Trim()

$nativeBuild = Invoke-Native

$results = @()

Write-Output "`n=== W6 8-CELL PROGRAMMED-BYTE REGRESSION ==="

foreach ($cell in $w6Cells) {
    $label = "W6-K$($cell.K)-$($cell.Mode)"
    $pattern = "r2-w6-w6target-k$($cell.K)-$($cell.Mode.ToLowerInvariant())-*"

    $rebuilt = Invoke-Target `
        -Profile 'W6Target' `
        -K ([string]$cell.K) `
        -Mode $cell.Mode `
        -Pattern $pattern `
        -Label $label

    $anchorBuild = Join-Path $buildRoot $cell.HardwareBuild
    $anchorElf = Join-Path $anchorBuild 'cubemx.elf'

    if (-not (Test-Path -LiteralPath $anchorElf -PathType Leaf)) {
        throw "$label hardware-tested anchor ELF missing: $anchorElf"
    }

    $anchorBin = Join-Path $outDir ("anchor-" + $label.ToLowerInvariant() + '.bin')
    $rebuiltBin = Join-Path $outDir ("rebuilt-" + $label.ToLowerInvariant() + '.bin')

    $anchorProgrammed = Get-ProgrammedHash `
        -Elf $anchorElf `
        -BinPath $anchorBin

    $rebuiltProgrammed = Get-ProgrammedHash `
        -Elf $rebuilt.Elf `
        -BinPath $rebuiltBin

    $pass = ($anchorProgrammed -eq $rebuiltProgrammed)

    Write-Output "$label anchor programmed SHA256:  $anchorProgrammed"
    Write-Output "$label rebuilt programmed SHA256: $rebuiltProgrammed"
    Write-Output ("$label programmed-byte identity: " + $(if ($pass) {'PASS'} else {'FAIL'}))

    $results += [pscustomobject]@{
        Layer = 'W6'
        Label = $label
        Profile = 'W6Target'
        AnchorBuild = $anchorBuild
        Rebuild = $rebuilt.Build
        AnchorProgrammedSHA256 = $anchorProgrammed
        RebuiltProgrammedSHA256 = $rebuiltProgrammed
        ProgrammedBytesIdentical = $pass
    }

    if (-not $pass) {
        throw "$label programmed bytes differ from hardware-tested anchor."
    }
}

Write-Output "`n=== LOWER-LAYER PROGRAMMED-BYTE REGRESSION ==="

foreach ($layer in $lowerLayers) {
    $label = $layer.Label
    $profile = $layer.Profile
    $pattern = 'r2-w6-' + $profile.ToLowerInvariant() + '-*'

    $rebuilt = Invoke-Target `
        -Profile $profile `
        -Pattern $pattern `
        -Label $label

    $anchorBuild = Join-Path $buildRoot $layer.AnchorBuild
    $anchorElf = Join-Path $anchorBuild 'cubemx.elf'

    if (-not (Test-Path -LiteralPath $anchorElf -PathType Leaf)) {
        throw "$label historical anchor ELF missing: $anchorElf"
    }

    $anchorBin = Join-Path $outDir ("anchor-" + $label.ToLowerInvariant() + '.bin')
    $rebuiltBin = Join-Path $outDir ("rebuilt-" + $label.ToLowerInvariant() + '.bin')

    $anchorProgrammed = Get-ProgrammedHash `
        -Elf $anchorElf `
        -BinPath $anchorBin

    $rebuiltProgrammed = Get-ProgrammedHash `
        -Elf $rebuilt.Elf `
        -BinPath $rebuiltBin

    $pass = ($anchorProgrammed -eq $rebuiltProgrammed)

    Write-Output "$label anchor programmed SHA256:  $anchorProgrammed"
    Write-Output "$label rebuilt programmed SHA256: $rebuiltProgrammed"
    Write-Output ("$label programmed-byte identity: " + $(if ($pass) {'PASS'} else {'FAIL'}))

    $results += [pscustomobject]@{
        Layer = $label
        Label = $label
        Profile = $profile
        AnchorBuild = $anchorBuild
        Rebuild = $rebuilt.Build
        AnchorProgrammedSHA256 = $anchorProgrammed
        RebuiltProgrammedSHA256 = $rebuiltProgrammed
        ProgrammedBytesIdentical = $pass
    }

    if (-not $pass) {
        throw "$label programmed bytes differ from historical known-good anchor."
    }
}

$indexTreeAfter = (git write-tree).Trim()
$postHead = (git rev-parse HEAD).Trim()

if ($indexTreeAfter -ne $indexTreeBefore) {
    throw 'Git index tree changed during committed-state regression.'
}

if ($postHead -ne $expectedHead) {
    throw 'HEAD changed during committed-state regression.'
}

git diff --exit-code -- HEAD --

if ($LASTEXITCODE -ne 0) {
    throw 'Tracked working tree changed during committed-state regression.'
}

$resultsPath = Join-Path $outDir 'programmed-byte-regression.csv'

$results |
    Export-Csv `
        -LiteralPath $resultsPath `
        -NoTypeInformation `
        -Encoding UTF8

$manifestPath = Join-Path $outDir 'regression-manifest.sha256'

$manifestLines = @()

Get-ChildItem `
    -LiteralPath $outDir `
    -File |
    Where-Object {
        $_.FullName -ne $manifestPath
    } |
    Sort-Object Name |
    ForEach-Object {
        $hash = (
            Get-FileHash `
                -LiteralPath $_.FullName `
                -Algorithm SHA256
        ).Hash

        $manifestLines += "$hash  $($_.Name)"
    }

[System.IO.File]::WriteAllLines(
    $manifestPath,
    $manifestLines,
    (New-Object System.Text.UTF8Encoding($false))
)

$passCount = @(
    $results |
    Where-Object {
        $_.ProgrammedBytesIdentical
    }
).Count

Write-Output "`n=== COMMITTED-STATE REGRESSION RESULT ==="
Write-Output "Firmware milestone:              $expectedHead"
Write-Output 'Native W1-W6:                   116 / 116 PASS'
Write-Output 'W6 hardware-image regression:   8 / 8 PASS'
Write-Output 'W5 programmed-byte regression:  PASS'
Write-Output 'W4 programmed-byte regression:  PASS'
Write-Output 'W3 programmed-byte regression:  PASS'
Write-Output 'R1 programmed-byte regression:  PASS'
Write-Output "Programmed comparisons total:   $passCount / 12 PASS"
Write-Output 'Tracked working tree:           EXACT HEAD'
Write-Output 'Git index / HEAD:               UNCHANGED'
Write-Output "Evidence directory:             $outDir"
Write-Output "Manifest:                       $manifestPath"
Write-Output ''
Write-Output 'R2-W6 COMMITTED-STATE REGRESSION: PASS'
Write-Output 'No flash/reset/MCU operation performed.'
Write-Output 'No commit/push/tag operation performed.'
