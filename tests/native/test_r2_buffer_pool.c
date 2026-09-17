#include "r2_buffer_pool.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Deliberately NOT assert(): checks stay active under NDEBUG/Release. */
#define CHECK(condition) do { \
    if (!(condition)) { \
        (void)fprintf(stderr, "CHECK failed: %s (%s:%d)\n", \
                      #condition, __FILE__, __LINE__); \
        exit(EXIT_FAILURE); \
    } \
} while (0)
#define OK(call) CHECK((call) == R2_BUFFER_POOL_OK)

static const uint32_t ks[] = {1U, 2U, 4U, 8U};

static R2_BufferPoolSnapshot Snap(void)
{
    R2_BufferPoolSnapshot snapshot;
    OK(R2_BufferPool_GetSnapshot(&snapshot));
    return snapshot;
}

/* Compare logical fields, not unportable padding bytes in a C structure. */
static void SameOwnership(
    const R2_BufferPoolSnapshot *a,
    const R2_BufferPoolSnapshot *b)
{
    uint32_t id;
    CHECK(a->activated == b->activated);
    CHECK(a->k == b->k);
    CHECK(a->active_count == b->active_count);
    CHECK(a->inactive_count == b->inactive_count);
    CHECK(a->free_count == b->free_count);
    CHECK(a->dma_owned_count == b->dma_owned_count);
    CHECK(a->ready_count == b->ready_count);
    CHECK(a->processing_count == b->processing_count);
    for (id = 0U; id < R2_BUFFER_POOL_MAX_BUFFERS; ++id)
    {
        CHECK(a->states[id] == b->states[id]);
    }
}

static void UnchangedExceptViolation(
    const R2_BufferPoolSnapshot *before,
    uint32_t added_violations)
{
    R2_BufferPoolSnapshot after = Snap();
    SameOwnership(before, &after);
    CHECK(after.violation_count == before->violation_count + added_violations);
    OK(R2_BufferPool_Validate());
}

static void Activate(uint32_t k)
{
    R2_BufferPool_Reset();
    OK(R2_BufferPool_Activate(k));
    OK(R2_BufferPool_Validate());
}

static void CheckReset(void)
{
    uint32_t id;
    R2_BufferPoolSnapshot s = Snap();
    CHECK(s.activated == 0U && s.k == 0U && s.active_count == 0U);
    CHECK(s.inactive_count == 10U);
    CHECK(s.free_count == 0U && s.dma_owned_count == 0U);
    CHECK(s.ready_count == 0U && s.processing_count == 0U);
    CHECK(s.violation_count == 0U);
    for (id = 0U; id < 10U; ++id)
    {
        CHECK(s.states[id] == R2_BUFFER_STATE_INACTIVE);
    }
    OK(R2_BufferPool_Validate());
}

static void TestReset(void)
{
    R2_BufferPool_Reset();
    CheckReset();
    R2_BufferPool_Reset();
    CheckReset();
}

static void TestActivation(uint32_t k)
{
    uint32_t id;
    R2_BufferPoolSnapshot s;
    Activate(k);
    s = Snap();
    CHECK(s.activated == 1U && s.k == k && s.active_count == k + 2U);
    CHECK(s.inactive_count == 8U - k && s.free_count == k);
    CHECK(s.dma_owned_count == 2U);
    CHECK(s.ready_count == 0U && s.processing_count == 0U);
    CHECK(s.violation_count == 0U);
    for (id = 0U; id < 10U; ++id)
    {
        R2_BufferState expected = (id < 2U) ? R2_BUFFER_STATE_DMA_OWNED :
            ((id < k + 2U) ? R2_BUFFER_STATE_FREE : R2_BUFFER_STATE_INACTIVE);
        CHECK(s.states[id] == expected);
    }
}
static void TestK1(void) { TestActivation(1U); }
static void TestK2(void) { TestActivation(2U); }
static void TestK4(void) { TestActivation(4U); }
static void TestK8(void) { TestActivation(8U); }

