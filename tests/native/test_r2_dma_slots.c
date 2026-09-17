#include "r2_dma_slots.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
            exit(1); \
        } \
    } while (0)

static R2_DmaSlotsSnapshot Snapshot(void)
{
    R2_DmaSlotsSnapshot snapshot;
    CHECK(R2_DmaSlots_GetSnapshot(&snapshot) == R2_DMA_SLOTS_OK);
    return snapshot;
}

static void CheckSnapshotSameExceptViolation(
    const R2_DmaSlotsSnapshot *before,
    const R2_DmaSlotsSnapshot *after,
    uint32_t violation_delta)
{
    CHECK(before->initialized == after->initialized);
    CHECK(before->mapping_epoch == after->mapping_epoch);
    CHECK(before->m0_buffer == after->m0_buffer);
    CHECK(before->m1_buffer == after->m1_buffer);
    CHECK(after->violation_count == before->violation_count + violation_delta);
}

static void ResetAndInit(R2_BufferId m0, R2_BufferId m1)
{
    R2_DmaSlots_Reset();
    CHECK(R2_DmaSlots_Initialize(m0, m1) == R2_DMA_SLOTS_OK);
    CHECK(R2_DmaSlots_Validate() == R2_DMA_SLOTS_OK);
}

static void TestReset(void)
{
    R2_DmaSlotsSnapshot snapshot;

    R2_DmaSlots_Reset();
    snapshot = Snapshot();

    CHECK(snapshot.initialized == 0U);
    CHECK(snapshot.mapping_epoch == 0U);
    CHECK(snapshot.violation_count == 0U);
    CHECK(snapshot.m0_buffer == R2_BUFFER_POOL_INVALID_ID);
    CHECK(snapshot.m1_buffer == R2_BUFFER_POOL_INVALID_ID);
    CHECK(R2_DmaSlots_Validate() == R2_DMA_SLOTS_OK);
}


static void TestColdInitialize(void)
{
    R2_DmaSlotsSnapshot snapshot;

    CHECK(R2_DmaSlots_Initialize(0U, 1U) == R2_DMA_SLOTS_OK);
    snapshot = Snapshot();
    CHECK(snapshot.initialized == 1U);
    CHECK(snapshot.mapping_epoch == 1U);
    CHECK(snapshot.m0_buffer == 0U);
    CHECK(snapshot.m1_buffer == 1U);
}

static void TestInitialize(void)
{
    R2_DmaSlotsSnapshot snapshot;

    ResetAndInit(0U, 1U);
    snapshot = Snapshot();

    CHECK(snapshot.initialized == 1U);
    CHECK(snapshot.mapping_epoch == 1U);
    CHECK(snapshot.m0_buffer == 0U);
    CHECK(snapshot.m1_buffer == 1U);
}

static void TestInvalidInitialize(void)
{
    R2_DmaSlotsSnapshot before;
    R2_DmaSlotsSnapshot after;

    R2_DmaSlots_Reset();
    before = Snapshot();
    CHECK(R2_DmaSlots_Initialize(0U, 0U) == R2_DMA_SLOTS_DUPLICATE_BINDING);
    after = Snapshot();
    CheckSnapshotSameExceptViolation(&before, &after, 1U);

    R2_DmaSlots_Reset();
    before = Snapshot();
    CHECK(R2_DmaSlots_Initialize(10U, 1U) == R2_DMA_SLOTS_INVALID_BUFFER_ID);
    after = Snapshot();
    CheckSnapshotSameExceptViolation(&before, &after, 1U);
}

static void TestAlreadyInitialized(void)
{
    R2_DmaSlotsSnapshot before;
    R2_DmaSlotsSnapshot after;

    ResetAndInit(0U, 1U);
    before = Snapshot();
    CHECK(R2_DmaSlots_Initialize(2U, 3U) == R2_DMA_SLOTS_ALREADY_INITIALIZED);
    after = Snapshot();
    CheckSnapshotSameExceptViolation(&before, &after, 1U);
}

static void TestObserveCt0(void)
{
    R2_DmaSlotsObservation observation;

    ResetAndInit(2U, 5U);
    CHECK(R2_DmaSlots_ObserveCt(0U, &observation) == R2_DMA_SLOTS_OK);
    CHECK(observation.ct_snapshot == 0U);
    CHECK(observation.active_slot == R2_DMA_SLOT_M0);
    CHECK(observation.inactive_slot == R2_DMA_SLOT_M1);
    CHECK(observation.active_buffer == 2U);
    CHECK(observation.completed_buffer == 5U);
    CHECK(observation.mapping_epoch == 1U);
}

