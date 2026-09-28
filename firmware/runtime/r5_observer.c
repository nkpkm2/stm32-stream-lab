#include "r5_observer.h"
#include <string.h>

static int Match(const R5Observer *o, const R5ObserverReceipt *r)
{
    return o != NULL && r != NULL && o->active != 0U &&
        r->boot_id == o->metrics.config.boot_id &&
        r->run_id == o->metrics.config.run_id &&
        r->generation == o->metrics.config.generation;
}

static R5MetricsStatus Reject(R5Observer *o, R5MetricsStatus reason)
{
    if ((o != NULL) && (o->active != 0U))
    {
        (void)R5RunMetrics_Invalidate(&o->metrics, reason);
    }
    return reason;
}

R5MetricsStatus R5Observer_Begin(R5Observer *o, const R5MetricsConfig *c)
{
    R5MetricsStatus s;
    if (o == NULL || c == NULL) return R5_METRICS_INVALID_ARGUMENT;
    (void)memset(o, 0, sizeof(*o));
    s = R5RunMetrics_Initialize(&o->metrics, c);
    if (s == R5_METRICS_OK) o->active = 1U;
    return s;
}

R5MetricsStatus R5Observer_Drain(R5Observer *o, uint64_t before)
{
    uint32_t i, pick;
    uint64_t best;
    R5MetricsStatus s;
    if (o == NULL || o->active == 0U) return R5_METRICS_INVALID_STATE;
    for (;;) {
        pick = R5_METRICS_MAX_COHORT_BLOCKS;
        best = UINT64_MAX;
        for (i = 0U; i < R5_METRICS_MAX_COHORT_BLOCKS; ++i) {
            if (o->complete[i].captured != 0U &&
                o->complete[i].receipt.serial < before &&
                o->complete[i].receipt.serial < best) { pick = i; best = o->complete[i].receipt.serial; }
        }
        if (pick == R5_METRICS_MAX_COHORT_BLOCKS) return R5_METRICS_OK;
        s = R5RunMetrics_OnCompletionWithDiagnostics(&o->metrics, &o->diagnostics,
            o->complete[pick].receipt.sequence, o->complete[pick].receipt.time,
            o->complete[pick].receipt.serial);
        o->complete[pick].captured = 0U;
        if (s != R5_METRICS_OK && s != R5_METRICS_OBSERVATION_CLOSED) return s;
    }
}

R5MetricsStatus R5Observer_OnInputReceipt(R5Observer *o, const R5ObserverReceipt *r)
{
    R5MetricsStatus s;
    if (!Match(o, r) || r->operation != R5_OBSERVER_INPUT)
        return Reject(o, R5_METRICS_INVALID_ARGUMENT);
    s = R5Observer_Drain(o, r->serial);
    if (s != R5_METRICS_OK) return s;
    return R5RunMetrics_OnInputBoundary(&o->metrics, r->sequence, r->time, r->serial);
}

R5MetricsStatus R5Observer_OnAdmission(R5Observer *o, uint32_t seq,
    uint32_t free_available, uint32_t occupancy)
{
    if (o == NULL || o->active == 0U) return R5_METRICS_INVALID_STATE;
    return R5RunMetrics_OnAdmissionDecision(&o->metrics, seq, free_available, occupancy);
}

R5MetricsStatus R5Observer_ArmComplete(R5Observer *o, uint32_t seq)
{
    if (o == NULL || o->active == 0U || o->armed_sequence != 0U ||
        seq == UINT32_MAX) return Reject(o, R5_METRICS_INVALID_STATE);
    o->armed_sequence = seq + 1U;
    return R5_METRICS_OK;
}

R5MetricsStatus R5Observer_CaptureComplete(R5Observer *o, const R5ObserverReceipt *r)
{
    uint32_t i;
    if (!Match(o, r) || r->operation != R5_OBSERVER_COMPLETE ||
        o->armed_sequence == 0U || r->sequence == UINT32_MAX ||
        r->sequence + 1U != o->armed_sequence)
        return Reject(o, R5_METRICS_INVALID_STATE);
    o->armed_sequence = 0U;
    if (r->sequence < o->metrics.config.s0 || r->sequence >= o->metrics.config.s1) return R5_METRICS_OK;
    i = r->sequence - o->metrics.config.s0;
    if (o->complete[i].captured != 0U)
        return Reject(o, R5_METRICS_DUPLICATE_COMPLETION);
    o->complete[i].receipt = *r;
    o->complete[i].captured = 1U;
    return R5_METRICS_OK;
}

uint32_t R5Observer_RequiresAdmission(const R5Observer *o, uint32_t sequence)
{
    return (o != NULL) ? R5RunMetrics_RequiresAdmissionDecision(&o->metrics,
        sequence) : 0U;
}
