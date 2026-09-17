$ErrorActionPreference = 'Stop'

$repo = 'E:\Projects\stm32-stream-lab'
$git = 'C:\Program Files\Git\cmd\git.exe'

$cmake = 'E:\DevTools\cmake-3.31.12-windows-x86_64\bin\cmake.exe'
$ninja = 'E:\DevTools\ninja-1.13.1-windows-x86_64\ninja.exe'
$armBin = 'E:\DevTools\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi\bin'

$source = Join-Path $repo 'firmware\cubemx'
$toolchain = Join-Path $source 'cmake\gcc-arm-none-eabi.cmake'
$build = Join-Path $repo 'build\r1-lifecycle-01'

$programmerRoot = 'E:\DevTools\STM32CubeProgrammer-2.23.0'
$stlinkSerial = '067AFF545754655087043860'

$expectedHead = '8475676b7ffd657c2bcb6a56328322a15a400530'

Set-Location -LiteralPath $repo

Write-Output '=== R1 LIFECYCLE REGRESSION BUILD / FLASH ==='

try {
    # -------------------------------------------------------------------------
    # 1. Repository guard
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 1. Repository guard ==='

    $head = (& $git rev-parse HEAD).Trim()
    if ($head -ne $expectedHead) {
        throw "Unexpected HEAD: $head"
    }

    $staged = @(& $git diff --cached --name-only)
    if ($staged.Count -ne 0) {
        throw 'Staging area is not empty.'
    }

    $expectedChanges = @(
        'firmware/acquisition/r1_bringup.c',
        'firmware/acquisition/r1_bringup.h'
    ) | Sort-Object

    $before = @(& $git status --porcelain=v1 --untracked-files=all)

    $actualChanges = @(
        $before |
        ForEach-Object {
            if ($_.Length -ge 4) {
                $_.Substring(3)
            }
        }
    ) | Sort-Object

    $scopeDiff = Compare-Object `
        -ReferenceObject $expectedChanges `
        -DifferenceObject $actualChanges

    if ($scopeDiff) {
        $scopeDiff | Format-Table
        throw 'Repository change scope is not the expected lifecycle-harness scope.'
    }

    $freeRtosState = @(
        & $git status --porcelain=v1 --untracked-files=all -- `
            firmware/cubemx/Middlewares/Third_Party/FreeRTOS-Kernel
    )

    if ($freeRtosState.Count -ne 0) {
        throw 'FreeRTOS subtree is not clean.'
    }

    Write-Output 'HEAD: verified'
    Write-Output 'Lifecycle harness change scope: verified'
    Write-Output 'FreeRTOS subtree: clean'

    # -------------------------------------------------------------------------
    # 2. Tool checks
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 2. Tool checks ==='

    foreach ($path in @($cmake, $ninja, $toolchain)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required build tool/file is missing: $path"
        }
    }

    if (-not (Test-Path -LiteralPath $armBin -PathType Container)) {
        throw 'Arm GNU toolchain directory is missing.'
    }

    $cliCandidate = Join-Path $programmerRoot 'bin\STM32_Programmer_CLI.exe'

    if (Test-Path -LiteralPath $cliCandidate -PathType Leaf) {
        $cli = $cliCandidate
    }
    else {
        $matches = @(
            Get-ChildItem `
                -LiteralPath $programmerRoot `
                -Recurse `
                -File `
                -Filter 'STM32_Programmer_CLI.exe'
        )

        if ($matches.Count -ne 1) {
            throw "Expected one STM32_Programmer_CLI.exe, found: $($matches.Count)"
        }

        $cli = $matches[0].FullName
    }

    Write-Output 'Build and flash tools: verified'

    # -------------------------------------------------------------------------
    # 3. Fresh configure
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 3. Fresh configure ==='

    if (Test-Path -LiteralPath $build) {
        Remove-Item -LiteralPath $build -Recurse -Force
    }

    New-Item -ItemType Directory -Path $build | Out-Null

    $env:PATH = "$armBin;$(Split-Path -Parent $ninja);$env:PATH"

    $configureOut = Join-Path $build 'configure.stdout.txt'
    $configureErr = Join-Path $build 'configure.stderr.txt'

    $configureArgs = @(
        '-S', $source,
        '-B', $build,
        '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Debug',
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
        "-DCMAKE_MAKE_PROGRAM=$ninja"
    )

    $p = Start-Process `
        -FilePath $cmake `
        -ArgumentList $configureArgs `
        -Wait `
        -PassThru `
        -NoNewWindow `
        -RedirectStandardOutput $configureOut `
        -RedirectStandardError $configureErr

    $p.Refresh()

    if ($p.ExitCode -ne 0) {
        Get-Content $configureOut | Select-Object -Last 80
        Get-Content $configureErr | Select-Object -Last 80
        throw 'CMake configure failed.'
    }

    Write-Output 'Fresh configure: PASS'

    # -------------------------------------------------------------------------
    # 4. Build
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 4. Build ==='

    $buildOut = Join-Path $build 'build.stdout.txt'
    $buildErr = Join-Path $build 'build.stderr.txt'

    $p = Start-Process `
        -FilePath $cmake `
        -ArgumentList @('--build', $build, '--parallel') `
        -Wait `
        -PassThru `
        -NoNewWindow `
        -RedirectStandardOutput $buildOut `
        -RedirectStandardError $buildErr

    $p.Refresh()

    if ($p.ExitCode -ne 0) {
        Get-Content $buildOut | Select-Object -Last 80
        Get-Content $buildErr | Select-Object -Last 80
        throw 'Firmware build failed.'
    }

    $warnings = @(
        Select-String `
            -Path $buildOut,$buildErr `
            -Pattern @(
                'r1_acquisition.*warning:',
                'warning:.*r1_acquisition',
                'r1_bringup.*warning:',
                'warning:.*r1_bringup'
            )
    )

    if ($warnings.Count -ne 0) {
        $warnings
        throw 'Compiler warnings were emitted for R1 sources.'
    }

    Write-Output 'Fresh build: PASS'
    Write-Output 'R1 compiler warnings: 0'

    # -------------------------------------------------------------------------
    # 5. ELF and symbol verification
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 5. ELF / symbol verification ==='

    $elfs = @(
        Get-ChildItem `
            -LiteralPath $build `
            -Recurse `
            -File `
            -Filter '*.elf'
    )

    if ($elfs.Count -ne 1) {
        throw "Expected exactly one ELF, found: $($elfs.Count)"
    }

    $elf = $elfs[0]

    $hash = (
        Get-FileHash `
            -LiteralPath $elf.FullName `
            -Algorithm SHA256
    ).Hash

    $nm = Join-Path $armBin 'arm-none-eabi-nm.exe'

    $symbols = @(& $nm -g $elf.FullName)

    if ($LASTEXITCODE -ne 0) {
        throw 'nm failed on lifecycle ELF.'
    }

    foreach ($symbol in @(
        'R1_Acquisition_Start',
        'R1_Acquisition_Stop',
        'R1_Bringup_CreateTask',
        'g_r1_lifecycle_result'
    )) {
        $hit = @(
            $symbols |
            Where-Object {
                $_ -match ("\b{0}$" -f [regex]::Escape($symbol))
            }
        )

        if ($hit.Count -ne 1) {
            throw "Linked lifecycle symbol missing: $symbol"
        }
    }

    Write-Output "ELF SHA256: $hash"
    Write-Output 'Lifecycle harness symbols: linked'

    # -------------------------------------------------------------------------
    # 6. Flash exact ELF
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 6. Flash / verify / reset ==='

    $flashOut = Join-Path $build 'flash.stdout.txt'
    $flashErr = Join-Path $build 'flash.stderr.txt'

    $arguments = @(
        '-c',
        'port=SWD',
        'freq=4000',
        "sn=$stlinkSerial",
        '-w',
        $elf.FullName,
        '-v',
        '-rst'
    )

    $p = Start-Process `
        -FilePath $cli `
        -ArgumentList $arguments `
        -Wait `
        -PassThru `
        -NoNewWindow `
        -RedirectStandardOutput $flashOut `
        -RedirectStandardError $flashErr

    $p.Refresh()

    if ($p.ExitCode -ne 0) {
        Get-Content $flashOut | Select-Object -Last 80
        Get-Content $flashErr | Select-Object -Last 40
        throw 'Flash / verify / reset failed.'
    }

    Write-Output 'SWD flash: PASS'
    Write-Output 'Flash verify: PASS'
    Write-Output 'Target reset: issued'

    # Six lifecycle cycles finish well below this delay.
    Start-Sleep -Seconds 2

    Write-Output 'Lifecycle completion wait: complete'

    # -------------------------------------------------------------------------
    # 7. Repository integrity
    # -------------------------------------------------------------------------
    Write-Output ''
    Write-Output '=== 7. Repository integrity ==='

    $after = @(& $git status --porcelain=v1 --untracked-files=all)

    $stateDiff = Compare-Object `
        -ReferenceObject @($before | Sort-Object) `
        -DifferenceObject @($after | Sort-Object)

    if ($stateDiff) {
        $stateDiff | Format-Table
        throw 'Build/flash changed repository source state.'
    }

    Write-Output 'Repository source state: unchanged'

    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R1 LIFECYCLE REGRESSION BUILD / FLASH: PASS'
    Write-Output '============================================================'
    Write-Output "ELF SHA256: $hash"
    Write-Output 'Fresh configure/build: PASS'
    Write-Output 'R1 compiler warnings: 0'
    Write-Output 'SWD flash/verify: PASS'
    Write-Output 'Target reset: issued'
    Write-Output 'Lifecycle run wait: complete'
    Write-Output 'Repository source state: unchanged'
    Write-Output ''
    Write-Output 'Lifecycle result: NOT YET INSPECTED'
}
catch {
    Write-Output ''
    Write-Output '============================================================'
    Write-Output 'R1 LIFECYCLE REGRESSION BUILD / FLASH: FAIL'
    Write-Output '============================================================'
    Write-Output $_.Exception.Message
    Write-Output ''
    Write-Output 'Do not reset, commit, or start soak testing.'
    exit 1
}