static void TestInvalidK(void)
{
    const uint32_t invalid[] = {0U, 3U, 5U, 6U, 7U, 9U, 10U, 16U, UINT32_MAX};
    size_t i;
    uint32_t active;
    for (active = 0U; active < 2U; ++active)
    {
        for (i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        {
            R2_BufferPoolSnapshot before;
            R2_BufferPool_Reset();
            if (active != 0U)
            {
                OK(R2_BufferPool_Activate(4U));
                OK(R2_BufferPool_CommitDmaRotation(0U, 2U));
                OK(R2_BufferPool_ClaimReady(0U));
            }
            before = Snap();
            CHECK(R2_BufferPool_Activate(invalid[i]) == R2_BUFFER_POOL_INVALID_K);
            UnchangedExceptViolation(&before, 1U);
        }
    }
}

static void TestFindFree(void)
{
    R2_BufferPoolSnapshot before;
    R2_BufferId id = R2_BUFFER_POOL_INVALID_ID;
    Activate(4U);
    before = Snap();
    OK(R2_BufferPool_FindFree(&id));
    CHECK(id == 2U);
    OK(R2_BufferPool_FindFree(&id));
    CHECK(id == 2U); /* The first query must NOT reserve B2. */
    UnchangedExceptViolation(&before, 0U);
    OK(R2_BufferPool_CommitDmaRotation(0U, id));
    OK(R2_BufferPool_Validate());
    OK(R2_BufferPool_FindFree(&id));
    CHECK(id == 3U);
    OK(R2_BufferPool_ClaimReady(0U));
    OK(R2_BufferPool_Validate());
    OK(R2_BufferPool_ReleaseProcessing(0U));
    OK(R2_BufferPool_Validate());
    OK(R2_BufferPool_FindFree(&id));
    CHECK(id == 0U);
}

static void TestRotation(void)
{
    size_t i;
    for (i = 0U; i < sizeof(ks) / sizeof(ks[0]); ++i)
    {
        R2_BufferPoolSnapshot s;
        Activate(ks[i]);
        OK(R2_BufferPool_CommitDmaRotation(0U, 2U));
        OK(R2_BufferPool_Validate());
        s = Snap();
        CHECK(s.states[0] == R2_BUFFER_STATE_READY);
        CHECK(s.states[1] == R2_BUFFER_STATE_DMA_OWNED);
        CHECK(s.states[2] == R2_BUFFER_STATE_DMA_OWNED);
        CHECK(s.free_count == ks[i] - 1U && s.ready_count == 1U);
        CHECK(s.dma_owned_count == 2U && s.processing_count == 0U);
    }
}

static void TestRoundTrip(void)
{
    R2_BufferPoolSnapshot s;
    Activate(1U);
    OK(R2_BufferPool_CommitDmaRotation(0U, 2U));
    OK(R2_BufferPool_Validate());
    OK(R2_BufferPool_ClaimReady(0U));
    OK(R2_BufferPool_Validate());
    s = Snap();
    CHECK(s.processing_count == 1U && s.ready_count == 0U);
    CHECK(s.free_count == 0U); /* PROCESSING still consumes Q. */
    OK(R2_BufferPool_ReleaseProcessing(0U));
    OK(R2_BufferPool_Validate());
    s = Snap();
    CHECK(s.states[0] == R2_BUFFER_STATE_FREE && s.free_count == 1U);
    CHECK(s.processing_count == 0U);
}

static void TestDoubleRelease(void)
{
    R2_BufferPoolSnapshot before;
    TestRoundTrip();
    before = Snap();
    CHECK(R2_BufferPool_ReleaseProcessing(0U) == R2_BUFFER_POOL_ILLEGAL_TRANSITION);
    UnchangedExceptViolation(&before, 1U);
}

static void TestClaimNonready(void)
{
    const R2_BufferId ids[] = {0U, 2U, 9U};
    size_t i;
    for (i = 0U; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        R2_BufferPoolSnapshot before;
        Activate(2U);
        before = Snap();
        CHECK(R2_BufferPool_ClaimReady(ids[i]) == R2_BUFFER_POOL_ILLEGAL_TRANSITION);
        UnchangedExceptViolation(&before, 1U);
    }
    Activate(1U);
    OK(R2_BufferPool_CommitDmaRotation(0U, 2U));
    OK(R2_BufferPool_ClaimReady(0U));
    {
        R2_BufferPoolSnapshot before = Snap();
        CHECK(R2_BufferPool_ClaimReady(0U) == R2_BUFFER_POOL_ILLEGAL_TRANSITION);
        UnchangedExceptViolation(&before, 1U);
    }
}

static void TestCompletedNotDma(void)
{
    R2_BufferPoolSnapshot before;
    Activate(4U);
    before = Snap();
    CHECK(R2_BufferPool_CommitDmaRotation(2U, 3U) == R2_BUFFER_POOL_ILLEGAL_TRANSITION);
    UnchangedExceptViolation(&before, 1U);
}

static void TestReplacementNotFree(void)
{
    R2_BufferPoolSnapshot before;
    Activate(4U);
    before = Snap();
    CHECK(R2_BufferPool_CommitDmaRotation(0U, 1U) == R2_BUFFER_POOL_ILLEGAL_TRANSITION);
    UnchangedExceptViolation(&before, 1U);
    OK(R2_BufferPool_CommitDmaRotation(0U, 2U));
    before = Snap();
    CHECK(R2_BufferPool_CommitDmaRotation(1U, 0U) == R2_BUFFER_POOL_ILLEGAL_TRANSITION);
    UnchangedExceptViolation(&before, 1U);
    OK(R2_BufferPool_ClaimReady(0U));
    before = Snap();
    CHECK(R2_BufferPool_CommitDmaRotation(1U, 0U) == R2_BUFFER_POOL_ILLEGAL_TRANSITION);
    UnchangedExceptViolation(&before, 1U);
}

static void TestSameId(void)
{
    uint32_t id;
    Activate(4U);
    for (id = 0U; id < 10U; ++id)
    {
        R2_BufferPoolSnapshot before = Snap();
        CHECK(R2_BufferPool_CommitDmaRotation((R2_BufferId)id, (R2_BufferId)id) ==
              R2_BUFFER_POOL_ILLEGAL_TRANSITION);
        UnchangedExceptViolation(&before, 1U);
    }
}

static void TestInvalidId(void)
{
    const R2_BufferId bad[] = {10U, 11U, 127U, 254U, R2_BUFFER_POOL_INVALID_ID};
    size_t i;
    Activate(8U);
    for (i = 0U; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        R2_BufferPoolSnapshot before;
        R2_BufferState state = R2_BUFFER_STATE_PROCESSING;
        before = Snap();
        CHECK(R2_BufferPool_CommitDmaRotation(bad[i], 2U) == R2_BUFFER_POOL_ID_OUT_OF_RANGE);
        UnchangedExceptViolation(&before, 1U);
        before = Snap();
        CHECK(R2_BufferPool_CommitDmaRotation(0U, bad[i]) == R2_BUFFER_POOL_ID_OUT_OF_RANGE);
        UnchangedExceptViolation(&before, 1U);
        before = Snap();
        CHECK(R2_BufferPool_ClaimReady(bad[i]) == R2_BUFFER_POOL_ID_OUT_OF_RANGE);
        UnchangedExceptViolation(&before, 1U);
        before = Snap();
        CHECK(R2_BufferPool_ReleaseProcessing(bad[i]) == R2_BUFFER_POOL_ID_OUT_OF_RANGE);
        UnchangedExceptViolation(&before, 1U);
        before = Snap();
        CHECK(R2_BufferPool_GetState(bad[i], &state) == R2_BUFFER_POOL_ID_OUT_OF_RANGE);
        CHECK(state == R2_BUFFER_STATE_PROCESSING);
        UnchangedExceptViolation(&before, 1U);
    }
}

static void TestExhaustion(void)
{
    size_t k_index;
    for (k_index = 0U; k_index < sizeof(ks) / sizeof(ks[0]); ++k_index)
    {
        uint32_t n;
        R2_BufferId dma_id = 0U;
        R2_BufferId old_ids[8];
        R2_BufferPoolSnapshot full;
        uint32_t k = ks[k_index];
        Activate(k);
        for (n = 0U; n < k; ++n)
        {
            R2_BufferId free_id = R2_BUFFER_POOL_INVALID_ID;
            OK(R2_BufferPool_FindFree(&free_id));
            old_ids[n] = dma_id;
            OK(R2_BufferPool_CommitDmaRotation(dma_id, free_id));
            dma_id = free_id;
            OK(R2_BufferPool_Validate());
        }
        full = Snap();
        CHECK(full.free_count == 0U && full.ready_count == k);
        /* Model 100 consecutive capacity-drop decisions: NO state transition. */
        for (n = 0U; n < 100U; ++n)
        {
            R2_BufferId output = R2_BUFFER_POOL_INVALID_ID;
            CHECK(R2_BufferPool_FindFree(&output) == R2_BUFFER_POOL_NO_FREE_BUFFER);
            CHECK(output == R2_BUFFER_POOL_INVALID_ID);
            UnchangedExceptViolation(&full, 0U);
        }
        for (n = 0U; n < k; ++n)
        {
            OK(R2_BufferPool_ClaimReady(old_ids[n]));
            OK(R2_BufferPool_Validate());
            OK(R2_BufferPool_ReleaseProcessing(old_ids[n]));
            OK(R2_BufferPool_Validate());
        }
        CHECK(Snap().free_count == k);
        CHECK(Snap().dma_owned_count == 2U);
    }
}

static void TestResetAfterActivity(void)
{
    TestExhaustion();
    R2_BufferPool_Reset();
    CheckReset();
    TestDoubleRelease();
    R2_BufferPool_Reset();
    CheckReset();
    TestActivation(8U);
}

static void TestQueries(void)
{
    uint32_t id;
    R2_BufferPoolSnapshot before;
    Activate(2U);
    before = Snap();
    for (id = 0U; id < 10U; ++id)
    {
        R2_BufferState state = R2_BUFFER_STATE_PROCESSING;
        OK(R2_BufferPool_GetState((R2_BufferId)id, &state));
        CHECK(state == before.states[id]);
        UnchangedExceptViolation(&before, 0U);
    }
}

static void TestNullArguments(void)
{
    R2_BufferPoolSnapshot before;
    Activate(4U);
    before = Snap();
    CHECK(R2_BufferPool_FindFree(NULL) == R2_BUFFER_POOL_INVALID_ARGUMENT);
    UnchangedExceptViolation(&before, 1U);
    before = Snap();
    CHECK(R2_BufferPool_GetState(0U, NULL) == R2_BUFFER_POOL_INVALID_ARGUMENT);
    UnchangedExceptViolation(&before, 1U);
    before = Snap();
    CHECK(R2_BufferPool_GetSnapshot(NULL) == R2_BUFFER_POOL_INVALID_ARGUMENT);
    UnchangedExceptViolation(&before, 1U);
}

static void TestUnactivated(void)
{
    R2_BufferPoolSnapshot before;
    R2_BufferId id = R2_BUFFER_POOL_INVALID_ID;
    R2_BufferState state = R2_BUFFER_STATE_PROCESSING;
    R2_BufferPool_Reset();
    before = Snap();
    CHECK(R2_BufferPool_FindFree(&id) == R2_BUFFER_POOL_NOT_ACTIVE);
    CHECK(id == R2_BUFFER_POOL_INVALID_ID);
    UnchangedExceptViolation(&before, 1U);
    before = Snap();
    CHECK(R2_BufferPool_CommitDmaRotation(0U, 2U) == R2_BUFFER_POOL_NOT_ACTIVE);
    UnchangedExceptViolation(&before, 1U);
    before = Snap();
    CHECK(R2_BufferPool_ClaimReady(0U) == R2_BUFFER_POOL_NOT_ACTIVE);
    UnchangedExceptViolation(&before, 1U);
    before = Snap();
    CHECK(R2_BufferPool_ReleaseProcessing(0U) == R2_BUFFER_POOL_NOT_ACTIVE);
    UnchangedExceptViolation(&before, 1U);
    OK(R2_BufferPool_GetState(0U, &state));
    CHECK(state == R2_BUFFER_STATE_INACTIVE);
}

static void TestReactivation(void)
{
    size_t i;
    size_t j;
    for (i = 0U; i < sizeof(ks) / sizeof(ks[0]); ++i)
    {
        for (j = 0U; j < sizeof(ks) / sizeof(ks[0]); ++j)
        {
            R2_BufferPoolSnapshot before;
            Activate(ks[i]);
            OK(R2_BufferPool_CommitDmaRotation(0U, 2U));
            OK(R2_BufferPool_ClaimReady(0U));
            before = Snap();
            CHECK(R2_BufferPool_Activate(ks[j]) == R2_BUFFER_POOL_ALREADY_ACTIVE);
            UnchangedExceptViolation(&before, 1U);
        }
    }
}

/* Exhaust all candidate ID pairs over reset-like and nontrivial active states. */
static void TestTransactionMatrix(void)
{
    const R2_BufferId ids[] = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 255U};
    size_t k_index;
    uint32_t fixture;
    size_t a;
    size_t b;
    for (k_index = 0U; k_index < sizeof(ks) / sizeof(ks[0]); ++k_index)
    {
        for (fixture = 0U; fixture < 3U; ++fixture)
        {
            for (a = 0U; a < sizeof(ids) / sizeof(ids[0]); ++a)
            {
                for (b = 0U; b < sizeof(ids) / sizeof(ids[0]); ++b)
                {
                    R2_BufferPoolSnapshot before;
                    R2_BufferPoolSnapshot expected;
                    R2_BufferPoolSnapshot after;
                    R2_BufferPoolStatus result;
                    int legal;
                    Activate(ks[k_index]);
                    if (fixture > 0U)
                    {
                        OK(R2_BufferPool_CommitDmaRotation(0U, 2U));
                    }
                    if (fixture > 1U)
                    {
                        OK(R2_BufferPool_ClaimReady(0U));
                    }
                    before = Snap();
                    legal = (ids[a] < 10U) && (ids[b] < 10U) &&
                        (ids[a] != ids[b]) &&
                        (before.states[ids[a]] == R2_BUFFER_STATE_DMA_OWNED) &&
                        (before.states[ids[b]] == R2_BUFFER_STATE_FREE);
                    result = R2_BufferPool_CommitDmaRotation(ids[a], ids[b]);
                    if (legal)
                    {
                        CHECK(result == R2_BUFFER_POOL_OK);
                        expected = before;
                        expected.states[ids[a]] = R2_BUFFER_STATE_READY;
                        expected.states[ids[b]] = R2_BUFFER_STATE_DMA_OWNED;
                        --expected.free_count;
                        ++expected.ready_count;
                        after = Snap();
                        SameOwnership(&expected, &after);
                        CHECK(after.violation_count == before.violation_count);
                    }
                    else
                    {
                        CHECK(result == ((ids[a] >= 10U || ids[b] >= 10U) ?
                            R2_BUFFER_POOL_ID_OUT_OF_RANGE : R2_BUFFER_POOL_ILLEGAL_TRANSITION));
                        UnchangedExceptViolation(&before, 1U);
                    }
                    OK(R2_BufferPool_Validate());
                }
            }
        }
    }
}

