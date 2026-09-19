#include "stream_ownership_core.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        (void)fprintf(stderr, "CHECK failed: %s (%s:%d)\n", \
            #condition, __FILE__, __LINE__); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static StreamOwnershipSnapshot Snap(void)
{
    StreamOwnershipSnapshot s;
    CHECK(StreamOwnership_GetSnapshot(&s) == STREAM_OWNERSHIP_OK);
    return s;
}

static void Init(uint32_t k)
{
    StreamOwnership_ResetOffline();
    CHECK(StreamOwnership_Initialize(k) == STREAM_OWNERSHIP_OK);
}

static StreamOwnershipCompletionPlan PrepareRebind(
    uint32_t sequence,
    uint32_t completed_slot,
    R2_BufferId replacement)
{
    StreamOwnershipCompletionPlan plan;
    CHECK(StreamOwnership_PrepareCompletion(
        sequence,
        completed_slot,
        STREAM_OWNERSHIP_REBIND,
        replacement,
        &plan) == STREAM_OWNERSHIP_OK);
    return plan;
}

static StreamOwnershipCompletionPlan PrepareKeep(
    uint32_t sequence,
    uint32_t completed_slot)
{
    StreamOwnershipCompletionPlan plan;
    CHECK(StreamOwnership_PrepareCompletion(
        sequence,
        completed_slot,
        STREAM_OWNERSHIP_KEEP,
        R2_BUFFER_POOL_INVALID_ID,
        &plan) == STREAM_OWNERSHIP_OK);
    return plan;
}

static void CaseReset(void)
{
    StreamOwnershipSnapshot s;
    StreamOwnership_ResetOffline();
    s = Snap();
    CHECK(s.initialized == 0U);
    CHECK(s.faulted == 0U);
    CHECK(s.k == 0U);
    CHECK(s.last_committed_sequence == 0U);
}

static void CaseInitK(uint32_t k)
{
    StreamOwnershipSnapshot s;
    Init(k);
    s = Snap();
    CHECK(s.initialized == 1U);
    CHECK(s.faulted == 0U);
    CHECK(s.k == k);
    CHECK(s.last_committed_sequence == 0U);
    CHECK(s.pool.free_count == k);
    CHECK(s.pool.dma_owned_count == 2U);
    CHECK(s.slots.m0_buffer == 0U);
    CHECK(s.slots.m1_buffer == 1U);
    CHECK(s.slots.mapping_epoch == 1U);
}
static void CaseInitK1(void) { CaseInitK(1U); }
static void CaseInitK2(void) { CaseInitK(2U); }
static void CaseInitK4(void) { CaseInitK(4U); }
static void CaseInitK8(void) { CaseInitK(8U); }


static void CaseDoubleInitFaults(void)
{
    StreamOwnershipSnapshot s;

    Init(2U);
    CHECK(StreamOwnership_Initialize(4U) ==
        STREAM_OWNERSHIP_INVALID_STATE);
    s = Snap();
    CHECK(s.initialized == 1U);
    CHECK(s.k == 2U);
    CHECK(s.faulted == 1U);
    CHECK(s.failure_count == 1U);
}

static void CaseInvalidK(void)
{
    StreamOwnershipSnapshot s;
    StreamOwnership_ResetOffline();
    CHECK(StreamOwnership_Initialize(3U) ==
        STREAM_OWNERSHIP_INVALID_ARGUMENT);
    s = Snap();
    CHECK(s.initialized == 0U);
    CHECK(s.faulted == 0U);
}

static void CasePrepareKeepNoMutation(void)
{
    StreamOwnershipSnapshot before;
    StreamOwnershipSnapshot after;
    StreamOwnershipCompletionPlan plan;

    Init(4U);
    before = Snap();
    plan = PrepareKeep(1U, 0U);
    after = Snap();

    CHECK(plan.completed_buffer == 0U);
    CHECK(plan.active_buffer == 1U);
    CHECK(memcmp(&before.pool, &after.pool, sizeof(before.pool)) == 0);
    CHECK(memcmp(&before.slots, &after.slots, sizeof(before.slots)) == 0);
    CHECK(after.last_committed_sequence == 0U);
}

static void CaseCommitKeepNoMutation(void)
{
    StreamOwnershipSnapshot before;
    StreamOwnershipSnapshot after;
    StreamOwnershipCompletionPlan plan;

    Init(1U);
    plan = PrepareKeep(1U, 0U);
    before = Snap();
    CHECK(StreamOwnership_CommitKeep(&plan) == STREAM_OWNERSHIP_OK);
    after = Snap();

    CHECK(after.keep_commit_count == before.keep_commit_count + 1U);
    CHECK(after.last_committed_sequence == 1U);
    CHECK(memcmp(&before.pool, &after.pool, sizeof(before.pool)) == 0);
    CHECK(memcmp(&before.slots, &after.slots, sizeof(before.slots)) == 0);
}

