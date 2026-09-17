#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "r2_buffer_pool.h"
#include "r2_dma_slots.h"
#include "r2_w6_guard.h"

typedef struct
{
    R2_BufferId data[8];
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
} MatrixStats;

static void QueueInit(IdQueue *queue)
{
    memset(queue, 0, sizeof(*queue));
}

static int QueuePush(IdQueue *queue, R2_BufferId id)
{
    if (queue->count == R2_W6_K)
    {
        return 0;
    }

    queue->data[queue->tail] = id;
    queue->tail = (queue->tail + 1U) % R2_W6_K;
    ++queue->count;
    return 1;
}

static int QueuePop(IdQueue *queue, R2_BufferId *id)
{
    if (queue->count == 0U)
    {
        return 0;
    }

    *id = queue->data[queue->head];
    queue->head = (queue->head + 1U) % R2_W6_K;
    --queue->count;
    return 1;
}

static void Setup(IdQueue *free_queue, IdQueue *ready_queue)
{
    uint32_t id;

    R2_BufferPool_Reset();
    R2_DmaSlots_Reset();

    assert(R2_BufferPool_Activate(R2_W6_K) == R2_BUFFER_POOL_OK);
    assert(R2_DmaSlots_Initialize(0U, 1U) == R2_DMA_SLOTS_OK);

    QueueInit(free_queue);
    QueueInit(ready_queue);

    for (id = 2U; id < R2_W6_BUFFERS; ++id)
    {
        assert(QueuePush(free_queue, (R2_BufferId)id));
    }

    assert(free_queue->count == R2_W6_K);
    assert(R2_BufferPool_Validate() == R2_BUFFER_POOL_OK);
    assert(R2_DmaSlots_Validate() == R2_DMA_SLOTS_OK);
}

static R2_BufferId Admit(uint32_t sequence, IdQueue *free_queue,
    IdQueue *ready_queue)
{
    R2_BufferId replacement;
    R2_DmaSlotsObservation observation;
    R2_DmaSlotsRebindPlan plan;
    uint32_t ct = sequence & 1U;

    assert(QueuePop(free_queue, &replacement));
    assert(R2_DmaSlots_ObserveCt(ct, &observation) == R2_DMA_SLOTS_OK);
    assert(R2_DmaSlots_PrepareInactiveRebind(ct, replacement, &plan) ==
        R2_DMA_SLOTS_OK);
    assert(R2_DmaSlots_CheckPlanCt(&plan, ct) == R2_DMA_SLOTS_OK);
    assert(R2_DmaSlots_CommitPreparedRebind(&plan) == R2_DMA_SLOTS_OK);
    assert(R2_BufferPool_CommitDmaRotation(observation.completed_buffer,
        replacement) == R2_BUFFER_POOL_OK);
    assert(QueuePush(ready_queue, observation.completed_buffer));
    assert(R2_BufferPool_Validate() == R2_BUFFER_POOL_OK);
    assert(R2_DmaSlots_Validate() == R2_DMA_SLOTS_OK);

    return observation.completed_buffer;
}

static R2_BufferId ClaimNext(IdQueue *ready_queue)
{
    R2_BufferId id;

    if (!QueuePop(ready_queue, &id))
    {
        return R2_BUFFER_POOL_INVALID_ID;
    }

    assert(R2_BufferPool_ClaimReady(id) == R2_BUFFER_POOL_OK);
    assert(R2_BufferPool_Validate() == R2_BUFFER_POOL_OK);
    return id;
}

static void Release(R2_BufferId id, IdQueue *free_queue)
{
    assert(R2_BufferPool_ReleaseProcessing(id) == R2_BUFFER_POOL_OK);
    assert(QueuePush(free_queue, id));
    assert(R2_BufferPool_Validate() == R2_BUFFER_POOL_OK);
}

