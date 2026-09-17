#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "r2_buffer_pool.h"
#include "r2_dma_slots.h"
#include "r2_w5_guard.h"

typedef struct
{
    R2_BufferId data[R2_W5_K];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} IdQueue;

typedef struct
{
    uint32_t input_count;
    uint32_t admitted_count;
    uint32_t drop_count;
    uint32_t recovered_count;
    uint32_t current_drop_streak;
    uint32_t max_drop_streak;
} ModelStats;

static void IdInit(IdQueue *q)
{
    memset(q, 0, sizeof(*q));
}

static int IdPush(IdQueue *q, R2_BufferId id)
{
    if (q->count == R2_W5_K)
    {
        return 0;
    }
    q->data[q->tail] = id;
    q->tail = (q->tail + 1U) % R2_W5_K;
    ++q->count;
    return 1;
}

static int IdPop(IdQueue *q, R2_BufferId *id)
{
    if (q->count == 0U)
    {
        return 0;
    }
    *id = q->data[q->head];
    q->head = (q->head + 1U) % R2_W5_K;
    --q->count;
    return 1;
}

static void Setup(IdQueue *free_q)
{
    R2_BufferPool_Reset();
    R2_DmaSlots_Reset();
    assert(R2_BufferPool_Activate(R2_W5_K) == R2_BUFFER_POOL_OK);
    assert(R2_DmaSlots_Initialize(0U, 1U) == R2_DMA_SLOTS_OK);
    IdInit(free_q);
    assert(IdPush(free_q, 2U));
    assert(R2_BufferPool_Validate() == R2_BUFFER_POOL_OK);
    assert(R2_DmaSlots_Validate() == R2_DMA_SLOTS_OK);
}

static R2_BufferId Admit(uint32_t sequence, IdQueue *free_q)
{
    R2_BufferId replacement;
    R2_DmaSlotsObservation observation;
    R2_DmaSlotsRebindPlan plan;
    uint32_t ct = sequence & 1U;

    assert(IdPop(free_q, &replacement));
    assert(R2_DmaSlots_ObserveCt(ct, &observation) == R2_DMA_SLOTS_OK);
    assert(R2_DmaSlots_PrepareInactiveRebind(ct, replacement, &plan) ==
        R2_DMA_SLOTS_OK);
    assert(R2_DmaSlots_CheckPlanCt(&plan, ct) == R2_DMA_SLOTS_OK);
    assert(R2_DmaSlots_CommitPreparedRebind(&plan) == R2_DMA_SLOTS_OK);
    assert(R2_BufferPool_CommitDmaRotation(observation.completed_buffer,
        replacement) == R2_BUFFER_POOL_OK);
    assert(R2_BufferPool_Validate() == R2_BUFFER_POOL_OK);
    assert(R2_DmaSlots_Validate() == R2_DMA_SLOTS_OK);
    return observation.completed_buffer;
}

static void Claim(R2_BufferId id)
{
    assert(R2_BufferPool_ClaimReady(id) == R2_BUFFER_POOL_OK);
    assert(R2_BufferPool_Validate() == R2_BUFFER_POOL_OK);
}

static void Release(R2_BufferId id, IdQueue *free_q)
{
    assert(R2_BufferPool_ReleaseProcessing(id) == R2_BUFFER_POOL_OK);
    assert(IdPush(free_q, id));
    assert(R2_BufferPool_Validate() == R2_BUFFER_POOL_OK);
}

static R2_BufferId CompletedBuffer(uint32_t sequence)
{
    R2_DmaSlotsObservation observation;
    assert(R2_DmaSlots_ObserveCt(sequence & 1U, &observation) ==
        R2_DMA_SLOTS_OK);
    return observation.completed_buffer;
}

static void AssertDropNoMutation(uint32_t sequence, const IdQueue *free_q)
{
    R2_BufferId dummy = R2_BUFFER_POOL_INVALID_ID;
    R2_BufferPoolSnapshot pool_before;
    R2_BufferPoolSnapshot pool_after;
    R2_DmaSlotsSnapshot slots_before;
    R2_DmaSlotsSnapshot slots_after;
    R2_BufferState state;
    R2_BufferId completed;

    assert(free_q->count == 0U);
    assert(R2_BufferPool_GetSnapshot(&pool_before) == R2_BUFFER_POOL_OK);
    assert(R2_DmaSlots_GetSnapshot(&slots_before) == R2_DMA_SLOTS_OK);

    completed = CompletedBuffer(sequence);
    assert(R2_BufferPool_GetState(completed, &state) == R2_BUFFER_POOL_OK);
    assert(state == R2_BUFFER_STATE_DMA_OWNED);

    assert(!IdPop((IdQueue *)free_q, &dummy));

    assert(R2_BufferPool_GetSnapshot(&pool_after) == R2_BUFFER_POOL_OK);
    assert(R2_DmaSlots_GetSnapshot(&slots_after) == R2_DMA_SLOTS_OK);
    assert(memcmp(&pool_before, &pool_after, sizeof(pool_before)) == 0);
    assert(memcmp(&slots_before, &slots_after, sizeof(slots_before)) == 0);
}

