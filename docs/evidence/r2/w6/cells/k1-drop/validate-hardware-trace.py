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

admit_count = 0
drop_count = 0
current_drop_streak = 0
max_drop_streak = 0
recovery_count = 0
previous_epoch = None
max_decision = 0
max_irq_exit = 0
max_final = 0
admitted_by_buffer = [0, 0, 0]
dropped_by_buffer = [0, 0, 0]

for i, row in enumerate(rows):
    seq = u(row, "sequence")
    decision = u(row, "decision")
    ct = u(row, "ct_entry")
    completed_slot = u(row, "completed_slot")
    completed_id = u(row, "completed_id")
    replacement_id = u(row, "replacement_id")
    ndtr = u(row, "ndtr_guard")
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
    free_depth = u(row, "free_depth_after_take")

    if seq != i + 1:
        raise SystemExit(f"FAIL: sequence mismatch at CSV row {i+1}: {seq}")

    expected_ct = seq & 1
    if ct != expected_ct:
        raise SystemExit(
            f"FAIL: CT alternation mismatch at sequence {seq}: "
            f"{ct} != {expected_ct}"
        )

    expected_completed_slot = ct ^ 1
    if completed_slot != expected_completed_slot:
        raise SystemExit(
            f"FAIL: completed-slot mismatch at sequence {seq}"
        )

    if completed_id >= 3:
        raise SystemExit(
            f"FAIL: inactive buffer entered active K=1 cell at sequence {seq}"
        )

    if not (192 <= ndtr <= 256):
        raise SystemExit(
            f"FAIL: NDTR guard violation at sequence {seq}: {ndtr}"
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

    if decision == 1:
        admit_count += 1
        admitted_by_buffer[completed_id] += 1

        if replacement_id >= 3:
            raise SystemExit(
                f"FAIL: invalid replacement buffer on ADMIT at "
                f"sequence {seq}: {replacement_id}"
            )

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
                f"FAIL: ADMIT lacks Processing/release timestamps "
                f"at sequence {seq}"
            )

        if raw_min > raw_max or raw_max > 4095:
            raise SystemExit(
                f"FAIL: invalid ADC range on ADMIT at sequence {seq}"
            )

        if completed_slot == 0:
            if m0_after == m0_before:
                raise SystemExit(
                    f"FAIL: ADMIT did not replace inactive M0AR "
                    f"at sequence {seq}"
                )
            if m1_after != m1_before:
                raise SystemExit(
                    f"FAIL: ADMIT modified active M1AR "
                    f"at sequence {seq}"
                )
        else:
            if m1_after == m1_before:
                raise SystemExit(
                    f"FAIL: ADMIT did not replace inactive M1AR "
                    f"at sequence {seq}"
                )
            if m0_after != m0_before:
                raise SystemExit(
                    f"FAIL: ADMIT modified active M0AR "
                    f"at sequence {seq}"
                )

        if previous_epoch is not None and epoch != previous_epoch + 1:
            raise SystemExit(
                f"FAIL: mapping epoch did not advance exactly once "
                f"on ADMIT at sequence {seq}"
            )

        if current_drop_streak > 0:
            recovery_count += 1
            current_drop_streak = 0

    elif decision == 2:
        drop_count += 1
        dropped_by_buffer[completed_id] += 1
        current_drop_streak += 1
        max_drop_streak = max(max_drop_streak, current_drop_streak)

        if replacement_id != 255:
            raise SystemExit(
                f"FAIL: DROP has replacement buffer at sequence {seq}: "
                f"{replacement_id}"
            )

        if m0_after != m0_before or m1_after != m1_before:
            raise SystemExit(
                f"FAIL: DROP changed M0AR/M1AR at sequence {seq}"
            )

        if previous_epoch is None:
            raise SystemExit(
                "FAIL: first matrix event unexpectedly classified as DROP"
            )

        if epoch != previous_epoch:
            raise SystemExit(
                f"FAIL: DROP advanced mapping epoch at sequence {seq}"
            )

        if processed_ok != 0:
            raise SystemExit(
                f"FAIL: dropped block entered Processing at sequence {seq}"
            )

        if (
            processing_begin != 0
            or processing_end != 0
            or release_commit != 0
        ):
            raise SystemExit(
                f"FAIL: DROP contains Processing/release timestamps "
                f"at sequence {seq}"
            )

        if free_depth != 0:
            raise SystemExit(
                f"FAIL: DROP occurred without exhausted FreeBufferQueue "
                f"at sequence {seq}"
            )
    else:
        raise SystemExit(
            f"FAIL: unknown decision={decision} at sequence {seq}"
        )

    previous_epoch = epoch

checks = [
    (admit_count == 14, f"admit count {admit_count} != 14"),
    (drop_count == 82, f"drop count {drop_count} != 82"),
    (
        max_drop_streak == 6,
        f"max drop streak {max_drop_streak} != 6",
    ),
    (
        recovery_count == 13,
        f"recovery count {recovery_count} != 13",
    ),
    (
        admitted_by_buffer == [5, 5, 4],
        f"admitted_by_buffer={admitted_by_buffer}",
    ),
    (
        dropped_by_buffer == [26, 27, 29],
        f"dropped_by_buffer={dropped_by_buffer}",
    ),
    (
        max_decision == 6808,
        f"max decision {max_decision} != 6808",
    ),
    (
        max_irq_exit == 11833,
        f"max IRQ exit {max_irq_exit} != 11833",
    ),
    (
        max_final == 1099,
        f"max final window {max_final} != 1099",
    ),
]

for ok, message in checks:
    if not ok:
        raise SystemExit("FAIL: " + message)

print("Rows:                 96")
print(f"ADMIT decisions:      {admit_count}")
print(f"DROP decisions:       {drop_count}")
print(f"Max DROP streak:      {max_drop_streak}")
print(f"Recoveries:           {recovery_count}")
print(
    "ADMIT by buffer:      "
    + ", ".join(map(str, admitted_by_buffer))
)
print(
    "DROP by buffer:       "
    + ", ".join(map(str, dropped_by_buffer))
)
print(f"Max decision cycles:  {max_decision}")
print(f"Max IRQ-exit cycles:  {max_irq_exit}")
print(f"Max final window:     {max_final}")
print("")
print("DROP M0AR/M1AR invariance:       82 / 82 PASS")
print("DROP mapping-epoch invariance:   82 / 82 PASS")
print("DROP Processing exclusion:       82 / 82 PASS")
print("DROP FreeQueue exhaustion:       82 / 82 PASS")
print("ADMIT inactive-slot write only:  14 / 14 PASS")
print("ADMIT Processing/release:        14 / 14 PASS")
print("Trace vs summary accounting:     PASS")