static void AssertDropNoMutation(uint32_t sequence,
    const IdQueue *free_queue)
{
    R2_BufferPoolSnapshot pool_before;
    R2_BufferPoolSnapshot pool_after;
    R2_DmaSlotsSnapshot slots_before;
    R2_DmaSlotsSnapshot slots_after;
    R2_DmaSlotsObservation observation;
    R2_BufferState state;

    assert(free_queue->count == 0U);
    assert(R2_BufferPool_GetSnapshot(&pool_before) == R2_BUFFER_POOL_OK);
    assert(R2_DmaSlots_GetSnapshot(&slots_before) == R2_DMA_SLOTS_OK);
    assert(R2_DmaSlots_ObserveCt(sequence & 1U, &observation) ==
        R2_DMA_SLOTS_OK);
    assert(R2_BufferPool_GetState(observation.completed_buffer, &state) ==
        R2_BUFFER_POOL_OK);
    assert(state == R2_BUFFER_STATE_DMA_OWNED);

    assert(R2_BufferPool_GetSnapshot(&pool_after) == R2_BUFFER_POOL_OK);
    assert(R2_DmaSlots_GetSnapshot(&slots_after) == R2_DMA_SLOTS_OK);
    assert(memcmp(&pool_before, &pool_after, sizeof(pool_before)) == 0);
    assert(memcmp(&slots_before, &slots_after, sizeof(slots_before)) == 0);
}

static void Drain(IdQueue *ready_queue, IdQueue *free_queue,
    R2_BufferId *processing)
{
    if (*processing != R2_BUFFER_POOL_INVALID_ID)
    {
        Release(*processing, free_queue);
        *processing = R2_BUFFER_POOL_INVALID_ID;
    }

    for (;;)
    {
        R2_BufferId next = ClaimNext(ready_queue);

        if (next == R2_BUFFER_POOL_INVALID_ID)
        {
            break;
        }

        Release(next, free_queue);
    }
}

static MatrixStats RunMatrix(void)
{
    uint32_t sequence;
    uint32_t release_at = 0U;
    R2_BufferId processing = R2_BUFFER_POOL_INVALID_ID;
    IdQueue free_queue;
    IdQueue ready_queue;
    MatrixStats stats;

    memset(&stats, 0, sizeof(stats));
    Setup(&free_queue, &ready_queue);

    for (sequence = 1U; sequence <= R2_W6_EVENTS; ++sequence)
    {
        if ((processing != R2_BUFFER_POOL_INVALID_ID) &&
            (R2_W6_DROP_MODE != 0U) &&
            (sequence == release_at))
        {
            Release(processing, &free_queue);
            processing = R2_BUFFER_POOL_INVALID_ID;

            processing = ClaimNext(&ready_queue);
            if (processing != R2_BUFFER_POOL_INVALID_ID)
            {
                release_at = sequence + R2_W6_PROCESS_HOLD_BLOCKS;
            }
        }

        ++stats.input_count;

        if (free_queue.count != 0U)
        {
            (void)Admit(sequence, &free_queue, &ready_queue);
            ++stats.admitted_count;

            if (stats.current_drop_streak != 0U)
            {
                ++stats.recovered_count;
                stats.current_drop_streak = 0U;
            }

            if (processing == R2_BUFFER_POOL_INVALID_ID)
            {
                processing = ClaimNext(&ready_queue);

                if (processing != R2_BUFFER_POOL_INVALID_ID)
                {
                    if (R2_W6_DROP_MODE == 0U)
                    {
                        Release(processing, &free_queue);
                        processing = R2_BUFFER_POOL_INVALID_ID;
                    }
                    else
                    {
                        release_at = sequence + R2_W6_PROCESS_HOLD_BLOCKS;
                    }
                }
            }
        }
        else
        {
            AssertDropNoMutation(sequence, &free_queue);
            ++stats.drop_count;
            ++stats.current_drop_streak;

            if (stats.current_drop_streak > stats.max_drop_streak)
            {
                stats.max_drop_streak = stats.current_drop_streak;
            }
        }
    }

    Drain(&ready_queue, &free_queue, &processing);

    assert(stats.input_count == R2_W6_EVENTS);
    assert(stats.admitted_count + stats.drop_count == R2_W6_EVENTS);
    assert(free_queue.count == R2_W6_K);
    assert(ready_queue.count == 0U);
    assert(R2_BufferPool_Validate() == R2_BUFFER_POOL_OK);
    assert(R2_DmaSlots_Validate() == R2_DMA_SLOTS_OK);

    if (R2_W6_DROP_MODE == 0U)
    {
        assert(stats.admitted_count == R2_W6_EVENTS);
        assert(stats.drop_count == 0U);
        assert(stats.recovered_count == 0U);
        assert(stats.max_drop_streak == 0U);
    }
    else
    {
        assert(stats.admitted_count >= R2_W6_MIN_ADMISSIONS);
        assert(stats.drop_count >= R2_W6_MIN_CAPACITY_DROPS);
        assert(stats.recovered_count >= R2_W6_MIN_RECOVERIES);
        assert(stats.max_drop_streak >= R2_W6_MIN_DROP_STREAK);
    }

    return stats;
}