static void TestObserveCt1(void)
{
    R2_DmaSlotsObservation observation;

    ResetAndInit(2U, 5U);
    CHECK(R2_DmaSlots_ObserveCt(1U, &observation) == R2_DMA_SLOTS_OK);
    CHECK(observation.ct_snapshot == 1U);
    CHECK(observation.active_slot == R2_DMA_SLOT_M1);
    CHECK(observation.inactive_slot == R2_DMA_SLOT_M0);
    CHECK(observation.active_buffer == 5U);
    CHECK(observation.completed_buffer == 2U);
    CHECK(observation.mapping_epoch == 1U);
}

static void TestInvalidCt(void)
{
    R2_DmaSlotsObservation observation;
    R2_DmaSlotsSnapshot before;
    R2_DmaSlotsSnapshot after;

    ResetAndInit(0U, 1U);
    (void)memset(&observation, 0xA5, sizeof(observation));
    before = Snapshot();
    CHECK(R2_DmaSlots_ObserveCt(2U, &observation) == R2_DMA_SLOTS_INVALID_CT);
    after = Snapshot();
    CheckSnapshotSameExceptViolation(&before, &after, 1U);
    CHECK(observation.ct_snapshot == 0xA5A5A5A5U);
}

static void TestGetBoundBuffer(void)
{
    R2_BufferId id = 0xEEU;

    ResetAndInit(3U, 7U);
    CHECK(R2_DmaSlots_GetBoundBuffer(R2_DMA_SLOT_M0, &id) == R2_DMA_SLOTS_OK);
    CHECK(id == 3U);
    CHECK(R2_DmaSlots_GetBoundBuffer(R2_DMA_SLOT_M1, &id) == R2_DMA_SLOTS_OK);
    CHECK(id == 7U);
}

static void TestInvalidSlot(void)
{
    R2_BufferId id = 0xEEU;
    R2_DmaSlotsSnapshot before;
    R2_DmaSlotsSnapshot after;

    ResetAndInit(0U, 1U);
    before = Snapshot();
    CHECK(R2_DmaSlots_GetBoundBuffer((R2_DmaSlot)2, &id) == R2_DMA_SLOTS_INVALID_SLOT);
    after = Snapshot();
    CheckSnapshotSameExceptViolation(&before, &after, 1U);
    CHECK(id == 0xEEU);
}

static void TestPrepareNoMutation(void)
{
    R2_DmaSlotsRebindPlan plan;
    R2_DmaSlotsSnapshot before;
    R2_DmaSlotsSnapshot after;

    ResetAndInit(0U, 1U);
    before = Snapshot();
    CHECK(R2_DmaSlots_PrepareInactiveRebind(0U, 2U, &plan) == R2_DMA_SLOTS_OK);
    after = Snapshot();

    CHECK(memcmp(&before, &after, sizeof(before)) == 0);
    CHECK(plan.valid == 1U);
    CHECK(plan.ct_snapshot == 0U);
    CHECK(plan.active_slot == R2_DMA_SLOT_M0);
    CHECK(plan.inactive_slot == R2_DMA_SLOT_M1);
    CHECK(plan.completed_buffer == 1U);
    CHECK(plan.replacement_buffer == 2U);
    CHECK(plan.mapping_epoch == 1U);
}

static void TestPrepareDuplicateReplacement(void)
{
    R2_DmaSlotsRebindPlan plan;
    R2_DmaSlotsSnapshot before;
    R2_DmaSlotsSnapshot after;

    ResetAndInit(0U, 1U);
    (void)memset(&plan, 0xA5, sizeof(plan));
    before = Snapshot();
    CHECK(R2_DmaSlots_PrepareInactiveRebind(0U, 0U, &plan) ==
        R2_DMA_SLOTS_DUPLICATE_BINDING);
    after = Snapshot();
    CheckSnapshotSameExceptViolation(&before, &after, 1U);
    CHECK(plan.valid == 0xA5A5A5A5U);
}

