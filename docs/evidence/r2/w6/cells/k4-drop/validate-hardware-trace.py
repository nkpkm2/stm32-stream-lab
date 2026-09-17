import csv
import re
import sys
from pathlib import Path

trace_path = Path(sys.argv[1])
inspection_path = Path(sys.argv[2])
k = int(sys.argv[3])
mode = sys.argv[4].upper()
active_count = k + 2

text = inspection_path.read_text(encoding="utf-8", errors="replace")
summary = {}
for line in text.splitlines():
    m = re.fullmatch(r"([A-Za-z0-9_]+)=(0x[0-9A-Fa-f]+|[0-9]+)", line.strip())
    if m:
        summary[m.group(1)] = int(m.group(2), 0)

def req(name):
    if name not in summary:
        raise SystemExit(f"FAIL: summary missing {name}")
    return summary[name]

def eq(name, expected):
    actual = req(name)
    if actual != expected:
        raise SystemExit(f"FAIL: summary {name}={actual}, expected {expected}")

def zero(name):
    eq(name, 0)

eq("magic", 0x52325736)
eq("phase", 5)
eq("test_pass", 1)
zero("fault_bits")
eq("configured_k", k)
eq("drop_mode", 1 if mode == "DROP" else 0)
eq("process_hold_blocks", k + 5 if mode == "DROP" else 0)
eq("input_count", 96)

admitted = req("admitted_count")
drops = req("capacity_drop_count")
processed = req("processed_count")
released = req("released_count")
recoveries = req("recovered_admission_after_drop_count")
summary_max_streak = req("max_drop_streak")

if admitted + drops != 96:
    raise SystemExit(f"FAIL: summary admitted+drops={admitted+drops}, expected 96")
if processed != admitted or released != admitted:
    raise SystemExit(
        f"FAIL: summary round trip admitted={admitted}, processed={processed}, released={released}"
    )

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

summary_decision = req("max_nominal_to_decision_cycles")
summary_irq = req("max_nominal_to_irq_exit_cycles")
summary_final = req("max_final_window_cycles")
if summary_decision > 57600 or summary_irq > 80640 or summary_final > 3600:
    raise SystemExit("FAIL: summary timing budget violation")

eq("full_sample_count", admitted * 256)
eq("quiet_irq_count", 96)
eq("quiet_input_count", 96)
eq("quiet_admitted_count", admitted)
eq("quiet_drop_count", drops)
eq("quiet_processed_count", processed)
eq("free_queue_depth_final", k)
eq("ready_queue_depth_final", 0)
eq("ready_token_mask_final", 0)
eq("pool_k", k)
eq("pool_active_count", active_count)
eq("pool_inactive_count", 10 - active_count)
eq("pool_free_count", k)
eq("pool_dma_owned_count", 2)
eq("pool_ready_count", 0)
eq("pool_processing_count", 0)
eq("slots_mapping_epoch", admitted + 1)

if mode == "NORMAL":
    if admitted != 96 or drops != 0 or recoveries != 0 or summary_max_streak != 0:
        raise SystemExit(
            f"FAIL: NORMAL summary admitted={admitted}, drops={drops}, "
            f"recoveries={recoveries}, max_streak={summary_max_streak}"
        )
else:
    if admitted < k + 3:
        raise SystemExit(f"FAIL: DROP admissions {admitted} < {k+3}")
    if drops < 8:
        raise SystemExit(f"FAIL: DROP count {drops} < 8")
    if recoveries < 3:
        raise SystemExit(f"FAIL: DROP recoveries {recoveries} < 3")
    if summary_max_streak < 3:
        raise SystemExit(f"FAIL: DROP max streak {summary_max_streak} < 3")

with trace_path.open("r", encoding="utf-8", newline="") as f:
    rows = list(csv.DictReader(f))

if len(rows) != 96:
    raise SystemExit(f"FAIL: expected 96 trace rows, found {len(rows)}")

def u(row, name):
    return int(row[name], 0)