static void CaseDropNoMutation(void)
{
    IdQueue free_q;
    R2_BufferId processing;

    Setup(&free_q);
    processing = Admit(1U, &free_q);
    Claim(processing);
    AssertDropNoMutation(2U, &free_q);
}

static void CaseDropPreservesEpoch(void)
{
    IdQueue free_q;
    R2_BufferId processing;
    R2_DmaSlotsSnapshot before;
    R2_DmaSlotsSnapshot after;

    Setup(&free_q);
    processing = Admit(1U, &free_q);
    Claim(processing);
    assert(R2_DmaSlots_GetSnapshot(&before) == R2_DMA_SLOTS_OK);
    AssertDropNoMutation(2U, &free_q);
    AssertDropNoMutation(3U, &free_q);
    assert(R2_DmaSlots_GetSnapshot(&after) == R2_DMA_SLOTS_OK);
    assert(after.mapping_epoch == before.mapping_epoch);
}

static void CaseConsecutiveDrops(void)
{
    uint32_t sequence;
    IdQueue free_q;
    R2_BufferId processing;

    Setup(&free_q);
    processing = Admit(1U, &free_q);
    Claim(processing);
    for (sequence = 2U; sequence <= 6U; ++sequence)
    {
        AssertDropNoMutation(sequence, &free_q);
    }
}

static void CaseReleaseRecovery(void)
{
    IdQueue free_q;
    R2_BufferId processing;
    R2_BufferId next_processing;

    Setup(&free_q);
    processing = Admit(1U, &free_q);
    Claim(processing);
    AssertDropNoMutation(2U, &free_q);
    AssertDropNoMutation(3U, &free_q);
    Release(processing, &free_q);
    next_processing = Admit(4U, &free_q);
    Claim(next_processing);
    assert(free_q.count == 0U);
}

static void CaseConservationAcrossDrops(void)
{
    IdQueue free_q;
    R2_BufferId processing;
    R2_BufferPoolSnapshot pool;

    Setup(&free_q);
    processing = Admit(1U, &free_q);
    Claim(processing);
    AssertDropNoMutation(2U, &free_q);
    assert(R2_BufferPool_GetSnapshot(&pool) == R2_BUFFER_POOL_OK);
    assert(pool.dma_owned_count == 2U);
    assert(pool.processing_count == 1U);
    assert(pool.ready_count == 0U);
    assert(pool.free_count == 0U);
    assert(pool.dma_owned_count + pool.processing_count == R2_W5_BUFFERS);
}

static void CaseDropNoViolation(void)
{
    IdQueue free_q;
    R2_BufferId processing;
    R2_BufferPoolSnapshot pool;
    R2_DmaSlotsSnapshot slots;

    Setup(&free_q);
    processing = Admit(1U, &free_q);
    Claim(processing);
    AssertDropNoMutation(2U, &free_q);
    AssertDropNoMutation(3U, &free_q);
    assert(R2_BufferPool_GetSnapshot(&pool) == R2_BUFFER_POOL_OK);
    assert(R2_DmaSlots_GetSnapshot(&slots) == R2_DMA_SLOTS_OK);
    assert(pool.violation_count == 0U);
    assert(slots.violation_count == 0U);
}

static void CaseGuardSafe(void)
{
    assert(R2_W5_CheckWindow(1U, 1U, 1U, 230400U + 100U,
        250U, 1U) == R2_W5_GUARD_OK);
}

static void CaseGuardLate(void)
{
    assert(R2_W5_CheckWindow(1U, 1U, 1U,
        230400U + R2_W5_WRITE_LIMIT_CYCLES, 250U, 1U) ==
        R2_W5_GUARD_TIME);
}

static void CaseGuardNdtr(void)
{
    assert(R2_W5_CheckWindow(1U, 1U, 1U, 230400U + 100U,
        R2_W5_MIN_REMAINING_SAMPLES - 1U, 1U) == R2_W5_GUARD_NDTR);
}

static void CaseGuardCt(void)
{
    assert(R2_W5_CheckWindow(2U, 0U, 1U, 460800U + 100U,
        250U, 1U) == R2_W5_GUARD_CT);
}