static void TestPrepareInvalidReplacement(void)
{
    R2_DmaSlotsRebindPlan plan;
    R2_DmaSlotsSnapshot before;
    R2_DmaSlotsSnapshot after;

    ResetAndInit(0U, 1U);
    (void)memset(&plan, 0xA5, sizeof(plan));
    before = Snapshot();
    CHECK(R2_DmaSlots_PrepareInactiveRebind(1U, 10U, &plan) ==
        R2_DMA_SLOTS_INVALID_BUFFER_ID);
    after = Snapshot();
    CheckSnapshotSameExceptViolation(&before, &after, 1U);
    CHECK(plan.valid == 0xA5A5A5A5U);
}

static void TestCheckPlanCt(void)
{
    R2_DmaSlotsRebindPlan plan;

    ResetAndInit(0U, 1U);
    CHECK(R2_DmaSlots_PrepareInactiveRebind(0U, 2U, &plan) == R2_DMA_SLOTS_OK);
    CHECK(R2_DmaSlots_CheckPlanCt(&plan, 0U) == R2_DMA_SLOTS_OK);
}

static void TestCtChanged(void)
{
    R2_DmaSlotsRebindPlan plan;
    R2_DmaSlotsSnapshot before;
    R2_DmaSlotsSnapshot after;

    ResetAndInit(0U, 1U);
    CHECK(R2_DmaSlots_PrepareInactiveRebind(0U, 2U, &plan) == R2_DMA_SLOTS_OK);
    before = Snapshot();
    CHECK(R2_DmaSlots_CheckPlanCt(&plan, 1U) == R2_DMA_SLOTS_CT_CHANGED);
    after = Snapshot();
    CheckSnapshotSameExceptViolation(&before, &after, 1U);
}

static void TestCommitCt0(void)
{
    R2_DmaSlotsRebindPlan plan;
    R2_DmaSlotsSnapshot snapshot;

    ResetAndInit(0U, 1U);
    CHECK(R2_DmaSlots_PrepareInactiveRebind(0U, 2U, &plan) == R2_DMA_SLOTS_OK);
    CHECK(R2_DmaSlots_CheckPlanCt(&plan, 0U) == R2_DMA_SLOTS_OK);
    CHECK(R2_DmaSlots_CommitPreparedRebind(&plan) == R2_DMA_SLOTS_OK);
    CHECK(R2_DmaSlots_Validate() == R2_DMA_SLOTS_OK);

    snapshot = Snapshot();
    CHECK(snapshot.m0_buffer == 0U);
    CHECK(snapshot.m1_buffer == 2U);
    CHECK(snapshot.mapping_epoch == 2U);
}

static void TestCommitCt1(void)
{
    R2_DmaSlotsRebindPlan plan;
    R2_DmaSlotsSnapshot snapshot;

    ResetAndInit(0U, 1U);
    CHECK(R2_DmaSlots_PrepareInactiveRebind(1U, 2U, &plan) == R2_DMA_SLOTS_OK);
    CHECK(R2_DmaSlots_CheckPlanCt(&plan, 1U) == R2_DMA_SLOTS_OK);
    CHECK(R2_DmaSlots_CommitPreparedRebind(&plan) == R2_DMA_SLOTS_OK);

    snapshot = Snapshot();
    CHECK(snapshot.m0_buffer == 2U);
    CHECK(snapshot.m1_buffer == 1U);
    CHECK(snapshot.mapping_epoch == 2U);
}

static void TestStalePlanAfterCommit(void)
{
    R2_DmaSlotsRebindPlan stale;
    R2_DmaSlotsRebindPlan current;
    R2_DmaSlotsSnapshot before;
    R2_DmaSlotsSnapshot after;

    ResetAndInit(0U, 1U);
    CHECK(R2_DmaSlots_PrepareInactiveRebind(0U, 2U, &stale) == R2_DMA_SLOTS_OK);
    CHECK(R2_DmaSlots_PrepareInactiveRebind(0U, 3U, &current) == R2_DMA_SLOTS_OK);
    CHECK(R2_DmaSlots_CommitPreparedRebind(&current) == R2_DMA_SLOTS_OK);

    before = Snapshot();
    CHECK(R2_DmaSlots_CommitPreparedRebind(&stale) == R2_DMA_SLOTS_STALE_PLAN);
    after = Snapshot();
    CheckSnapshotSameExceptViolation(&before, &after, 1U);
}