static uint32_t NextRandom(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void TestModelWalk(void)
{
    size_t k_index;
    for (k_index = 0U; k_index < sizeof(ks) / sizeof(ks[0]); ++k_index)
    {
        R2_BufferState model[10];
        uint32_t k = ks[k_index];
        uint32_t random_state = UINT32_C(0x52425731) + k;
        uint32_t violations = 0U;
        uint32_t step;
        uint32_t id;
        Activate(k);
        for (id = 0U; id < 10U; ++id)
        {
            model[id] = (id < 2U) ? R2_BUFFER_STATE_DMA_OWNED :
                ((id < k + 2U) ? R2_BUFFER_STATE_FREE : R2_BUFFER_STATE_INACTIVE);
        }
        for (step = 0U; step < 10000U; ++step)
        {
            uint32_t op = (NextRandom(&random_state) >> 16U) % 4U;
            R2_BufferId a = (R2_BufferId)((NextRandom(&random_state) >> 16U) % 12U);
            R2_BufferId b = (R2_BufferId)((NextRandom(&random_state) >> 16U) % 12U);
            R2_BufferPoolStatus expected;
            R2_BufferPoolStatus actual;
            R2_BufferPoolSnapshot snapshot;
            uint32_t counts[5] = {0U, 0U, 0U, 0U, 0U};
            if (op == 0U)
            {
                R2_BufferId output = R2_BUFFER_POOL_INVALID_ID;
                R2_BufferId lowest = R2_BUFFER_POOL_INVALID_ID;
                for (id = 0U; id < k + 2U; ++id)
                {
                    if (model[id] == R2_BUFFER_STATE_FREE)
                    {
                        lowest = (R2_BufferId)id;
                        break;
                    }
                }
                expected = (lowest == R2_BUFFER_POOL_INVALID_ID) ?
                    R2_BUFFER_POOL_NO_FREE_BUFFER : R2_BUFFER_POOL_OK;
                actual = R2_BufferPool_FindFree(&output);
                CHECK(output == lowest);
            }
            else if (op == 1U)
            {
                if ((a >= 10U) || (b >= 10U))
                {
                    expected = R2_BUFFER_POOL_ID_OUT_OF_RANGE;
                }
                else if ((a == b) || (model[a] != R2_BUFFER_STATE_DMA_OWNED) ||
                         (model[b] != R2_BUFFER_STATE_FREE))
                {
                    expected = R2_BUFFER_POOL_ILLEGAL_TRANSITION;
                }
                else
                {
                    expected = R2_BUFFER_POOL_OK;
                    model[a] = R2_BUFFER_STATE_READY;
                    model[b] = R2_BUFFER_STATE_DMA_OWNED;
                }
                actual = R2_BufferPool_CommitDmaRotation(a, b);
            }
            else
            {
                R2_BufferState from = (op == 2U) ?
                    R2_BUFFER_STATE_READY : R2_BUFFER_STATE_PROCESSING;
                if (a >= 10U)
                {
                    expected = R2_BUFFER_POOL_ID_OUT_OF_RANGE;
                }
                else if (model[a] != from)
                {
                    expected = R2_BUFFER_POOL_ILLEGAL_TRANSITION;
                }
                else
                {
                    expected = R2_BUFFER_POOL_OK;
                    model[a] = (op == 2U) ?
                        R2_BUFFER_STATE_PROCESSING : R2_BUFFER_STATE_FREE;
                }
                actual = (op == 2U) ? R2_BufferPool_ClaimReady(a) :
                    R2_BufferPool_ReleaseProcessing(a);
            }
            CHECK(actual == expected);
            if ((expected != R2_BUFFER_POOL_OK) &&
                (expected != R2_BUFFER_POOL_NO_FREE_BUFFER))
            {
                ++violations;
            }
            OK(R2_BufferPool_Validate());
            snapshot = Snap();
            CHECK(snapshot.activated == 1U && snapshot.k == k);
            CHECK(snapshot.violation_count == violations);
            CHECK(snapshot.active_count == k + 2U);
            for (id = 0U; id < 10U; ++id)
            {
                CHECK(snapshot.states[id] == model[id]);
                ++counts[(size_t)model[id]];
            }
            CHECK(snapshot.inactive_count == counts[R2_BUFFER_STATE_INACTIVE]);
            CHECK(snapshot.free_count == counts[R2_BUFFER_STATE_FREE]);
            CHECK(snapshot.dma_owned_count == counts[R2_BUFFER_STATE_DMA_OWNED]);
            CHECK(snapshot.ready_count == counts[R2_BUFFER_STATE_READY]);
            CHECK(snapshot.processing_count == counts[R2_BUFFER_STATE_PROCESSING]);
            CHECK(snapshot.dma_owned_count == 2U);
            CHECK(snapshot.free_count + snapshot.ready_count +
                  snapshot.processing_count == k);
        }
    }
}

static void TestSnapshotCopy(void)
{
    R2_BufferPoolSnapshot before;
    R2_BufferPoolSnapshot copy;
    Activate(2U);
    before = Snap();
    copy = Snap();
    copy.states[0] = R2_BUFFER_STATE_FREE;
    copy.k = 123U;
    copy.violation_count = 999U;
    CHECK(copy.k != before.k);
    UnchangedExceptViolation(&before, 0U);
}

typedef struct
{
    const char *name;
    void (*run)(void);
} TestCase;

static const TestCase cases[] =
{
    {"reset", TestReset}, {"activate_k1", TestK1}, {"activate_k2", TestK2},
    {"activate_k4", TestK4}, {"activate_k8", TestK8}, {"invalid_k", TestInvalidK},
    {"find_free", TestFindFree}, {"rotation", TestRotation},
    {"round_trip", TestRoundTrip}, {"double_release", TestDoubleRelease},
    {"claim_nonready", TestClaimNonready}, {"completed_not_dma", TestCompletedNotDma},
    {"replacement_not_free", TestReplacementNotFree}, {"same_id", TestSameId},
    {"invalid_id", TestInvalidId}, {"exhaustion", TestExhaustion},
    {"reset_after_activity", TestResetAfterActivity}, {"queries", TestQueries},
    {"null_arguments", TestNullArguments}, {"unactivated", TestUnactivated},
    {"reactivation", TestReactivation}, {"transaction_matrix", TestTransactionMatrix},
    {"deterministic_model_walk", TestModelWalk}, {"snapshot_copy", TestSnapshotCopy}
};

int main(int argc, char **argv)
{
    size_t i;
    int ran = 0;
    if ((argc == 2) && (strcmp(argv[1], "--assertion-probe") == 0))
    {
        CHECK(0); /* CTest expects nonzero; this must NEVER print PASS. */
    }
    if (argc > 2)
    {
        (void)fprintf(stderr, "Usage: test_r2_buffer_pool [case_name]\n");
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
        (void)fprintf(stderr, "Unknown test case: %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    (void)puts("R2-W1 BufferPool native tests: PASS");
    return EXIT_SUCCESS;
}
