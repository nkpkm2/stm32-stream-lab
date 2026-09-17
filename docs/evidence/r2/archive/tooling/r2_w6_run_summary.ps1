param(
    [Parameter(Mandatory = $true)]
    [ValidateSet(1,2,4,8)]
    [int]$K,

    [Parameter(Mandatory = $true)]
    [ValidateSet('NORMAL','DROP')]
    [string]$Mode,

    [Parameter(Mandatory = $true)]
    [string]$Build,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string]$ElfSha256
)

$ErrorActionPreference = 'Stop'

$repo =
    'E:\Projects\stm32-stream-lab'

$expectedHead =
    '2752c0e915ab4725cc13e400d16fe47b14adfd4e'

$programmer =
    'E:\DevTools\STM32CubeProgrammer-2.23.0\bin\STM32_Programmer_CLI.exe'

$gdb =
    'E:\DevTools\arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi\bin\arm-none-eabi-gdb.exe'

$gdbServer =
    'E:\DevTools\STM32CubeCLT-1.22.0\STLink-gdb-server\bin\ST-LINK_gdbserver.exe'

$cubeProgrammerBin =
    'E:\DevTools\STM32CubeCLT-1.22.0\STM32CubeProgrammer\bin'

$Mode = $Mode.ToUpperInvariant()
$expectedDropMode = if ($Mode -eq 'DROP') { 1 } else { 0 }
$expectedHoldBlocks = if ($Mode -eq 'DROP') { $K + 5 } else { 0 }

$elf =
    Join-Path $Build 'cubemx.elf'

$summary =
    Join-Path $Build 'verification-summary.txt'

$flashLog =
    Join-Path $Build 'hardware-flash-verify-01.txt'

$runLog =
    Join-Path $Build 'hardware-reset-run-01.txt'

$gdbScript =
    Join-Path $Build 'hardware-inspection-summary-01.gdb'

$inspection =
    Join-Path $Build 'hardware-inspection-summary-01.txt'

$serverStdout =
    Join-Path $Build 'hardware-gdbserver-summary-01.stdout.txt'

$serverStderr =
    Join-Path $Build 'hardware-gdbserver-summary-01.stderr.txt'

$validator =
    Join-Path $Build 'validate-hardware-summary-01.py'

Set-Location -LiteralPath $repo

Write-Output "=== R2-W6 K=$K $Mode RUN + SUMMARY PREFLIGHT ==="

$head = (git rev-parse HEAD).Trim()

if ($LASTEXITCODE -ne 0) {
    throw 'Unable to read Git HEAD.'
}

Write-Output "HEAD: $head"

if ($head -ne $expectedHead) {
    throw "Unexpected HEAD: $head"
}

foreach ($path in @(
    $Build,
    $cubeProgrammerBin
)) {
    if (-not (Test-Path -LiteralPath $path -PathType Container)) {
        throw "Required directory missing: $path"
    }
}

foreach ($path in @(
    $elf,
    $summary,
    $flashLog,
    $programmer,
    $gdb,
    $gdbServer
)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file missing: $path"
    }
}

$actualElfHash = (
    Get-FileHash `
        -LiteralPath $elf `
        -Algorithm SHA256
).Hash

Write-Output "ELF SHA256: $actualElfHash"

if ($actualElfHash -ne $ElfSha256.ToUpperInvariant()) {
    throw 'ELF identity mismatch. No reset issued.'
}

$summaryText =
    [System.IO.File]::ReadAllText($summary)

$requiredSummary = @(
    'Profile: W6Target',
    'Build: PASS',
    'Hardware: NOT RUN',
    "W6 K: $K",
    "W6 mode: $Mode"
)

foreach ($anchor in $requiredSummary) {
    if ($summaryText -notmatch [regex]::Escape($anchor)) {
        throw "Target summary identity mismatch: missing '$anchor'"
    }
}

$flashText =
    [System.IO.File]::ReadAllText($flashLog)

if ($flashText -notmatch 'STM32F446') {
    throw 'Flash log does not contain STM32F446 target identity.'
}

if ($flashText -notmatch 'Download verified successfully') {
    throw 'Flash log does not contain successful download verification.'
}

Write-Output 'Target profile identity: PASS'
Write-Output 'Flash/verify evidence:   PASS'