static void TestForgedPlanRejected(void)
{
    R2_DmaSlotsRebindPlan plan;
    R2_DmaSlotsSnapshot before;
    R2_DmaSlotsSnapshot after;

    ResetAndInit(0U, 1U);
    CHECK(R2_DmaSlots_PrepareInactiveRebind(0U, 2U, &plan) == R2_DMA_SLOTS_OK);
    plan.active_slot = R2_DMA_SLOT_M1;
    plan.inactive_slot = R2_DMA_SLOT_M0;
    before = Snapshot();
    CHECK(R2_DmaSlots_CommitPreparedRebind(&plan) == R2_DMA_SLOTS_STALE_PLAN);
    after = Snapshot();
    CheckSnapshotSameExceptViolation(&before, &after, 1U);
}

static void TestRepeatedCommitRejected(void)
{
    R2_DmaSlotsRebindPlan plan;
    R2_DmaSlotsSnapshot before;
    R2_DmaSlotsSnapshot after;

    ResetAndInit(0U, 1U);
    CHECK(R2_DmaSlots_PrepareInactiveRebind(0U, 2U, &plan) == R2_DMA_SLOTS_OK);
    CHECK(R2_DmaSlots_CommitPreparedRebind(&plan) == R2_DMA_SLOTS_OK);

    before = Snapshot();
    CHECK(R2_DmaSlots_CommitPreparedRebind(&plan) == R2_DMA_SLOTS_STALE_PLAN);
    after = Snapshot();
    CheckSnapshotSameExceptViolation(&before, &after, 1U);
}

static void TestAlternatingRotationSequence(void)
{
    R2_DmaSlotsRebindPlan plan;
    R2_DmaSlotsObservation observation;
    uint32_t i;
    static const uint32_t ct_sequence[] = {0U, 1U, 0U, 1U, 0U, 1U};
    static const R2_BufferId replacements[] = {2U, 3U, 4U, 5U, 6U, 7U};

    ResetAndInit(0U, 1U);

    for (i = 0U; i < 6U; ++i)
    {
        CHECK(R2_DmaSlots_PrepareInactiveRebind(
            ct_sequence[i], replacements[i], &plan) == R2_DMA_SLOTS_OK);
        CHECK(R2_DmaSlots_CheckPlanCt(&plan, ct_sequence[i]) == R2_DMA_SLOTS_OK);
        CHECK(R2_DmaSlots_CommitPreparedRebind(&plan) == R2_DMA_SLOTS_OK);
        CHECK(R2_DmaSlots_Validate() == R2_DMA_SLOTS_OK);
        CHECK(R2_DmaSlots_ObserveCt(ct_sequence[i], &observation) == R2_DMA_SLOTS_OK);
        CHECK(observation.active_slot == (ct_sequence[i] == 0U ?
            R2_DMA_SLOT_M0 : R2_DMA_SLOT_M1));
    }
}

static void TestNullArguments(void)
{
    R2_DmaSlotsSnapshot before;
    R2_DmaSlotsSnapshot after;

    ResetAndInit(0U, 1U);

    before = Snapshot();
    CHECK(R2_DmaSlots_ObserveCt(0U, NULL) == R2_DMA_SLOTS_INVALID_ARGUMENT);
    after = Snapshot();
    CheckSnapshotSameExceptViolation(&before, &after, 1U);

    before = Snapshot();
    CHECK(R2_DmaSlots_PrepareInactiveRebind(0U, 2U, NULL) ==
        R2_DMA_SLOTS_INVALID_ARGUMENT);
    after = Snapshot();
    CheckSnapshotSameExceptViolation(&before, &after, 1U);

    before = Snapshot();
    CHECK(R2_DmaSlots_CheckPlanCt(NULL, 0U) == R2_DMA_SLOTS_INVALID_ARGUMENT);
    after = Snapshot();
    CheckSnapshotSameExceptViolation(&before, &after, 1U);

    before = Snapshot();
    CHECK(R2_DmaSlots_CommitPreparedRebind(NULL) == R2_DMA_SLOTS_INVALID_ARGUMENT);
    after = Snapshot();
    CheckSnapshotSameExceptViolation(&before, &after, 1U);
}

static void TestUninitialized(void)
{
    R2_DmaSlotsObservation observation;
    R2_DmaSlotsSnapshot before;
    R2_DmaSlotsSnapshot after;

    R2_DmaSlots_Reset();
    before = Snapshot();
    CHECK(R2_DmaSlots_ObserveCt(0U, &observation) == R2_DMA_SLOTS_NOT_INITIALIZED);
    after = Snapshot();
    CheckSnapshotSameExceptViolation(&before, &after, 1U);
}