static void CasePrepareRebindNoMutation(void)
{
    StreamOwnershipSnapshot before;
    StreamOwnershipSnapshot after;
    StreamOwnershipCompletionPlan plan;

    Init(4U);
    before = Snap();
    plan = PrepareRebind(1U, 0U, 2U);
    after = Snap();

    CHECK(plan.completed_buffer == 0U);
    CHECK(plan.active_buffer == 1U);
    CHECK(plan.replacement_buffer == 2U);
    CHECK(memcmp(&before.pool, &after.pool, sizeof(before.pool)) == 0);
    CHECK(memcmp(&before.slots, &after.slots, sizeof(before.slots)) == 0);
    CHECK(after.last_committed_sequence == 0U);
}

static void CaseCommitRebind(void)
{
    StreamOwnershipCompletionPlan plan;
    StreamOwnershipDescriptor d;
    StreamOwnershipSnapshot s;

    Init(4U);
    plan = PrepareRebind(1U, 0U, 2U);
    CHECK(StreamOwnership_CommitRebind(&plan, &d) ==
        STREAM_OWNERSHIP_OK);

    s = Snap();
    CHECK(d.sequence == 1U);
    CHECK(d.buffer_id == 0U);
    CHECK(d.completed_slot == 0U);
    CHECK(d.mapping_epoch == 2U);
    CHECK(s.last_committed_sequence == 1U);
    CHECK(s.slots.m0_buffer == 2U);
    CHECK(s.slots.m1_buffer == 1U);
    CHECK(s.pool.states[0] == R2_BUFFER_STATE_READY);
    CHECK(s.pool.states[1] == R2_BUFFER_STATE_DMA_OWNED);
    CHECK(s.pool.states[2] == R2_BUFFER_STATE_DMA_OWNED);
    CHECK(s.rebind_commit_count == 1U);
}

static void CaseAlternatingEight(void)
{
    uint32_t sequence;
    StreamOwnershipSnapshot s;

    Init(8U);

    for (sequence = 1U; sequence <= 8U; ++sequence)
    {
        uint32_t completed_slot = (sequence & 1U) ^ 1U;
        R2_BufferId replacement = (R2_BufferId)(sequence + 1U);
        StreamOwnershipCompletionPlan plan =
            PrepareRebind(sequence, completed_slot, replacement);
        StreamOwnershipDescriptor d;

        CHECK(StreamOwnership_CommitRebind(&plan, &d) ==
            STREAM_OWNERSHIP_OK);
        CHECK(d.sequence == sequence);
    }

    s = Snap();
    CHECK(s.last_committed_sequence == 8U);
    CHECK(s.slots.m0_buffer == 8U);
    CHECK(s.slots.m1_buffer == 9U);
    CHECK(s.slots.mapping_epoch == 9U);
    CHECK(s.pool.free_count == 0U);
    CHECK(s.pool.ready_count == 8U);
    CHECK(s.pool.dma_owned_count == 2U);
    CHECK(s.rebind_commit_count == 8U);
}

static void CaseKeepAfterExhaustion(void)
{
    uint32_t sequence;
    StreamOwnershipSnapshot before;
    StreamOwnershipSnapshot after;
    StreamOwnershipCompletionPlan keep;

    Init(8U);
    for (sequence = 1U; sequence <= 8U; ++sequence)
    {
        uint32_t completed_slot = (sequence & 1U) ^ 1U;
        R2_BufferId replacement = (R2_BufferId)(sequence + 1U);
        StreamOwnershipCompletionPlan plan =
            PrepareRebind(sequence, completed_slot, replacement);
        StreamOwnershipDescriptor d;
        CHECK(StreamOwnership_CommitRebind(&plan, &d) ==
            STREAM_OWNERSHIP_OK);
    }

    before = Snap();
    keep = PrepareKeep(9U, 0U);
    CHECK(StreamOwnership_CommitKeep(&keep) == STREAM_OWNERSHIP_OK);
    after = Snap();

    CHECK(after.last_committed_sequence == 9U);
    CHECK(after.keep_commit_count == before.keep_commit_count + 1U);
    CHECK(memcmp(&before.pool, &after.pool, sizeof(before.pool)) == 0);
    CHECK(memcmp(&before.slots, &after.slots, sizeof(before.slots)) == 0);
}

