param()

$ErrorActionPreference = 'Stop'

$repo =
    'E:\Projects\stm32-stream-lab'

$build =
    'E:\Projects\stm32-stream-lab\build\r2-w6-w6target-k2-normal-1518c429'

$elf =
    Join-Path $build 'cubemx.elf'

$inspection =
    Join-Path $build 'hardware-inspection-summary-01.txt'

$expectedElfHash =
    '1D44D800B5940882F00D1E6CCCD1FF6E2A30B3661BB19EAE37292E022EE8C3F7'

$expectedHead =
    '2752c0e915ab4725cc13e400d16fe47b14adfd4e'

$gdb =
    'E:\DevTools\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi\bin\arm-none-eabi-gdb.exe'

$gdbServer =
    'E:\DevTools\STM32CubeCLT-1.22.0\STLink-gdb-server\bin\ST-LINK_gdbserver.exe'

$cubeProgrammerBin =
    'E:\DevTools\STM32CubeCLT-1.22.0\STM32CubeProgrammer\bin'

$gdbScript =
    Join-Path $build 'hardware-trace-export-01.gdb'

$traceCsv =
    Join-Path $build 'hardware-trace-01.csv'

$rawCapture =
    Join-Path $build 'hardware-trace-export-01.txt'

$serverStdout =
    Join-Path $build 'hardware-gdbserver-trace-01.stdout.txt'

$serverStderr =
    Join-Path $build 'hardware-gdbserver-trace-01.stderr.txt'

$validator =
    Join-Path $build 'validate-hardware-trace-01.py'

Set-Location -LiteralPath $repo

Write-Output '=== R2-W6 K=2 NORMAL TRACE EXPORT PREFLIGHT ==='

$head = (git rev-parse HEAD).Trim()

if ($LASTEXITCODE -ne 0) {
    throw 'Unable to determine Git HEAD.'
}

Write-Output "HEAD: $head"

if ($head -ne $expectedHead) {
    throw "Unexpected HEAD: $head"
}

foreach ($path in @(
    $elf,
    $inspection,
    $gdb,
    $gdbServer
)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file missing: $path"
    }
}

if (-not (Test-Path -LiteralPath $cubeProgrammerBin -PathType Container)) {
    throw "CubeProgrammer bin directory missing: $cubeProgrammerBin"
}

$elfHash = (
    Get-FileHash `
        -LiteralPath $elf `
        -Algorithm SHA256
).Hash

Write-Output "ELF SHA256: $elfHash"

if ($elfHash -ne $expectedElfHash) {
    throw 'ELF identity mismatch.'
}

$summaryText =
    [System.IO.File]::ReadAllText($inspection)

$requiredAnchors = @(
    'magic=0x52325736',
    'phase=5',
    'test_pass=1',
    'fault_bits=0x00000000',
    'configured_k=2',
    'drop_mode=0',
    'process_hold_blocks=0',
    'input_count=96',
    'admitted_count=96',
    'capacity_drop_count=0',
    'processed_count=96',
    'released_count=96',
    'token_ledger_errors=0',
    'dma_error_flags_seen=0',
    'adc_ovr_seen=0',
    'sample_errors=0',
    'canary_errors=0',
    'pool_k=2',
    'pool_active_count=4',
    'pool_free_count=2',
    'pool_dma_owned_count=2',
    'pool_ready_count=0',
    'pool_processing_count=0',
    'pool_violation_count=0',
    'slots_violation_count=0'
)

foreach ($anchor in $requiredAnchors) {
    if ($summaryText -notmatch [regex]::Escape($anchor)) {
        throw "Previously captured hardware PASS anchor missing: $anchor"
    }
}

Write-Output 'Existing hardware summary: VERIFIED'

foreach ($path in @(
    $gdbScript,
    $traceCsv,
    $rawCapture,
    $serverStdout,
    $serverStderr,
    $validator
)) {
    if (Test-Path -LiteralPath $path) {
        throw "Evidence file already exists; refusing to overwrite: $path"
    }
}