static void TestResetAfterActivity(void)
{
    R2_DmaSlotsRebindPlan plan;
    R2_DmaSlotsSnapshot snapshot;

    ResetAndInit(0U, 1U);
    CHECK(R2_DmaSlots_PrepareInactiveRebind(0U, 2U, &plan) == R2_DMA_SLOTS_OK);
    CHECK(R2_DmaSlots_CommitPreparedRebind(&plan) == R2_DMA_SLOTS_OK);
    R2_DmaSlots_Reset();

    snapshot = Snapshot();
    CHECK(snapshot.initialized == 0U);
    CHECK(snapshot.mapping_epoch == 0U);
    CHECK(snapshot.violation_count == 0U);
    CHECK(snapshot.m0_buffer == R2_BUFFER_POOL_INVALID_ID);
    CHECK(snapshot.m1_buffer == R2_BUFFER_POOL_INVALID_ID);
}

static void TestSnapshotCopy(void)
{
    R2_DmaSlotsSnapshot a;
    R2_DmaSlotsSnapshot b;

    ResetAndInit(4U, 9U);
    a = Snapshot();
    b = a;
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
}

static void RunNamed(const char *name)
{
    if (strcmp(name, "reset") == 0) TestReset();
    else if (strcmp(name, "cold_initialize") == 0) TestColdInitialize();
    else if (strcmp(name, "initialize") == 0) TestInitialize();
    else if (strcmp(name, "invalid_initialize") == 0) TestInvalidInitialize();
    else if (strcmp(name, "already_initialized") == 0) TestAlreadyInitialized();
    else if (strcmp(name, "observe_ct0") == 0) TestObserveCt0();
    else if (strcmp(name, "observe_ct1") == 0) TestObserveCt1();
    else if (strcmp(name, "invalid_ct") == 0) TestInvalidCt();
    else if (strcmp(name, "get_bound") == 0) TestGetBoundBuffer();
    else if (strcmp(name, "invalid_slot") == 0) TestInvalidSlot();
    else if (strcmp(name, "prepare_no_mutation") == 0) TestPrepareNoMutation();
    else if (strcmp(name, "prepare_duplicate") == 0) TestPrepareDuplicateReplacement();
    else if (strcmp(name, "prepare_invalid") == 0) TestPrepareInvalidReplacement();
    else if (strcmp(name, "check_plan_ct") == 0) TestCheckPlanCt();
    else if (strcmp(name, "ct_changed") == 0) TestCtChanged();
    else if (strcmp(name, "commit_ct0") == 0) TestCommitCt0();
    else if (strcmp(name, "commit_ct1") == 0) TestCommitCt1();
    else if (strcmp(name, "stale_plan") == 0) TestStalePlanAfterCommit();
    else if (strcmp(name, "forged_plan") == 0) TestForgedPlanRejected();
    else if (strcmp(name, "repeat_commit") == 0) TestRepeatedCommitRejected();
    else if (strcmp(name, "alternating_sequence") == 0) TestAlternatingRotationSequence();
    else if (strcmp(name, "null_arguments") == 0) TestNullArguments();
    else if (strcmp(name, "uninitialized") == 0) TestUninitialized();
    else if (strcmp(name, "reset_after_activity") == 0) TestResetAfterActivity();
    else if (strcmp(name, "snapshot_copy") == 0) TestSnapshotCopy();
    else
    {
        fprintf(stderr, "Unknown test case: %s\n", name);
        exit(2);
    }
}

static void RunAll(void)
{
    static const char *const cases[] = {
        "reset",
        "cold_initialize",
        "initialize",
        "invalid_initialize",
        "already_initialized",
        "observe_ct0",
        "observe_ct1",
        "invalid_ct",
        "get_bound",
        "invalid_slot",
        "prepare_no_mutation",
        "prepare_duplicate",
        "prepare_invalid",
        "check_plan_ct",
        "ct_changed",
        "commit_ct0",
        "commit_ct1",
        "stale_plan",
        "forged_plan",
        "repeat_commit",
        "alternating_sequence",
        "null_arguments",
        "uninitialized",
        "reset_after_activity",
        "snapshot_copy"
    };
    size_t i;

    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        RunNamed(cases[i]);
    }
}

int main(int argc, char **argv)
{
    if (argc == 1)
    {
        RunAll();
        puts("R2-W2 DMA slot native tests: PASS");
        return 0;
    }

    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s [case]\n", argv[0]);
        return 2;
    }

    RunNamed(argv[1]);
    return 0;
}