trace_admit = 0
trace_drop = 0
current_drop_streak = 0
trace_max_streak = 0
trace_recoveries = 0
previous_epoch = None
max_decision = 0
max_irq = 0
max_final = 0
admit_by_buffer = [0] * active_count
drop_by_buffer = [0] * active_count
processed_by_buffer = [0] * active_count

for i, row in enumerate(rows):
    seq = u(row, "sequence")
    decision = u(row, "decision")
    ct = u(row, "ct_entry")
    completed_slot = u(row, "completed_slot")
    completed_id = u(row, "completed_id")
    replacement_id = u(row, "replacement_id")
    ndtr = u(row, "ndtr_guard")
    free_depth = u(row, "free_depth_after_take")
    decision_cycles = u(row, "nominal_to_decision_cycles")
    irq_cycles = u(row, "nominal_to_irq_exit_cycles")
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
        raise SystemExit(f"FAIL: sequence mismatch at row {i+1}: {seq}")
    expected_ct = seq & 1
    if ct != expected_ct:
        raise SystemExit(f"FAIL: CT mismatch at sequence {seq}")
    if completed_slot != (ct ^ 1):
        raise SystemExit(f"FAIL: completed-slot mismatch at sequence {seq}")
    if completed_id >= active_count:
        raise SystemExit(
            f"FAIL: completed buffer {completed_id} outside active range 0..{active_count-1}"
        )
    if not (192 <= ndtr <= 256):
        raise SystemExit(f"FAIL: NDTR={ndtr} at sequence {seq}")
    if decision_cycles > 57600 or irq_cycles > 80640 or final_cycles > 3600:
        raise SystemExit(f"FAIL: timing violation at sequence {seq}")

    max_decision = max(max_decision, decision_cycles)
    max_irq = max(max_irq, irq_cycles)
    max_final = max(max_final, final_cycles)

    if decision == 1:
        trace_admit += 1
        admit_by_buffer[completed_id] += 1
        processed_by_buffer[completed_id] += 1

        if replacement_id >= active_count:
            raise SystemExit(
                f"FAIL: replacement {replacement_id} outside active range at sequence {seq}"
            )
        if processed_ok != 1:
            raise SystemExit(f"FAIL: ADMIT not processed at sequence {seq}")
        if processing_begin == 0 or processing_end == 0 or release_commit == 0:
            raise SystemExit(f"FAIL: ADMIT lacks processing/release timestamps at sequence {seq}")
        if raw_min > raw_max or raw_max > 4095:
            raise SystemExit(f"FAIL: raw ADC range invalid at sequence {seq}")

        if completed_slot == 0:
            if m0_after == m0_before or m1_after != m1_before:
                raise SystemExit(f"FAIL: inactive-slot M0 rebind invariant at sequence {seq}")
        else:
            if m1_after == m1_before or m0_after != m0_before:
                raise SystemExit(f"FAIL: inactive-slot M1 rebind invariant at sequence {seq}")

        if previous_epoch is not None and epoch != previous_epoch + 1:
            raise SystemExit(f"FAIL: mapping epoch did not advance on ADMIT at sequence {seq}")

        if current_drop_streak:
            trace_recoveries += 1
            current_drop_streak = 0

    elif decision == 2:
        trace_drop += 1
        drop_by_buffer[completed_id] += 1
        current_drop_streak += 1
        trace_max_streak = max(trace_max_streak, current_drop_streak)

        if replacement_id != 255:
            raise SystemExit(f"FAIL: DROP replacement_id={replacement_id} at sequence {seq}")
        if m0_after != m0_before or m1_after != m1_before:
            raise SystemExit(f"FAIL: DROP changed M0AR/M1AR at sequence {seq}")
        if previous_epoch is None or epoch != previous_epoch:
            raise SystemExit(f"FAIL: DROP changed mapping epoch at sequence {seq}")
        if processed_ok != 0:
            raise SystemExit(f"FAIL: DROP entered Processing at sequence {seq}")
        if processing_begin != 0 or processing_end != 0 or release_commit != 0:
            raise SystemExit(f"FAIL: DROP has processing/release timestamps at sequence {seq}")
        if free_depth != 0:
            raise SystemExit(f"FAIL: DROP without exhausted FreeBufferQueue at sequence {seq}")
    else:
        raise SystemExit(f"FAIL: unknown decision {decision} at sequence {seq}")

    previous_epoch = epoch

