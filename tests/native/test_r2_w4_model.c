#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "r2_buffer_pool.h"
#include "r2_dma_slots.h"
#include "r2_w4_guard.h"

typedef struct
{
    R2_BufferId data[R2_W4_K];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} IdQueue;

typedef struct
{
    uint32_t sequence;
    R2_BufferId id;
} Desc;

typedef struct
{
    Desc data[R2_W4_K];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} DescQueue;

static void IdInit(IdQueue *q)
{
    memset(q, 0, sizeof(*q));
}

static int IdPush(IdQueue *q, R2_BufferId id)
{
    if (q->count == R2_W4_K)
    {
        return 0;
    }
    q->data[q->tail] = id;
    q->tail = (q->tail + 1U) % R2_W4_K;
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
    q->head = (q->head + 1U) % R2_W4_K;
    --q->count;
    return 1;
}

static void DescInit(DescQueue *q)
{
    memset(q, 0, sizeof(*q));
}

static int DescPush(DescQueue *q, Desc d)
{
    if (q->count == R2_W4_K)
    {
        return 0;
    }
    q->data[q->tail] = d;
    q->tail = (q->tail + 1U) % R2_W4_K;
    ++q->count;
    return 1;
}

static int DescPop(DescQueue *q, Desc *d)
{
    if (q->count == 0U)
    {
        return 0;
    }
    *d = q->data[q->head];
    q->head = (q->head + 1U) % R2_W4_K;
    --q->count;
    return 1;
}

static void Setup(IdQueue *free_q, DescQueue *ready_q)
{
    uint32_t id;

    R2_BufferPool_Reset();
    R2_DmaSlots_Reset();
    assert(R2_BufferPool_Activate(R2_W4_K) == R2_BUFFER_POOL_OK);
    assert(R2_DmaSlots_Initialize(0U, 1U) == R2_DMA_SLOTS_OK);

    IdInit(free_q);
    DescInit(ready_q);

    for (id = 2U; id < R2_W4_BUFFERS; ++id)
    {
        assert(IdPush(free_q, (R2_BufferId)id));
    }

    assert(R2_BufferPool_Validate() == R2_BUFFER_POOL_OK);
    assert(R2_DmaSlots_Validate() == R2_DMA_SLOTS_OK);
}

static void AdmitOne(uint32_t sequence, IdQueue *free_q, DescQueue *ready_q)
{
    uint32_t ct = sequence & 1U;
    R2_BufferId replacement;
    R2_DmaSlotsObservation observation;
    R2_DmaSlotsRebindPlan plan;
    Desc d;

    assert(IdPop(free_q, &replacement));
    assert(R2_DmaSlots_ObserveCt(ct, &observation) == R2_DMA_SLOTS_OK);
    assert(R2_DmaSlots_PrepareInactiveRebind(ct, replacement, &plan) == R2_DMA_SLOTS_OK);
    assert(R2_DmaSlots_CheckPlanCt(&plan, ct) == R2_DMA_SLOTS_OK);
    assert(R2_DmaSlots_CommitPreparedRebind(&plan) == R2_DMA_SLOTS_OK);
    assert(R2_BufferPool_CommitDmaRotation(observation.completed_buffer,
        replacement) == R2_BUFFER_POOL_OK);

    d.sequence = sequence;
    d.id = observation.completed_buffer;
    assert(DescPush(ready_q, d));

    assert(R2_BufferPool_Validate() == R2_BUFFER_POOL_OK);
    assert(R2_DmaSlots_Validate() == R2_DMA_SLOTS_OK);
}

static void ProcessOne(uint32_t expected_sequence, IdQueue *free_q, DescQueue *ready_q)
{
    Desc d;

    assert(DescPop(ready_q, &d));
    assert(d.sequence == expected_sequence);
    assert(R2_BufferPool_ClaimReady(d.id) == R2_BUFFER_POOL_OK);
    assert(R2_BufferPool_ReleaseProcessing(d.id) == R2_BUFFER_POOL_OK);
    assert(IdPush(free_q, d.id));

    assert(R2_BufferPool_Validate() == R2_BUFFER_POOL_OK);
}

