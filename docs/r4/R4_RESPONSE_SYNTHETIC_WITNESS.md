# R4 synthetic response witness

`response-synthetic` is the directed R4 target case for a known task response
interval.  It supplements, rather than replaces, the existing task-synthetic,
IRQ, and window evidence.

The harness creates a static worker at a priority above its coordinator.  The
worker first blocks on a direct task notification.  The coordinator records a
raw `DWT->CYCCNT` release endpoint immediately before `xTaskNotifyGive()`.
The unblocked worker records direct raw DWT start and completion endpoints
around a fixed 50,000-iteration volatile arithmetic workload.  These three
wall endpoints do not use `Clock64`, `RuntimeEvent`, or ledger fields.

The worker places a RuntimeEvent checkpoint after its direct start observation
and after its direct completion observation.  Once the formal CPU window has
closed, the harness reads the sealed owner bucket for that exact worker task.
The evidence reader accepts a run only when:

- the worker was created and completed;
- direct release-to-completion and start-to-completion intervals are positive,
  ordered, and below the fixed 5,000,000-cycle directed-case bound; and
- the sealed worker owner bucket contains at least the independently observed
  fixed-work interval.

This is a controlled accounting interpretation check, not a claim about DSP
end-to-end latency or a production response-time limit.  Raw DWT endpoints are
only valid for this short interval and deliberately use modulo-32-bit
subtraction; the bound is far below one DWT wrap period.