static void CaseDroppedBufferRemainsDmaOwned(void)
{
    IdQueue free_q;
    R2_BufferId processing;
    R2_BufferId completed;
    R2_BufferState state;

    Setup(&free_q);
    processing = Admit(1U, &free_q);
    Claim(processing);
    completed = CompletedBuffer(2U);
    AssertDropNoMutation(2U, &free_q);
    assert(R2_BufferPool_GetState(completed, &state) == R2_BUFFER_POOL_OK);
    assert(state == R2_BUFFER_STATE_DMA_OWNED);
}

static void CaseDeterministic64(void)
{
    uint32_t sequence;
    uint32_t release_at = 0U;
    R2_BufferId processing = R2_BUFFER_POOL_INVALID_ID;
    IdQueue free_q;
    ModelStats stats;

    memset(&stats, 0, sizeof(stats));
    Setup(&free_q);

    for (sequence = 1U; sequence <= R2_W5_EVENTS; ++sequence)
    {
        if ((processing != R2_BUFFER_POOL_INVALID_ID) &&
            (sequence == release_at))
        {
            Release(processing, &free_q);
            processing = R2_BUFFER_POOL_INVALID_ID;
        }

        ++stats.input_count;
        if (free_q.count != 0U)
        {
            processing = Admit(sequence, &free_q);
            Claim(processing);
            ++stats.admitted_count;
            if (stats.current_drop_streak != 0U)
            {
                ++stats.recovered_count;
                stats.current_drop_streak = 0U;
            }
            release_at = sequence + R2_W5_PROCESS_HOLD_BLOCKS;
        }
        else
        {
            AssertDropNoMutation(sequence, &free_q);
            ++stats.drop_count;
            ++stats.current_drop_streak;
            if (stats.current_drop_streak > stats.max_drop_streak)
            {
                stats.max_drop_streak = stats.current_drop_streak;
            }
        }
    }

    if (processing != R2_BUFFER_POOL_INVALID_ID)
    {
        Release(processing, &free_q);
    }

    assert(stats.input_count == R2_W5_EVENTS);
    assert(stats.admitted_count + stats.drop_count == R2_W5_EVENTS);
    assert(stats.admitted_count >= R2_W5_MIN_ADMISSIONS);
    assert(stats.drop_count >= R2_W5_MIN_CAPACITY_DROPS);
    assert(stats.max_drop_streak >= R2_W5_MIN_DROP_STREAK);
    assert(stats.recovered_count >= R2_W5_MIN_RECOVERIES);
    assert(free_q.count == R2_W5_K);
    assert(R2_BufferPool_Validate() == R2_BUFFER_POOL_OK);
    assert(R2_DmaSlots_Validate() == R2_DMA_SLOTS_OK);
}

static void RunCase(const char *name)
{
    if (strcmp(name, "drop_no_mutation") == 0) CaseDropNoMutation();
    else if (strcmp(name, "drop_preserves_epoch") == 0) CaseDropPreservesEpoch();
    else if (strcmp(name, "consecutive_drops") == 0) CaseConsecutiveDrops();
    else if (strcmp(name, "release_recovery") == 0) CaseReleaseRecovery();
    else if (strcmp(name, "conservation") == 0) CaseConservationAcrossDrops();
    else if (strcmp(name, "drop_no_violation") == 0) CaseDropNoViolation();
    else if (strcmp(name, "guard_safe") == 0) CaseGuardSafe();
    else if (strcmp(name, "guard_late") == 0) CaseGuardLate();
    else if (strcmp(name, "guard_ndtr") == 0) CaseGuardNdtr();
    else if (strcmp(name, "guard_ct") == 0) CaseGuardCt();
    else if (strcmp(name, "drop_dma_owned") == 0) CaseDroppedBufferRemainsDmaOwned();
    else if (strcmp(name, "deterministic_64") == 0) CaseDeterministic64();
    else
    {
        fprintf(stderr, "unknown test case: %s\n", name);
        exit(2);
    }
}

int main(int argc, char **argv)
{
    if (argc == 1)
    {
        static const char *cases[] = {
            "drop_no_mutation", "drop_preserves_epoch", "consecutive_drops",
            "release_recovery", "conservation", "drop_no_violation",
            "guard_safe", "guard_late", "guard_ndtr", "guard_ct",
            "drop_dma_owned", "deterministic_64"
        };
        size_t i;
        for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
        {
            RunCase(cases[i]);
        }
        puts("R2-W5 capacity-drop model native tests: PASS");
        return 0;
    }
    if (argc == 2)
    {
        RunCase(argv[1]);
        return 0;
    }
    return 2;
}
