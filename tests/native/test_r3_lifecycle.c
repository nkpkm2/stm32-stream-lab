#include <stdio.h>
#include <string.h>

#include "r3_lifecycle.h"

typedef struct { int validate, prepare, arm, commit, rollback; } Fake;
static R3LifecycleStatus V(void *p, const R3LifecycleStartRequest *r) { (void)r; return ((Fake *)p)->validate ? R3_LIFECYCLE_INVALID_CONFIG : R3_LIFECYCLE_OK; }
static R3LifecycleStatus P(void *p, const R3LifecycleStartRequest *r, const StreamRunTicket *t) { (void)r; (void)t; return ((Fake *)p)->prepare ? R3_LIFECYCLE_PREPARE_FAILED : R3_LIFECYCLE_OK; }
static R3LifecycleStatus A(void *p, const R3LifecycleStartRequest *r, const StreamRunTicket *t) { (void)r; (void)t; return ((Fake *)p)->arm ? R3_LIFECYCLE_ARM_FAILED : R3_LIFECYCLE_OK; }
static R3LifecycleStatus C(void *p, const StreamRunTicket *t) { (void)t; return ((Fake *)p)->commit ? R3_LIFECYCLE_COMMIT_FAILED : R3_LIFECYCLE_OK; }
static R3LifecycleStatus R(void *p, const StreamRunTicket *t) { (void)t; return ((Fake *)p)->rollback ? R3_LIFECYCLE_ROLLBACK_FAILED : R3_LIFECYCLE_OK; }
static void Init(Fake *f) { R3LifecycleHooks h = {f, V, P, A, C, R}; R3Lifecycle_Init(7U, &h); }
static R3LifecycleStartRequest Req(void) { R3LifecycleStartRequest r = {7U, 9U, 3U, 11U}; return r; }
#define CHECK(x) do { if (!(x)) return 1; } while (0)
int main(int argc, char **argv) {
    Fake f; R3LifecycleStartTicket t; R3LifecycleSnapshot s; R3LifecycleStartRequest r;
    if (argc != 2) return 2; (void)memset(&f, 0, sizeof(f)); Init(&f); r = Req();
    if (strcmp(argv[1], "normal") == 0) { CHECK(R3Lifecycle_PrepareStart(&r,&t)==R3_LIFECYCLE_OK); CHECK(R3Lifecycle_CommitStart(&t)==R3_LIFECYCLE_OK); CHECK(R3Lifecycle_GetSnapshot(&s)==R3_LIFECYCLE_OK); CHECK(s.state==R3_LIFECYCLE_RUNNING && s.acquisition_publish_allowed); }
    else if (strcmp(argv[1], "invalid") == 0) { f.validate=1; CHECK(R3Lifecycle_PrepareStart(&r,&t)==R3_LIFECYCLE_INVALID_CONFIG); CHECK(R3Lifecycle_GetSnapshot(&s)==R3_LIFECYCLE_OK); CHECK(s.state==R3_LIFECYCLE_IDLE && !s.prepare_count); }
    else if (strcmp(argv[1], "arm_rollback") == 0) { f.arm=1; CHECK(R3Lifecycle_PrepareStart(&r,&t)==R3_LIFECYCLE_ARM_FAILED); CHECK(R3Lifecycle_GetSnapshot(&s)==R3_LIFECYCLE_OK); CHECK(s.state==R3_LIFECYCLE_IDLE && s.rollback_count==1U); }
    else if (strcmp(argv[1], "stop_before_commit") == 0) { CHECK(R3Lifecycle_PrepareStart(&r,&t)==R3_LIFECYCLE_OK); CHECK(R3Lifecycle_RequestStopBeforeCommit(&t)==R3_LIFECYCLE_STOPPED); CHECK(R3Lifecycle_GetSnapshot(&s)==R3_LIFECYCLE_OK); CHECK(s.state==R3_LIFECYCLE_IDLE && !s.acquisition_publish_allowed); }
    else if (strcmp(argv[1], "commit_failure") == 0) { f.commit=1; CHECK(R3Lifecycle_PrepareStart(&r,&t)==R3_LIFECYCLE_OK); CHECK(R3Lifecycle_CommitStart(&t)==R3_LIFECYCLE_COMMIT_FAILED); CHECK(R3Lifecycle_GetSnapshot(&s)==R3_LIFECYCLE_OK); CHECK(s.state==R3_LIFECYCLE_IDLE && !s.processing_claim_allowed); }
    else if (strcmp(argv[1], "stale") == 0) { CHECK(R3Lifecycle_PrepareStart(&r,&t)==R3_LIFECYCLE_OK); ++t.start_ticket; CHECK(R3Lifecycle_CommitStart(&t)==R3_LIFECYCLE_STALE_TICKET); }
    else return 2; return 0;
}