static void CaseConfiguration(void)
{
    assert((R2_W6_K == 1U) || (R2_W6_K == 2U) ||
        (R2_W6_K == 4U) || (R2_W6_K == 8U));
    assert(R2_W6_BUFFERS == R2_W6_K + 2U);
    assert(R2_W6_EVENTS == 96U);

    if (R2_W6_DROP_MODE == 0U)
    {
        assert(R2_W6_PROCESS_HOLD_BLOCKS == 0U);
    }
    else
    {
        assert(R2_W6_PROCESS_HOLD_BLOCKS == R2_W6_K + 5U);
    }
}

static void CaseGuard(void)
{
    assert(R2_W6_CheckWindow(1U, 1U, 1U,
        R2_W6_BLOCK_CYCLES + 100U, 250U, 1U) == R2_W6_GUARD_OK);
    assert(R2_W6_CheckWindow(1U, 1U, 1U,
        R2_W6_BLOCK_CYCLES + R2_W6_WRITE_LIMIT_CYCLES,
        250U, 1U) == R2_W6_GUARD_TIME);
    assert(R2_W6_CheckWindow(2U, 0U, 1U,
        2U * R2_W6_BLOCK_CYCLES + 100U, 250U, 1U) ==
        R2_W6_GUARD_CT);
}

static void CaseMatrix(void)
{
    (void)RunMatrix();
}

static void CaseFinalConservation(void)
{
    R2_BufferPoolSnapshot pool;
    R2_DmaSlotsSnapshot slots;

    (void)RunMatrix();

    assert(R2_BufferPool_GetSnapshot(&pool) == R2_BUFFER_POOL_OK);
    assert(R2_DmaSlots_GetSnapshot(&slots) == R2_DMA_SLOTS_OK);
    assert(pool.k == R2_W6_K);
    assert(pool.active_count == R2_W6_BUFFERS);
    assert(pool.free_count == R2_W6_K);
    assert(pool.dma_owned_count == 2U);
    assert(pool.ready_count == 0U);
    assert(pool.processing_count == 0U);
    assert(pool.violation_count == 0U);
    assert(slots.violation_count == 0U);
    assert(slots.m0_buffer != slots.m1_buffer);
}

static void RunCase(const char *name)
{
    if (strcmp(name, "configuration") == 0) CaseConfiguration();
    else if (strcmp(name, "guard") == 0) CaseGuard();
    else if (strcmp(name, "matrix") == 0) CaseMatrix();
    else if (strcmp(name, "final_conservation") == 0) CaseFinalConservation();
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
            "configuration", "guard", "matrix", "final_conservation"
        };
        size_t i;

        for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
        {
            RunCase(cases[i]);
        }

        printf("R2-W6 matrix model native tests: PASS (K=%u, mode=%s)\n",
            (unsigned)R2_W6_K,
            R2_W6_DROP_MODE != 0U ? "DROP" : "NORMAL");
        return 0;
    }

    if (argc == 2)
    {
        RunCase(argv[1]);
        return 0;
    }

    return 2;
}
