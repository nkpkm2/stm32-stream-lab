# RC3 diagnostic attempt 0001

This immutable attempt completed its 65-second target run and readback with
50,796 balanced COMPLETE events. Its diagnostic witness export is invalid:
the snapshot code was compiled only for case ID 12, while COMMIT_BUDGET is
case ID 5. The raw timing population remains preserved, but this diagnostic
attempt cannot answer the same-event root-cause question and is not formal
T04 acceptance.

No automatic retry was performed. `classification.json` records the offline,
source-proven failure classification without modifying the captured H0-H5
files.
