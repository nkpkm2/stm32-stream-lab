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