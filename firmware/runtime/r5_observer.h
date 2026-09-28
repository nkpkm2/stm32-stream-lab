#ifndef R5_OBSERVER_H
#define R5_OBSERVER_H

#include "r5_run_metrics.h"

typedef enum {
    R5_OBSERVER_INPUT = 0,
    R5_OBSERVER_COMPLETE = 1
} R5ObserverOperation;

typedef struct {
    uint32_t boot_id, run_id, generation, sequence;
    uint64_t time, serial;
    R5ObserverOperation operation;
} R5ObserverReceipt;

typedef struct {
    uint32_t armed, captured;
    R5ObserverReceipt receipt;
} R5ObserverCompletionSlot;

typedef struct {
    R5RunMetrics metrics;
    R5LiveDiagnostics diagnostics;
    uint32_t active;
    uint32_t armed_sequence;
    R5ObserverCompletionSlot complete[R5_METRICS_MAX_COHORT_BLOCKS];
} R5Observer;

R5MetricsStatus R5Observer_Begin(R5Observer *observer,
    const R5MetricsConfig *config);
R5MetricsStatus R5Observer_OnInputReceipt(R5Observer *observer,
    const R5ObserverReceipt *receipt);
R5MetricsStatus R5Observer_OnAdmission(R5Observer *observer,
    uint32_t sequence, uint32_t free_available, uint32_t occupancy);
R5MetricsStatus R5Observer_ArmComplete(R5Observer *observer,
    uint32_t sequence);
R5MetricsStatus R5Observer_CaptureComplete(R5Observer *observer,
    const R5ObserverReceipt *receipt);
R5MetricsStatus R5Observer_Drain(R5Observer *observer, uint64_t before_serial);
uint32_t R5Observer_RequiresAdmission(const R5Observer *observer,
    uint32_t sequence);

#endif