foreach ($path in @(
    $runLog,
    $gdbScript,
    $inspection,
    $serverStdout,
    $serverStderr,
    $validator
)) {
    if (Test-Path -LiteralPath $path) {
        throw "Evidence path already exists; refusing to overwrite: $path"
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

Write-Output "`n=== ISSUE ONE EXPLICIT MCU RESET ==="

$oldPreference =
    $ErrorActionPreference

try {
    $ErrorActionPreference = 'Continue'

    $resetOutput = @(
        & $programmer `
            -c 'port=SWD' `
            -rst `
            2>&1
    )

    $resetRc =
        $LASTEXITCODE
}
finally {
    $ErrorActionPreference =
        $oldPreference
}

$resetLines = @(
    $resetOutput |
    ForEach-Object {
        $_.ToString()
    }
)

$resetLines |
    ForEach-Object {
        Write-Output $_
    }

$utf8NoBom =
    New-Object System.Text.UTF8Encoding($false)

[System.IO.File]::WriteAllLines(
    $runLog,
    $resetLines,
    $utf8NoBom
)

Write-Output "`nReset command exit code: $resetRc"
Write-Output "Run evidence: $runLog"

if ($resetRc -ne 0) {
    throw "Explicit reset failed with exit code $resetRc."
}

$resetText =
    [System.IO.File]::ReadAllText($runLog)

$explicitResetObserved = (
    $resetText -match '(?m)^\s*MCU Reset\s*$' -and
    $resetText -match 'Software reset is performed'
)

Write-Output "Explicit reset evidence observed: $explicitResetObserved"

if (-not $explicitResetObserved) {
    throw 'Explicit reset evidence was not found.'
}

Write-Output "`n=== ALLOW BOUNDED W6 RUN TO COMPLETE ==="
Write-Output 'No debugger attached during measurement.'
Write-Output 'Waiting 2500 ms...'

Start-Sleep -Milliseconds 2500

Write-Output 'Run window elapsed.'

$gdbText = @'
set pagination off
set confirm off
set print pretty off

target remote 127.0.0.1:61234

printf "magic=0x%08x\n", (unsigned int)g_r2_w6_result.magic
printf "control_task_created=%u\n", (unsigned int)g_r2_w6_result.control_task_created
printf "processing_task_created=%u\n", (unsigned int)g_r2_w6_result.processing_task_created
printf "phase=%u\n", (unsigned int)g_r2_w6_result.phase
printf "test_pass=%u\n", (unsigned int)g_r2_w6_result.test_pass
printf "fault_bits=0x%08x\n", (unsigned int)g_r2_w6_result.fault_bits

printf "system_core_clock=%u\n", (unsigned int)g_r2_w6_result.system_core_clock
printf "configured_k=%u\n", (unsigned int)g_r2_w6_result.configured_k
printf "drop_mode=%u\n", (unsigned int)g_r2_w6_result.drop_mode
printf "process_hold_blocks=%u\n", (unsigned int)g_r2_w6_result.process_hold_blocks
printf "aircr=0x%08x\n", (unsigned int)g_r2_w6_result.aircr

printf "start_status=%u\n", (unsigned int)g_r2_w6_result.start_status
printf "stop_status=%u\n", (unsigned int)g_r2_w6_result.stop_status

printf "irq_count=%u\n", (unsigned int)g_r2_w6_result.irq_count
printf "input_count=%u\n", (unsigned int)g_r2_w6_result.input_count
printf "admitted_count=%u\n", (unsigned int)g_r2_w6_result.admitted_count
printf "capacity_drop_count=%u\n", (unsigned int)g_r2_w6_result.capacity_drop_count
printf "processed_count=%u\n", (unsigned int)g_r2_w6_result.processed_count
printf "released_count=%u\n", (unsigned int)g_r2_w6_result.released_count
printf "recovered_admission_after_drop_count=%u\n", (unsigned int)g_r2_w6_result.recovered_admission_after_drop_count
printf "current_drop_streak=%u\n", (unsigned int)g_r2_w6_result.current_drop_streak
printf "max_drop_streak=%u\n", (unsigned int)g_r2_w6_result.max_drop_streak

printf "init_hook_count=%u\n", (unsigned int)g_r2_w6_result.init_hook_count
printf "completion_hook_count=%u\n", (unsigned int)g_r2_w6_result.completion_hook_count
printf "illegal_free_send_count=%u\n", (unsigned int)g_r2_w6_result.illegal_free_send_count
printf "ready_send_fail_count=%u\n", (unsigned int)g_r2_w6_result.ready_send_fail_count
printf "notification_fail_count=%u\n", (unsigned int)g_r2_w6_result.notification_fail_count
printf "token_ledger_errors=%u\n", (unsigned int)g_r2_w6_result.token_ledger_errors
printf "dma_error_flags_seen=%u\n", (unsigned int)g_r2_w6_result.dma_error_flags_seen
printf "adc_ovr_seen=%u\n", (unsigned int)g_r2_w6_result.adc_ovr_seen

printf "max_nominal_to_decision_cycles=%u\n", (unsigned int)g_r2_w6_result.max_nominal_to_decision_cycles
printf "max_nominal_to_irq_exit_cycles=%u\n", (unsigned int)g_r2_w6_result.max_nominal_to_irq_exit_cycles
printf "max_final_window_cycles=%u\n", (unsigned int)g_r2_w6_result.max_final_window_cycles

printf "max_ready_depth=%u\n", (unsigned int)g_r2_w6_result.max_ready_depth
printf "min_free_depth=%u\n", (unsigned int)g_r2_w6_result.min_free_depth
printf "free_queue_depth_final=%u\n", (unsigned int)g_r2_w6_result.free_queue_depth_final
printf "ready_queue_depth_final=%u\n", (unsigned int)g_r2_w6_result.ready_queue_depth_final
printf "free_token_mask_final=0x%08x\n", (unsigned int)g_r2_w6_result.free_token_mask_final
printf "ready_token_mask_final=0x%08x\n", (unsigned int)g_r2_w6_result.ready_token_mask_final

printf "full_sample_count=%u\n", (unsigned int)g_r2_w6_result.full_sample_count
printf "sample_errors=%u\n", (unsigned int)g_r2_w6_result.sample_errors
printf "canary_errors=%u\n", (unsigned int)g_r2_w6_result.canary_errors

printf "quiet_irq_count=%u\n", (unsigned int)g_r2_w6_result.quiet_irq_count
printf "quiet_input_count=%u\n", (unsigned int)g_r2_w6_result.quiet_input_count
printf "quiet_admitted_count=%u\n", (unsigned int)g_r2_w6_result.quiet_admitted_count
printf "quiet_drop_count=%u\n", (unsigned int)g_r2_w6_result.quiet_drop_count
printf "quiet_processed_count=%u\n", (unsigned int)g_r2_w6_result.quiet_processed_count

printf "pool_activated=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.activated
printf "pool_k=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.k
printf "pool_active_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.active_count
printf "pool_inactive_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.inactive_count
printf "pool_free_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.free_count
printf "pool_dma_owned_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.dma_owned_count
printf "pool_ready_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.ready_count
printf "pool_processing_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.processing_count
printf "pool_violation_count=%u\n", (unsigned int)g_r2_w6_result.pool_at_stop.violation_count

printf "slots_initialized=%u\n", (unsigned int)g_r2_w6_result.slots_at_stop.initialized
printf "slots_mapping_epoch=%u\n", (unsigned int)g_r2_w6_result.slots_at_stop.mapping_epoch
printf "slots_violation_count=%u\n", (unsigned int)g_r2_w6_result.slots_at_stop.violation_count
printf "slots_m0_buffer=%u\n", (unsigned int)g_r2_w6_result.slots_at_stop.m0_buffer
printf "slots_m1_buffer=%u\n", (unsigned int)g_r2_w6_result.slots_at_stop.m1_buffer

set $i = 0
while $i < 10
    printf "buffer%u_admitted=%u\n", $i, (unsigned int)g_r2_w6_result.admitted_by_buffer[$i]
    printf "buffer%u_dropped=%u\n", $i, (unsigned int)g_r2_w6_result.dropped_by_buffer[$i]
    printf "buffer%u_processed=%u\n", $i, (unsigned int)g_r2_w6_result.processed_by_buffer[$i]
    printf "buffer%u_released=%u\n", $i, (unsigned int)g_r2_w6_result.released_by_buffer[$i]
    printf "pool_state_%u=%u\n", $i, (unsigned int)g_r2_w6_result.pool_at_stop.states[$i]
    set $i = $i + 1
end

detach
quit
'@

$validatorText = @'
import re
import sys
from pathlib import Path

inspection = Path(sys.argv[1])
k = int(sys.argv[2])
mode = sys.argv[3].upper()

text = inspection.read_text(encoding="utf-8", errors="replace")

values = {}
for line in text.splitlines():
    m = re.fullmatch(r"([A-Za-z0-9_]+)=(0x[0-9A-Fa-f]+|[0-9]+)", line.strip())
    if m:
        values[m.group(1)] = int(m.group(2), 0)

def req(name):
    if name not in values:
        raise SystemExit(f"FAIL: missing field {name}")
    return values[name]

def eq(name, expected):
    actual = req(name)
    if actual != expected:
        raise SystemExit(
            f"FAIL: {name}={actual}, expected {expected}"
        )

def zero(name):
    eq(name, 0)

eq("magic", 0x52325736)
eq("control_task_created", 1)
eq("processing_task_created", 1)
eq("phase", 5)
eq("test_pass", 1)
zero("fault_bits")
eq("system_core_clock", 180000000)
eq("configured_k", k)
eq("drop_mode", 1 if mode == "DROP" else 0)
eq("process_hold_blocks", k + 5 if mode == "DROP" else 0)
eq("start_status", 0)
eq("stop_status", 0)
eq("irq_count", 96)
eq("input_count", 96)

admitted = req("admitted_count")
drops = req("capacity_drop_count")
processed = req("processed_count")
released = req("released_count")
recoveries = req("recovered_admission_after_drop_count")
max_drop_streak = req("max_drop_streak")

if admitted + drops != 96:
    raise SystemExit(
        f"FAIL: admitted+drops={admitted+drops}, expected 96"
    )

if processed != admitted:
    raise SystemExit(
        f"FAIL: processed={processed}, admitted={admitted}"
    )

if released != admitted:
    raise SystemExit(
        f"FAIL: released={released}, admitted={admitted}"
    )

eq("init_hook_count", k)
eq("completion_hook_count", admitted)

for name in (
    "illegal_free_send_count",
    "ready_send_fail_count",
    "notification_fail_count",
    "token_ledger_errors",
    "dma_error_flags_seen",
    "adc_ovr_seen",
    "sample_errors",
    "canary_errors",
    "pool_violation_count",
    "slots_violation_count",
):
    zero(name)

decision = req("max_nominal_to_decision_cycles")
irq_exit = req("max_nominal_to_irq_exit_cycles")
final_window = req("max_final_window_cycles")

if decision > 57600:
    raise SystemExit(
        f"FAIL: max decision cycles {decision} > 57600"
    )

if irq_exit > 80640:
    raise SystemExit(
        f"FAIL: max IRQ-exit cycles {irq_exit} > 80640"
    )

if final_window > 3600:
    raise SystemExit(
        f"FAIL: max final window {final_window} > 3600"
    )

eq("full_sample_count", admitted * 256)

eq("quiet_irq_count", 96)
eq("quiet_input_count", 96)
eq("quiet_admitted_count", admitted)
eq("quiet_drop_count", drops)
eq("quiet_processed_count", processed)

eq("free_queue_depth_final", k)
eq("ready_queue_depth_final", 0)

eq("pool_activated", 1)
eq("pool_k", k)
eq("pool_active_count", k + 2)
eq("pool_inactive_count", 10 - (k + 2))
eq("pool_free_count", k)
eq("pool_dma_owned_count", 2)
eq("pool_ready_count", 0)
eq("pool_processing_count", 0)

eq("slots_initialized", 1)

admit_sum = 0
drop_sum = 0
processed_sum = 0
released_sum = 0

for i in range(10):
    admit_sum += req(f"buffer{i}_admitted")
    drop_sum += req(f"buffer{i}_dropped")
    processed_sum += req(f"buffer{i}_processed")
    released_sum += req(f"buffer{i}_released")

if admit_sum != admitted:
    raise SystemExit(
        f"FAIL: per-buffer admitted sum {admit_sum} != {admitted}"
    )

if drop_sum != drops:
    raise SystemExit(
        f"FAIL: per-buffer drop sum {drop_sum} != {drops}"
    )

if processed_sum != processed:
    raise SystemExit(
        f"FAIL: per-buffer processed sum {processed_sum} != {processed}"
    )

if released_sum != released:
    raise SystemExit(
        f"FAIL: per-buffer released sum {released_sum} != {released}"
    )

active_states = [req(f"pool_state_{i}") for i in range(k + 2)]
inactive_states = [req(f"pool_state_{i}") for i in range(k + 2, 10)]

if active_states.count(1) != k or active_states.count(2) != 2:
    raise SystemExit(
        "FAIL: final active pool states do not contain "
        f"{k} FREE and 2 DMA_OWNED: {active_states}"
    )

if any(state != 0 for state in inactive_states):
    raise SystemExit(
        f"FAIL: inactive pool states are not all INACTIVE: {inactive_states}"
    )

if mode == "NORMAL":
    if admitted != 96 or drops != 0:
        raise SystemExit(
            f"FAIL: NORMAL accounting admitted={admitted}, drops={drops}"
        )
    if recoveries != 0 or max_drop_streak != 0:
        raise SystemExit(
            f"FAIL: NORMAL drop metrics recoveries={recoveries}, "
            f"max_drop_streak={max_drop_streak}"
        )
else:
    if admitted < k + 3:
        raise SystemExit(
            f"FAIL: DROP admissions {admitted} < {k+3}"
        )
    if drops < 8:
        raise SystemExit(
            f"FAIL: DROP count {drops} < 8"
        )
    if max_drop_streak < 3:
        raise SystemExit(
            f"FAIL: max drop streak {max_drop_streak} < 3"
        )
    if recoveries < 3:
        raise SystemExit(
            f"FAIL: recoveries {recoveries} < 3"
        )

print(f"K={k} mode={mode}")
print("Input events:                96")
print(f"Admissions:                  {admitted}")
print(f"Capacity drops:              {drops}")
print(f"Processed:                   {processed}")
print(f"Released:                    {released}")
print(f"Recovered admissions:        {recoveries}")
print(f"Max DROP streak:             {max_drop_streak}")
print(f"Max decision cycles:         {decision}")
print(f"Max IRQ-exit cycles:         {irq_exit}")
print(f"Max final window:            {final_window}")
print(f"Final FREE buffers:          {k}")
print("Final DMA_OWNED buffers:     2")
print("DMA/ADC/ownership/token:     PASS")
print("Sample/canary validation:    PASS")
print("Quiet window:                PASS")
print("Summary acceptance:          PASS")
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

Write-Output "`n=== SUMMARY VALIDATOR SYNTAX CHECK ==="

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
    throw 'Summary validator syntax check failed. No GDB operation was started.'
}

$pycache =
    Join-Path $Build '__pycache__'

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

Write-Output "`n=== ATTACH AND READ EXISTING RAM RESULT ==="
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

    $rawLines |
        ForEach-Object {
            Write-Output $_
        }

    [System.IO.File]::WriteAllLines(
        $inspection,
        $rawLines,
        $utf8NoBom
    )

    Write-Output "`nGDB exit code: $gdbRc"
    Write-Output "Inspection evidence: $inspection"

    if ($gdbRc -ne 0) {
        throw "GDB summary inspection failed with exit code $gdbRc."
    }
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

Write-Output "`n=== AUTOMATED SUMMARY ACCEPTANCE ==="

$oldPreference =
    $ErrorActionPreference

try {
    $ErrorActionPreference = 'Continue'

    $validationOutput = @(
        & $python `
            $validator `
            $inspection `
            $K `
            $Mode `
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

Write-Output "Summary validator exit code: $validationRc"

if ($validationRc -ne 0) {
    throw "Hardware summary acceptance failed with exit code $validationRc."
}

Write-Output "`n=== POST-RUN REPOSITORY IDENTITY ==="

$postHead =
    (git rev-parse HEAD).Trim()

Write-Output "HEAD: $postHead"
git status --short --branch

if ($postHead -ne $expectedHead) {
    throw 'HEAD changed during run/summary capture.'
}

Write-Output ''
Write-Output "R2-W6 K=$K $Mode CONTROLLED RUN + SUMMARY: PASS"
Write-Output 'Trace evidence: NOT YET EXPORTED'
