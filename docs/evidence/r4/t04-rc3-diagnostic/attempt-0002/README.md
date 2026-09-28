# RC3 diagnostic attempt 0002

This is the one explicitly authorized corrected RC3 hardware diagnostic
attempt. It preserves attempt 0001 unchanged and fixes only its proven export
selector defect (COMMIT_BUDGET is case ID 5, not 12).

The target completed 50,796 balanced COMPLETE events. The exported witness is
valid and is the event that created `max_full`: ordinal 20, operation COMPLETE,
full/prefix/suffix `1904/1286/618` cycles, flags `0x1f`, and zero consistency
failures. `1904 = 1286 + 618` for that same event.

This is diagnostic evidence, not formal T04 acceptance. It neither replaces
nor weakens the immutable production observation of 1919 cycles over the
1800-cycle target. No automatic retry was performed.