static void CaseRoundTrip64(void)
{
    uint32_t sequence;
    IdQueue free_q;
    DescQueue ready_q;
    R2_BufferPoolSnapshot pool;
    R2_DmaSlotsSnapshot slots;

    Setup(&free_q, &ready_q);

    for (sequence = 1U; sequence <= R2_W4_EVENTS; ++sequence)
    {
        AdmitOne(sequence, &free_q, &ready_q);
        ProcessOne(sequence, &free_q, &ready_q);
        assert(free_q.count == R2_W4_K);
        assert(ready_q.count == 0U);
    }

    assert(R2_BufferPool_GetSnapshot(&pool) == R2_BUFFER_POOL_OK);
    assert(R2_DmaSlots_GetSnapshot(&slots) == R2_DMA_SLOTS_OK);
    assert(pool.free_count == R2_W4_K);
    assert(pool.ready_count == 0U);
    assert(pool.processing_count == 0U);
    assert(pool.dma_owned_count == 2U);
    assert(pool.states[slots.m0_buffer] == R2_BUFFER_STATE_DMA_OWNED);
    assert(pool.states[slots.m1_buffer] == R2_BUFFER_STATE_DMA_OWNED);
}

static void CaseFifoRotation(void)
{
    static const uint8_t expected_replacement[10] = {2U,3U,4U,5U,0U,1U,2U,3U,4U,5U};
    uint32_t sequence;
    IdQueue free_q;
    DescQueue ready_q;
    R2_BufferId replacement;
    R2_DmaSlotsObservation observation;
    R2_DmaSlotsRebindPlan plan;
    Desc d;

    Setup(&free_q, &ready_q);

    for (sequence = 1U; sequence <= 10U; ++sequence)
    {
        uint32_t ct = sequence & 1U;
        assert(IdPop(&free_q, &replacement));
        assert(replacement == expected_replacement[sequence - 1U]);
        assert(R2_DmaSlots_ObserveCt(ct, &observation) == R2_DMA_SLOTS_OK);
        assert(R2_DmaSlots_PrepareInactiveRebind(ct, replacement, &plan) == R2_DMA_SLOTS_OK);
        assert(R2_DmaSlots_CommitPreparedRebind(&plan) == R2_DMA_SLOTS_OK);
        assert(R2_BufferPool_CommitDmaRotation(observation.completed_buffer,
            replacement) == R2_BUFFER_POOL_OK);
        d.sequence = sequence;
        d.id = observation.completed_buffer;
        assert(DescPush(&ready_q, d));
        ProcessOne(sequence, &free_q, &ready_q);
    }
}

static void CaseConservation(void)
{
    uint32_t sequence;
    IdQueue free_q;
    DescQueue ready_q;
    R2_BufferPoolSnapshot pool;

    Setup(&free_q, &ready_q);

    for (sequence = 1U; sequence <= 32U; ++sequence)
    {
        AdmitOne(sequence, &free_q, &ready_q);
        assert(R2_BufferPool_GetSnapshot(&pool) == R2_BUFFER_POOL_OK);
        assert(free_q.count + pool.ready_count + pool.processing_count == R2_W4_K);
        ProcessOne(sequence, &free_q, &ready_q);
        assert(R2_BufferPool_GetSnapshot(&pool) == R2_BUFFER_POOL_OK);
        assert(free_q.count + pool.ready_count + pool.processing_count == R2_W4_K);
    }
}

static void CaseExhaustionBoundary(void)
{
    uint32_t sequence;
    IdQueue free_q;
    DescQueue ready_q;
    R2_BufferId id;
    R2_BufferPoolSnapshot before;
    R2_BufferPoolSnapshot after;
    R2_DmaSlotsSnapshot slots_before;
    R2_DmaSlotsSnapshot slots_after;

    Setup(&free_q, &ready_q);

    for (sequence = 1U; sequence <= R2_W4_K; ++sequence)
    {
        AdmitOne(sequence, &free_q, &ready_q);
    }

    assert(free_q.count == 0U);
    assert(!IdPop(&free_q, &id));
    assert(R2_BufferPool_GetSnapshot(&before) == R2_BUFFER_POOL_OK);
    assert(R2_DmaSlots_GetSnapshot(&slots_before) == R2_DMA_SLOTS_OK);
    assert(R2_BufferPool_GetSnapshot(&after) == R2_BUFFER_POOL_OK);
    assert(R2_DmaSlots_GetSnapshot(&slots_after) == R2_DMA_SLOTS_OK);
    assert(memcmp(&before, &after, sizeof(before)) == 0);
    assert(memcmp(&slots_before, &slots_after, sizeof(slots_before)) == 0);
}

