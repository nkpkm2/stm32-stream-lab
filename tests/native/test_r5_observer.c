#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "r5_observer.h"
#define C(x) do { if (!(x)) { fprintf(stderr, "fail:%d\n", __LINE__); exit(1); } } while (0)
static R5MetricsConfig cfg(void) { R5MetricsConfig c = {1U,2U,3U,0U,2U,2U,0U,100U,160U,10U}; return c; }
static void begin(R5Observer *o) { R5MetricsConfig c = cfg(); C(R5Observer_Begin(o,&c) == R5_METRICS_OK); }
static R5ObserverReceipt r(uint32_t seq, uint64_t time, uint64_t serial) {
    R5ObserverReceipt x = {1U,2U,3U,seq,time,serial,0}; return x;
}
static void input(R5Observer *o, uint32_t seq, uint64_t time, uint64_t serial, uint32_t free) {
    R5ObserverReceipt x = r(seq,time,serial); C(R5Observer_OnInputReceipt(o,&x)==R5_METRICS_OK); C(R5Observer_OnAdmission(o,seq,free,0U)==R5_METRICS_OK);
}
static void CaseOrder(uint32_t completion_first) {
    R5Observer o; R5ObserverReceipt x;
    begin(&o);
    input(&o,0U,1U,1U,1U); input(&o,1U,2U,2U,1U); input(&o,2U,3U,3U,1U); input(&o,3U,4U,4U,1U);
    C(R5Observer_ArmComplete(&o,0U)==R5_METRICS_OK);
    x=r(0U,371U,completion_first ? 5U : 6U); x.operation=R5_OBSERVER_COMPLETE;
    C(R5Observer_CaptureComplete(&o,&x)==R5_METRICS_OK);
    x=r(4U,371U,completion_first ? 6U : 5U);
    C(R5Observer_OnInputReceipt(&o,&x)==R5_METRICS_OK);
    C(o.metrics.cohort_outcome[0] == (completion_first ? R5_METRICS_OUTCOME_LATE : R5_METRICS_OUTCOME_EXPIRED_UNRESOLVED));
}
int main(int argc,char **argv) {
    R5Observer o; R5ObserverReceipt x;
    if(argc!=2) return 1;
    if(!strcmp(argv[1],"completion_first")) CaseOrder(1U);
    else if(!strcmp(argv[1],"cutoff_first")) CaseOrder(0U);
    else if(!strcmp(argv[1],"identity")) { begin(&o); x=r(0,1,1); x.run_id=99; C(R5Observer_OnInputReceipt(&o,&x)==R5_METRICS_INVALID_ARGUMENT); C(o.metrics.phase==R5_METRICS_INVALID); }
    else if(!strcmp(argv[1],"duplicate")) { begin(&o); input(&o,0,1,1,1); C(R5Observer_ArmComplete(&o,0)==R5_METRICS_OK); x=r(0,100,2); x.operation=R5_OBSERVER_COMPLETE; C(R5Observer_CaptureComplete(&o,&x)==R5_METRICS_OK); C(R5Observer_ArmComplete(&o,0)==R5_METRICS_OK); C(R5Observer_CaptureComplete(&o,&x)==R5_METRICS_DUPLICATE_COMPLETION); }
    else return 1; return 0;
}