static void CaseDropStreakThenRebind(void)
{
    StreamOwnershipCompletionPlan keep1;
    StreamOwnershipCompletionPlan keep2;
    StreamOwnershipCompletionPlan rebind;
    StreamOwnershipDescriptor d;
    StreamOwnershipSnapshot after_keep;
    StreamOwnershipSnapshot after_rebind;

    Init(2U);

    keep1 = PrepareKeep(1U, 0U);
    CHECK(StreamOwnership_CommitKeep(&keep1) == STREAM_OWNERSHIP_OK);
    keep2 = PrepareKeep(2U, 1U);
    CHECK(StreamOwnership_CommitKeep(&keep2) == STREAM_OWNERSHIP_OK);

    after_keep = Snap();
    CHECK(after_keep.last_committed_sequence == 2U);
    CHECK(after_keep.keep_commit_count == 2U);
    CHECK(after_keep.rebind_commit_count == 0U);
    CHECK(after_keep.slots.mapping_epoch == 1U);
    CHECK(after_keep.slots.m0_buffer == 0U);
    CHECK(after_keep.slots.m1_buffer == 1U);
    CHECK(after_keep.pool.states[0] == R2_BUFFER_STATE_DMA_OWNED);
    CHECK(after_keep.pool.states[1] == R2_BUFFER_STATE_DMA_OWNED);
    CHECK(after_keep.pool.states[2] == R2_BUFFER_STATE_FREE);

    rebind = PrepareRebind(3U, 0U, 2U);
    CHECK(StreamOwnership_CommitRebind(&rebind, &d) ==
        STREAM_OWNERSHIP_OK);

    after_rebind = Snap();
    CHECK(d.sequence == 3U);
    CHECK(d.buffer_id == 0U);
    CHECK(d.mapping_epoch == 2U);
    CHECK(after_rebind.last_committed_sequence == 3U);
    CHECK(after_rebind.keep_commit_count == 2U);
    CHECK(after_rebind.rebind_commit_count == 1U);
    CHECK(after_rebind.slots.mapping_epoch == 2U);
    CHECK(after_rebind.slots.m0_buffer == 2U);
    CHECK(after_rebind.slots.m1_buffer == 1U);
    CHECK(after_rebind.pool.states[0] == R2_BUFFER_STATE_READY);
    CHECK(after_rebind.pool.states[1] == R2_BUFFER_STATE_DMA_OWNED);
    CHECK(after_rebind.pool.states[2] == R2_BUFFER_STATE_DMA_OWNED);
}

static void CaseSequenceGapFaults(void)
{
    StreamOwnershipCompletionPlan plan;
    StreamOwnershipSnapshot s;

    Init(2U);
    CHECK(StreamOwnership_PrepareCompletion(
        2U,
        1U,
        STREAM_OWNERSHIP_KEEP,
        R2_BUFFER_POOL_INVALID_ID,
        &plan) == STREAM_OWNERSHIP_STALE_TRANSACTION);
    s = Snap();
    CHECK(s.faulted == 1U);
    CHECK(s.failure_count == 1U);
    CHECK(s.last_committed_sequence == 0U);
}

static void CaseDuplicateKeepFaults(void)
{
    StreamOwnershipCompletionPlan plan;
    StreamOwnershipSnapshot s;

    Init(1U);
    plan = PrepareKeep(1U, 0U);
    CHECK(StreamOwnership_CommitKeep(&plan) == STREAM_OWNERSHIP_OK);
    CHECK(StreamOwnership_CommitKeep(&plan) ==
        STREAM_OWNERSHIP_STALE_TRANSACTION);
    s = Snap();
    CHECK(s.faulted == 1U);
    CHECK(s.failure_count == 1U);
    CHECK(s.keep_commit_count == 1U);
    CHECK(s.last_committed_sequence == 1U);
}

static void CaseStalePlanRejected(void)
{
    StreamOwnershipCompletionPlan stale;
    StreamOwnershipCompletionPlan current;
    StreamOwnershipDescriptor d;
    StreamOwnershipSnapshot before;
    StreamOwnershipSnapshot after;

    Init(4U);
    stale = PrepareRebind(1U, 0U, 2U);
    current = PrepareRebind(1U, 0U, 3U);
    CHECK(StreamOwnership_CommitRebind(&current, &d) ==
        STREAM_OWNERSHIP_OK);
    before = Snap();
    CHECK(StreamOwnership_CommitRebind(&stale, &d) ==
        STREAM_OWNERSHIP_STALE_TRANSACTION);
    after = Snap();

    CHECK(after.faulted == 1U);
    CHECK(after.failure_count == before.failure_count + 1U);
    CHECK(after.last_committed_sequence == 1U);
    CHECK(after.slots.m0_buffer == 3U);
    CHECK(after.slots.m1_buffer == 1U);
    CHECK(after.pool.states[0] == R2_BUFFER_STATE_READY);
    CHECK(after.pool.states[2] == R2_BUFFER_STATE_FREE);
    CHECK(after.pool.states[3] == R2_BUFFER_STATE_DMA_OWNED);
}

