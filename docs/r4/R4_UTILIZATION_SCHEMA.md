# R4 utilization and runtime-report schema

Status: frozen R4 reporting contract.  The values are residency/accounting
measurements for a fixed instrumentation profile, not claims of uninstrumented
physical CPU time.

## Formal input

Only `R4_RuntimeLedger_GetSealedWindowSummary()` may supply formal CPU-window
report values.  It rejects an open or never-closed window and uses only the
sealed window buckets.  It never combines whole-run live counters with a
formal denominator.

| Field | Definition |
|---|---|
| `window_cycles` | Formal CPU wall interval `[S0, S1)` in Clock64 cycles. |
| `task_cycles` | Task-owned residency inside that interval. |
| `irq_cycles` | Instrumented IRQ-owned residency inside that interval. |
| `idle_cycles` | Idle-owned residency inside that interval. |
| `unclassified_cycles` | Explicit platform/residual ownership not attributed to task, IRQ, or Idle. |
| `attributed_non_idle_cycles` | `task_cycles + irq_cycles`. |
| `occupied_non_idle_cycles` | `task_cycles + irq_cycles + unclassified_cycles`. |

The API rejects a report unless:

```text
window_cycles == task_cycles + irq_cycles + idle_cycles + unclassified_cycles
```

## Frozen ratio names

For `window_cycles > 0`:

```text
U_attributed_non_idle = attributed_non_idle_cycles / window_cycles
U_occupied_non_idle   = occupied_non_idle_cycles / window_cycles
U_idle                 = idle_cycles / window_cycles
U_residual             = unclassified_cycles / window_cycles
```

`U_attributed_non_idle` is the utilization number for work whose ownership is
known.  `U_occupied_non_idle` adds explicit residual/platform occupancy; it is
not silently presented as task or IRQ utilization.  A report must label which
of these ratios it uses.  No STOP-time whole-run counter may substitute for
these numerator values or the formal denominator.

## Evidence and limits

The schema and rejection behavior are native-tested in
`test_r4_runtime_event.c`; sealed target `window-intersection/attempt-0001`
establishes target partition conservation and post-CLOSE immutability.  This
does not by itself establish a known wall-response workload or quantify the
R4-on versus minimal-instrumentation perturbation.