$pythonCommands = @(
    Get-Command python.exe, python3.exe `
        -CommandType Application `
        -All `
        -ErrorAction SilentlyContinue |
    Where-Object {
        $_.Source -notmatch '[\\/]WindowsApps[\\/]'
    }
)

if ($pythonCommands.Count -eq 0) {
    throw 'No installed Python executable was found.'
}

$python = $pythonCommands[0].Source
Write-Output "Python: $python"

$gdbText = @'
set pagination off
set confirm off
set print pretty off

target remote 127.0.0.1:61234

set $i = 0

while $i < 96
    printf "TRACE,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", \
        (unsigned int)g_r2_w6_result.trace[$i].sequence, \
        (unsigned int)g_r2_w6_result.trace[$i].decision, \
        (unsigned int)g_r2_w6_result.trace[$i].ct_entry, \
        (unsigned int)g_r2_w6_result.trace[$i].completed_slot, \
        (unsigned int)g_r2_w6_result.trace[$i].completed_id, \
        (unsigned int)g_r2_w6_result.trace[$i].replacement_id, \
        (unsigned int)g_r2_w6_result.trace[$i].ndtr_guard, \
        (unsigned int)g_r2_w6_result.trace[$i].free_depth_after_take, \
        (unsigned int)g_r2_w6_result.trace[$i].ready_depth_after_publish, \
        (unsigned int)g_r2_w6_result.trace[$i].nominal_to_decision_cycles, \
        (unsigned int)g_r2_w6_result.trace[$i].nominal_to_irq_exit_cycles, \
        (unsigned int)g_r2_w6_result.trace[$i].final_window_cycles, \
        (unsigned int)g_r2_w6_result.trace[$i].mapping_epoch_after, \
        (unsigned int)g_r2_w6_result.trace[$i].m0_before, \
        (unsigned int)g_r2_w6_result.trace[$i].m1_before, \
        (unsigned int)g_r2_w6_result.trace[$i].m0_after, \
        (unsigned int)g_r2_w6_result.trace[$i].m1_after, \
        (unsigned int)g_r2_w6_result.trace[$i].processing_begin_offset_cycles, \
        (unsigned int)g_r2_w6_result.trace[$i].processing_end_offset_cycles, \
        (unsigned int)g_r2_w6_result.trace[$i].release_commit_offset_cycles, \
        (unsigned int)g_r2_w6_result.trace[$i].raw_min, \
        (unsigned int)g_r2_w6_result.trace[$i].raw_max, \
        (unsigned int)g_r2_w6_result.trace[$i].processed_ok

    set $i = $i + 1
end

detach
quit
'@

$validatorText = @'
import csv
import sys
from pathlib import Path

csv_path = Path(sys.argv[1])

with csv_path.open("r", encoding="utf-8", newline="") as f:
    rows = list(csv.DictReader(f))

def u(row, name):
    return int(row[name], 0)

if len(rows) != 96:
    raise SystemExit(f"FAIL: expected 96 rows, found {len(rows)}")

max_decision = 0
max_irq_exit = 0
max_final = 0
previous_epoch = None
seen_completed_ids = set()
seen_replacement_ids = set()

for i, row in enumerate(rows):
    seq = u(row, "sequence")
    decision = u(row, "decision")
    ct = u(row, "ct_entry")
    completed_slot = u(row, "completed_slot")
    completed_id = u(row, "completed_id")
    replacement_id = u(row, "replacement_id")
    ndtr = u(row, "ndtr_guard")
    free_depth = u(row, "free_depth_after_take")
    ready_depth = u(row, "ready_depth_after_publish")
    decision_cycles = u(row, "nominal_to_decision_cycles")
    irq_exit_cycles = u(row, "nominal_to_irq_exit_cycles")
    final_cycles = u(row, "final_window_cycles")
    epoch = u(row, "mapping_epoch_after")
    m0_before = u(row, "m0_before")
    m1_before = u(row, "m1_before")
    m0_after = u(row, "m0_after")
    m1_after = u(row, "m1_after")
    processing_begin = u(row, "processing_begin_offset_cycles")
    processing_end = u(row, "processing_end_offset_cycles")
    release_commit = u(row, "release_commit_offset_cycles")
    raw_min = u(row, "raw_min")
    raw_max = u(row, "raw_max")
    processed_ok = u(row, "processed_ok")

    if seq != i + 1:
        raise SystemExit(
            f"FAIL: sequence mismatch at row {i+1}: {seq}"
        )

    if decision != 1:
        raise SystemExit(
            f"FAIL: NORMAL cell contains non-ADMIT decision "
            f"at sequence {seq}: {decision}"
        )

    expected_ct = seq & 1
    if ct != expected_ct:
        raise SystemExit(
            f"FAIL: CT alternation mismatch at sequence {seq}"
        )

    expected_completed_slot = ct ^ 1
    if completed_slot != expected_completed_slot:
        raise SystemExit(
            f"FAIL: completed-slot mismatch at sequence {seq}"
        )

    if completed_id >= 4:
        raise SystemExit(
            f"FAIL: inactive buffer completed in K=2 cell "
            f"at sequence {seq}: {completed_id}"
        )

    if replacement_id >= 4:
        raise SystemExit(
            f"FAIL: inactive replacement buffer entered K=2 cell "
            f"at sequence {seq}: {replacement_id}"
        )

    seen_completed_ids.add(completed_id)
    seen_replacement_ids.add(replacement_id)

    if not (192 <= ndtr <= 256):
        raise SystemExit(
            f"FAIL: NDTR guard violation at sequence {seq}: {ndtr}"
        )

    if free_depth > 1:
        raise SystemExit(
            f"FAIL: invalid free depth after take at sequence {seq}: "
            f"{free_depth}"
        )

    if ready_depth != 1:
        raise SystemExit(
            f"FAIL: NORMAL publish did not produce ReadyQueue depth 1 "
            f"at sequence {seq}: {ready_depth}"
        )

    if decision_cycles > 57600:
        raise SystemExit(
            f"FAIL: decision timing violation at sequence {seq}: "
            f"{decision_cycles}"
        )

    if irq_exit_cycles > 80640:
        raise SystemExit(
            f"FAIL: IRQ-exit timing violation at sequence {seq}: "
            f"{irq_exit_cycles}"
        )

    if final_cycles > 3600:
        raise SystemExit(
            f"FAIL: final-window violation at sequence {seq}: "
            f"{final_cycles}"
        )

    max_decision = max(max_decision, decision_cycles)
    max_irq_exit = max(max_irq_exit, irq_exit_cycles)
    max_final = max(max_final, final_cycles)

    if processed_ok != 1:
        raise SystemExit(
            f"FAIL: ADMIT was not processed at sequence {seq}"
        )

    if (
        processing_begin == 0
        or processing_end == 0
        or release_commit == 0
    ):
        raise SystemExit(
            f"FAIL: Processing/release timestamps missing "
            f"at sequence {seq}"
        )

    if raw_min > raw_max or raw_max > 4095:
        raise SystemExit(
            f"FAIL: invalid ADC range at sequence {seq}"
        )

    if completed_slot == 0:
        if m0_after == m0_before:
            raise SystemExit(
                f"FAIL: inactive M0AR not replaced at sequence {seq}"
            )
        if m1_after != m1_before:
            raise SystemExit(
                f"FAIL: active M1AR changed at sequence {seq}"
            )
    else:
        if m1_after == m1_before:
            raise SystemExit(
                f"FAIL: inactive M1AR not replaced at sequence {seq}"
            )
        if m0_after != m0_before:
            raise SystemExit(
                f"FAIL: active M0AR changed at sequence {seq}"
            )

    if previous_epoch is not None and epoch != previous_epoch + 1:
        raise SystemExit(
            f"FAIL: mapping epoch did not advance exactly once "
            f"at sequence {seq}"
        )

    previous_epoch = epoch

checks = [
    (
        seen_completed_ids == {0, 1, 2, 3},
        f"completed IDs observed: {sorted(seen_completed_ids)}",
    ),
    (
        seen_replacement_ids == {0, 1, 2, 3},
        f"replacement IDs observed: {sorted(seen_replacement_ids)}",
    ),
    (
        max_decision == 6800,
        f"max decision {max_decision} != 6800",
    ),
    (
        max_irq_exit == 11836,
        f"max IRQ exit {max_irq_exit} != 11836",
    ),
    (
        max_final == 1075,
        f"max final window {max_final} != 1075",
    ),
]

for ok, message in checks:
    if not ok:
        raise SystemExit("FAIL: " + message)

print("Rows:                         96")
print("Decisions:                    96 / 96 ADMIT")
print("processed_ok:                 96 / 96")
print("CT/completed-slot sequence:    96 / 96 PASS")
print("K=2 active buffer ID range:   96 / 96 PASS")
print("Inactive-slot-only rebind:     96 / 96 PASS")
print("Mapping epoch +1 per ADMIT:    96 / 96 PASS")
print("Processing/release evidence:   96 / 96 PASS")
print("ReadyQueue publish depth:      96 / 96 PASS")
print(f"Completed IDs observed:        {sorted(seen_completed_ids)}")
print(f"Replacement IDs observed:      {sorted(seen_replacement_ids)}")
print(f"Max decision cycles:           {max_decision}")
print(f"Max IRQ-exit cycles:           {max_irq_exit}")
print(f"Max final window:              {max_final}")
print("Trace vs summary maxima:       PASS")
'@

$utf8NoBom =
    New-Object System.Text.UTF8Encoding($false)

[System.IO.File]::WriteAllText(
    $gdbScript,
    $gdbText,
    $utf8NoBom
)

[System.IO.File]::WriteAllText(
    $validator,
    $validatorText,
    $utf8NoBom
)

Write-Output "`n=== VALIDATOR SYNTAX CHECK ==="

$oldPreference =
    $ErrorActionPreference

try {
    $ErrorActionPreference = 'Continue'

    $syntaxOutput = @(
        & $python `
            -m py_compile `
            $validator `
            2>&1
    )

    $syntaxRc =
        $LASTEXITCODE
}
finally {
    $ErrorActionPreference =
        $oldPreference
}

$syntaxOutput |
    ForEach-Object {
        Write-Output $_.ToString()
    }

Write-Output "Python validator syntax exit code: $syntaxRc"

if ($syntaxRc -ne 0) {
    throw 'Python validator syntax check failed. No GDB operation was started.'
}

$pycache =
    Join-Path $build '__pycache__'

if (Test-Path -LiteralPath $pycache -PathType Container) {
    Remove-Item `
        -LiteralPath $pycache `
        -Recurse `
        -Force `
        -ErrorAction SilentlyContinue
}

Write-Output 'Python validator syntax: PASS'

Write-Output "`n=== DEBUG PORT CHECK ==="

$listener = @(
    Get-NetTCPConnection `
        -State Listen `
        -LocalPort 61234 `
        -ErrorAction SilentlyContinue
)

if ($listener.Count -ne 0) {
    $listener |
        Select-Object LocalAddress, LocalPort, OwningProcess |
        Format-Table -AutoSize

    throw 'TCP port 61234 is already occupied.'
}

Write-Output 'TCP port 61234: AVAILABLE'

Write-Output "`n=== ATTACH AND EXPORT EXISTING K=2 NORMAL TRACE ==="
Write-Output 'Reset: NO'
Write-Output 'Flash: NO'
Write-Output 'Run/restart: NO'

$serverArgs = @(
    '-p', '61234',
    '-d',
    '-g',
    '-l', '1',
    '-cp', $cubeProgrammerBin
)

$serverProcess = Start-Process `
    -FilePath $gdbServer `
    -ArgumentList $serverArgs `
    -RedirectStandardOutput $serverStdout `
    -RedirectStandardError $serverStderr `
    -PassThru

try {
    Start-Sleep -Milliseconds 1200

    if ($serverProcess.HasExited) {
        Write-Output "`n=== GDB SERVER STDOUT ==="

        Get-Content `
            -LiteralPath $serverStdout `
            -ErrorAction SilentlyContinue

        Write-Output "`n=== GDB SERVER STDERR ==="

        Get-Content `
            -LiteralPath $serverStderr `
            -ErrorAction SilentlyContinue

        throw "ST-LINK GDB server exited early. Exit code: $($serverProcess.ExitCode)"
    }

    $oldPreference =
        $ErrorActionPreference

    try {
        $ErrorActionPreference = 'Continue'

        $raw = @(
            & $gdb `
                --batch `
                --quiet `
                $elf `
                -x $gdbScript `
                2>&1
        )

        $gdbRc =
            $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference =
            $oldPreference
    }

    $rawLines = @(
        $raw |
        ForEach-Object {
            $_.ToString()
        }
    )

    [System.IO.File]::WriteAllLines(
        $rawCapture,
        $rawLines,
        $utf8NoBom
    )

    Write-Output "GDB exit code: $gdbRc"

    if ($gdbRc -ne 0) {
        $rawLines
        throw "GDB trace export failed with exit code $gdbRc."
    }

    $traceLines = @(
        $rawLines |
        Where-Object {
            $_ -match '^TRACE,'
        } |
        ForEach-Object {
            $_.Substring(6)
        }
    )

    Write-Output "TRACE rows captured: $($traceLines.Count)"

    if ($traceLines.Count -ne 96) {
        throw "Expected exactly 96 TRACE rows; found $($traceLines.Count)."
    }

    $header = (
        'sequence,' +
        'decision,' +
        'ct_entry,' +
        'completed_slot,' +
        'completed_id,' +
        'replacement_id,' +
        'ndtr_guard,' +
        'free_depth_after_take,' +
        'ready_depth_after_publish,' +
        'nominal_to_decision_cycles,' +
        'nominal_to_irq_exit_cycles,' +
        'final_window_cycles,' +
        'mapping_epoch_after,' +
        'm0_before,' +
        'm1_before,' +
        'm0_after,' +
        'm1_after,' +
        'processing_begin_offset_cycles,' +
        'processing_end_offset_cycles,' +
        'release_commit_offset_cycles,' +
        'raw_min,' +
        'raw_max,' +
        'processed_ok'
    )

    [System.IO.File]::WriteAllLines(
        $traceCsv,
        @($header) + $traceLines,
        $utf8NoBom
    )
}
finally {
    if ($null -ne $serverProcess) {
        try {
            if (-not $serverProcess.WaitForExit(3000)) {
                Stop-Process `
                    -Id $serverProcess.Id `
                    -Force `
                    -ErrorAction SilentlyContinue
            }
        }
        catch {
            # Host-side cleanup only.
        }
    }
}

Write-Output "`n=== MACHINE-READABLE K=2 NORMAL TRACE VALIDATION ==="

$oldPreference =
    $ErrorActionPreference

try {
    $ErrorActionPreference = 'Continue'

    $validationOutput = @(
        & $python `
            $validator `
            $traceCsv `
            2>&1
    )

    $validationRc =
        $LASTEXITCODE
}
finally {
    $ErrorActionPreference =
        $oldPreference
}

$validationOutput |
    ForEach-Object {
        Write-Output $_.ToString()
    }

Write-Output "Validator exit code: $validationRc"

if ($validationRc -ne 0) {
    throw "K=2 NORMAL trace validation failed with exit code $validationRc."
}

$traceHash = (
    Get-FileHash `
        -LiteralPath $traceCsv `
        -Algorithm SHA256
).Hash

Write-Output "`n=== TRACE EVIDENCE IDENTITY ==="
Write-Output "CSV: $traceCsv"
Write-Output "CSV SHA256: $traceHash"
Write-Output "Raw GDB capture: $rawCapture"
Write-Output "Validator: $validator"

Write-Output "`n=== POST-EXPORT REPOSITORY STATE ==="

$postHead =
    (git rev-parse HEAD).Trim()

Write-Output "HEAD: $postHead"
git status --short --branch

if ($postHead -ne $expectedHead) {
    throw 'HEAD changed during trace export.'
}

Write-Output ''
Write-Output 'R2-W6 K=2 NORMAL TRACE EVIDENCE: PASS'