static void CaseReplacementNotFreeFaults(void)
{
    StreamOwnershipCompletionPlan plan;
    StreamOwnershipSnapshot s;

    Init(4U);
    CHECK(StreamOwnership_PrepareCompletion(
        1U,
        0U,
        STREAM_OWNERSHIP_REBIND,
        1U,
        &plan) == STREAM_OWNERSHIP_MODEL_ERROR);
    s = Snap();
    CHECK(s.faulted == 1U);
    CHECK(s.failure_count == 1U);
}

static void CaseForgedPlanFaults(void)
{
    StreamOwnershipCompletionPlan plan;
    StreamOwnershipDescriptor d;
    StreamOwnershipSnapshot before;
    StreamOwnershipSnapshot after;

    Init(4U);
    plan = PrepareRebind(1U, 0U, 2U);
    before = Snap();
    plan.rebind_plan.active_slot = R2_DMA_SLOT_M0;
    CHECK(StreamOwnership_CommitRebind(&plan, &d) ==
        STREAM_OWNERSHIP_STALE_TRANSACTION);
    after = Snap();

    CHECK(after.faulted == 1U);
    CHECK(after.failure_count == before.failure_count + 1U);
    CHECK(after.slots.mapping_epoch == before.slots.mapping_epoch);
    CHECK(after.slots.violation_count == before.slots.violation_count);
    CHECK(memcmp(&before.pool, &after.pool, sizeof(before.pool)) == 0);
}

static void CaseKeepReplacementRejected(void)
{
    StreamOwnershipCompletionPlan plan;
    StreamOwnershipSnapshot s;

    Init(2U);
    CHECK(StreamOwnership_PrepareCompletion(
        1U,
        0U,
        STREAM_OWNERSHIP_KEEP,
        2U,
        &plan) == STREAM_OWNERSHIP_INVALID_ARGUMENT);
    s = Snap();
    CHECK(s.faulted == 1U);
    CHECK(s.failure_count == 1U);
}

static void CaseSnapshot(void)
{
    StreamOwnershipSnapshot s;
    Init(2U);
    s = Snap();
    CHECK(s.initialized == 1U);
    CHECK(s.k == 2U);
    CHECK(s.last_committed_sequence == 0U);
    CHECK(s.keep_commit_count == 0U);
    CHECK(s.rebind_commit_count == 0U);
    CHECK(s.failure_count == 0U);
    CHECK(s.last_status == STREAM_OWNERSHIP_OK);
}

typedef struct
{
    const char *name;
    void (*run)(void);
} TestCase;

static const TestCase cases[] = {
    {"reset", CaseReset},
    {"init_k1", CaseInitK1},
    {"init_k2", CaseInitK2},
    {"init_k4", CaseInitK4},
    {"init_k8", CaseInitK8},
    {"invalid_k", CaseInvalidK},
    {"double_init_faults", CaseDoubleInitFaults},
    {"prepare_keep_no_mutation", CasePrepareKeepNoMutation},
    {"commit_keep_no_mutation", CaseCommitKeepNoMutation},
    {"prepare_rebind_no_mutation", CasePrepareRebindNoMutation},
    {"commit_rebind", CaseCommitRebind},
    {"alternating_eight", CaseAlternatingEight},
    {"keep_after_exhaustion", CaseKeepAfterExhaustion},
    {"drop_streak_then_rebind", CaseDropStreakThenRebind},
    {"sequence_gap_faults", CaseSequenceGapFaults},
    {"duplicate_keep_faults", CaseDuplicateKeepFaults},
    {"stale_plan_rejected", CaseStalePlanRejected},
    {"replacement_not_free_faults", CaseReplacementNotFreeFaults},
    {"forged_plan_faults", CaseForgedPlanFaults},
    {"keep_replacement_rejected", CaseKeepReplacementRejected},
    {"snapshot", CaseSnapshot}
};

int main(int argc, char **argv)
{
    size_t i;
    int ran = 0;

    if (argc > 2)
    {
        return EXIT_FAILURE;
    }

    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        if ((argc == 1) || (strcmp(argv[1], cases[i].name) == 0))
        {
            cases[i].run();
            ran = 1;
        }
    }

    if (ran == 0)
    {
        (void)fprintf(stderr, "Unknown case: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    (void)puts("F0-3 StreamOwnership native tests: PASS");
    return EXIT_SUCCESS;
}