if trace_admit != admitted or trace_drop != drops:
    raise SystemExit(
        f"FAIL: trace/summary counts trace=({trace_admit},{trace_drop}) "
        f"summary=({admitted},{drops})"
    )
if trace_recoveries != recoveries:
    raise SystemExit(
        f"FAIL: trace recoveries {trace_recoveries} != summary {recoveries}"
    )
if trace_max_streak != summary_max_streak:
    raise SystemExit(
        f"FAIL: trace max streak {trace_max_streak} != summary {summary_max_streak}"
    )
if max_decision != summary_decision or max_irq != summary_irq or max_final != summary_final:
    raise SystemExit(
        "FAIL: trace timing maxima do not match summary: "
        f"trace=({max_decision},{max_irq},{max_final}) "
        f"summary=({summary_decision},{summary_irq},{summary_final})"
    )

for i in range(active_count):
    a_name = f"buffer{i}_admitted"
    d_name = f"buffer{i}_dropped"
    p_name = f"buffer{i}_processed"
    r_name = f"buffer{i}_released"
    if all(name in summary for name in (a_name, d_name, p_name, r_name)):
        if summary[a_name] != admit_by_buffer[i]:
            raise SystemExit(f"FAIL: {a_name} summary={summary[a_name]} trace={admit_by_buffer[i]}")
        if summary[d_name] != drop_by_buffer[i]:
            raise SystemExit(f"FAIL: {d_name} summary={summary[d_name]} trace={drop_by_buffer[i]}")
        if summary[p_name] != processed_by_buffer[i]:
            raise SystemExit(f"FAIL: {p_name} summary={summary[p_name]} trace={processed_by_buffer[i]}")
        if summary[r_name] != processed_by_buffer[i]:
            raise SystemExit(f"FAIL: {r_name} summary={summary[r_name]} trace={processed_by_buffer[i]}")

active_states = [req(f"pool_state_{i}") for i in range(active_count)]
if active_states.count(1) != k or active_states.count(2) != 2:
    raise SystemExit(
        f"FAIL: final active states {active_states}; expected {k} FREE and 2 DMA_OWNED"
    )
if any(state not in (1, 2) for state in active_states):
    raise SystemExit(
        f"FAIL: active pool states contain unexpected value: {active_states}"
    )

print(f"K={k} mode={mode}")
print("Rows:                         96")
print(f"ADMIT decisions:              {trace_admit}")
print(f"DROP decisions:               {trace_drop}")
print(f"Max DROP streak:              {trace_max_streak}")
print(f"Recovered admissions:         {trace_recoveries}")
print(f"ADMIT by active buffer:       {admit_by_buffer}")
print(f"DROP by active buffer:        {drop_by_buffer}")
print(f"Max decision cycles:          {max_decision}")
print(f"Max IRQ-exit cycles:          {max_irq}")
print(f"Max final window:             {max_final}")
print("CT/completed-slot sequence:    96 / 96 PASS")
print("Active buffer ID range:       96 / 96 PASS")
print("Trace vs summary accounting:   PASS")
if mode == "NORMAL":
    print("NORMAL decisions:              96 / 96 ADMIT")
    print("Inactive-slot-only rebind:     96 / 96 PASS")
    print("Processing/release evidence:   96 / 96 PASS")
else:
    print(f"DROP M0AR/M1AR invariance:      {trace_drop} / {trace_drop} PASS")
    print(f"DROP mapping-epoch invariance:  {trace_drop} / {trace_drop} PASS")
    print(f"DROP Processing exclusion:      {trace_drop} / {trace_drop} PASS")
    print(f"DROP FreeQueue exhaustion:      {trace_drop} / {trace_drop} PASS")
    print(f"ADMIT inactive-slot rebind:     {trace_admit} / {trace_admit} PASS")
    print(f"ADMIT Processing/release:       {trace_admit} / {trace_admit} PASS")
print("Timing budgets:                 PASS")
print("Final ownership:                PASS")
print("TRACE ACCEPTANCE:               PASS")