static void CaseGuardSafe(void)
{
    assert(R2_W4_CheckWindow(1U, 1U, 1U, 230400U + 100U,
        250U, 1U) == R2_W4_GUARD_OK);
}

static void CaseGuardLate(void)
{
    assert(R2_W4_CheckWindow(1U, 1U, 1U,
        230400U + R2_W4_WRITE_LIMIT_CYCLES, 250U, 1U) == R2_W4_GUARD_TIME);
}

static void CaseGuardNdtr(void)
{
    assert(R2_W4_CheckWindow(1U, 1U, 1U, 230400U + 100U,
        R2_W4_MIN_REMAINING_SAMPLES - 1U, 1U) == R2_W4_GUARD_NDTR);
}

static void CaseGuardCt(void)
{
    assert(R2_W4_CheckWindow(2U, 0U, 1U, 460800U + 100U,
        250U, 1U) == R2_W4_GUARD_CT);
}

static void CaseProcessingState(void)
{
    IdQueue free_q;
    DescQueue ready_q;
    Desc d;
    R2_BufferState state;

    Setup(&free_q, &ready_q);
    AdmitOne(1U, &free_q, &ready_q);
    assert(DescPop(&ready_q, &d));
    assert(R2_BufferPool_ClaimReady(d.id) == R2_BUFFER_POOL_OK);
    assert(R2_BufferPool_GetState(d.id, &state) == R2_BUFFER_POOL_OK);
    assert(state == R2_BUFFER_STATE_PROCESSING);
    assert(R2_BufferPool_ReleaseProcessing(d.id) == R2_BUFFER_POOL_OK);
    assert(IdPush(&free_q, d.id));
}

static void CaseFinalMappingDistinct(void)
{
    uint32_t sequence;
    IdQueue free_q;
    DescQueue ready_q;
    R2_DmaSlotsSnapshot slots;

    Setup(&free_q, &ready_q);
    for (sequence = 1U; sequence <= R2_W4_EVENTS; ++sequence)
    {
        AdmitOne(sequence, &free_q, &ready_q);
        ProcessOne(sequence, &free_q, &ready_q);
    }
    assert(R2_DmaSlots_GetSnapshot(&slots) == R2_DMA_SLOTS_OK);
    assert(slots.m0_buffer != slots.m1_buffer);
    assert(slots.mapping_epoch == R2_W4_EVENTS + 1U);
}

static void RunCase(const char *name)
{
    if (strcmp(name, "roundtrip_64") == 0) CaseRoundTrip64();
    else if (strcmp(name, "fifo_rotation") == 0) CaseFifoRotation();
    else if (strcmp(name, "conservation") == 0) CaseConservation();
    else if (strcmp(name, "exhaustion_boundary") == 0) CaseExhaustionBoundary();
    else if (strcmp(name, "guard_safe") == 0) CaseGuardSafe();
    else if (strcmp(name, "guard_late") == 0) CaseGuardLate();
    else if (strcmp(name, "guard_ndtr") == 0) CaseGuardNdtr();
    else if (strcmp(name, "guard_ct") == 0) CaseGuardCt();
    else if (strcmp(name, "processing_state") == 0) CaseProcessingState();
    else if (strcmp(name, "final_mapping_distinct") == 0) CaseFinalMappingDistinct();
    else { fprintf(stderr, "unknown test case: %s\n", name); exit(2); }
}

int main(int argc, char **argv)
{
    if (argc == 1)
    {
        static const char *cases[] = {
            "roundtrip_64", "fifo_rotation", "conservation",
            "exhaustion_boundary", "guard_safe", "guard_late",
            "guard_ndtr", "guard_ct", "processing_state",
            "final_mapping_distinct"
        };
        size_t i;
        for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
        {
            RunCase(cases[i]);
        }
        puts("R2-W4 round-trip model native tests: PASS");
        return 0;
    }
    if (argc == 2)
    {
        RunCase(argv[1]);
        return 0;
    }
    return 2;
}
