#include "stream_token_ledger.h"

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

static StreamTokenSnapshot Snap(void)
{
    StreamTokenSnapshot s;
    CHECK(StreamTokenLedger_GetSnapshot(&s) == STREAM_TOKEN_OK);
    return s;
}

static R2_BufferPoolSnapshot PoolSnap(void)
{
    R2_BufferPoolSnapshot s;
    CHECK(R2_BufferPool_GetSnapshot(&s) == R2_BUFFER_POOL_OK);
    return s;
}

static void Setup(uint32_t k)
{
    uint32_t id;

    StreamTokenLedger_ResetOffline();
    R2_BufferPool_Reset();
    CHECK(R2_BufferPool_Activate(k) == R2_BUFFER_POOL_OK);
    CHECK(StreamTokenLedger_Initialize(k) == STREAM_TOKEN_OK);
    for (id = 2U; id < k + 2U; ++id)
    {
        CHECK(StreamTokenLedger_RecordFreeSend(
            STREAM_TOKEN_FREE_INIT, (R2_BufferId)id) == STREAM_TOKEN_OK);
    }
    CHECK(StreamTokenLedger_SealInitialization() == STREAM_TOKEN_OK);
    {
        R2_BufferPoolSnapshot pool = PoolSnap();
        CHECK(StreamTokenLedger_ValidateStable(&pool) == STREAM_TOKEN_OK);
    }
}

static void CaseReset(void)
{
    StreamTokenSnapshot s;
    StreamTokenLedger_ResetOffline();
    s = Snap();
    CHECK(s.initialized == 0U);
    CHECK(s.sealed == 0U);
    CHECK(s.free_mask == 0U);
    CHECK(s.ready_mask == 0U);
    CHECK(s.admission_replacement == R2_BUFFER_POOL_INVALID_ID);
    CHECK(s.ready_hold_buffer == R2_BUFFER_POOL_INVALID_ID);
}

static void CheckInitK(uint32_t k)
{
    StreamTokenSnapshot s;
    Setup(k);
    s = Snap();
    CHECK(s.initialized == 1U);
    CHECK(s.sealed == 1U);
    CHECK(s.k == k);
    CHECK(s.active_count == k + 2U);
    CHECK(s.free_mask == (s.active_mask & ~0x3UL));
    CHECK(s.ready_mask == 0U);
    CHECK(s.init_send_count == k);
}
static void CaseInitK1(void) { CheckInitK(1U); }
static void CaseInitK2(void) { CheckInitK(2U); }
static void CaseInitK4(void) { CheckInitK(4U); }
static void CaseInitK8(void) { CheckInitK(8U); }

static void CaseInvalidK(void)
{
    StreamTokenSnapshot s;
    StreamTokenLedger_ResetOffline();
    CHECK(StreamTokenLedger_Initialize(3U) == STREAM_TOKEN_INVALID_K);
    s = Snap();
    CHECK(s.initialized == 0U);
    CHECK(s.faulted == 0U);
}

static void CaseSealIncomplete(void)
{
    StreamTokenSnapshot s;
    StreamTokenLedger_ResetOffline();
    CHECK(StreamTokenLedger_Initialize(2U) == STREAM_TOKEN_OK);
    CHECK(StreamTokenLedger_RecordFreeSend(
        STREAM_TOKEN_FREE_INIT, 2U) == STREAM_TOKEN_OK);
    CHECK(StreamTokenLedger_SealInitialization() ==
        STREAM_TOKEN_RECONCILE_ERROR);
    s = Snap();
    CHECK(s.faulted == 1U);
}

static void CaseDuplicateInit(void)
{
    StreamTokenSnapshot s;
    StreamTokenLedger_ResetOffline();
    CHECK(StreamTokenLedger_Initialize(2U) == STREAM_TOKEN_OK);
    CHECK(StreamTokenLedger_RecordFreeSend(
        STREAM_TOKEN_FREE_INIT, 2U) == STREAM_TOKEN_OK);
    CHECK(StreamTokenLedger_RecordFreeSend(
        STREAM_TOKEN_FREE_INIT, 2U) == STREAM_TOKEN_DUPLICATE);
    s = Snap();
    CHECK(s.faulted == 1U);
}

static void CaseAdmissionPublish(void)
{
    StreamTokenSnapshot s;
    Setup(2U);
    CHECK(StreamTokenLedger_TakeFreeForAdmission(2U) == STREAM_TOKEN_OK);
    s = Snap();
    CHECK(s.admission_active == 1U);
    CHECK((s.free_mask & (1UL << 2U)) == 0U);
    CHECK(StreamTokenLedger_PublishReady(0U) == STREAM_TOKEN_OK);
    s = Snap();
    CHECK(s.admission_active == 0U);
    CHECK((s.ready_mask & 1UL) != 0U);
}

static void CaseReadyClaim(void)
{
    StreamTokenSnapshot s;
    Setup(2U);
    CHECK(StreamTokenLedger_TakeFreeForAdmission(2U) == STREAM_TOKEN_OK);
    CHECK(StreamTokenLedger_PublishReady(0U) == STREAM_TOKEN_OK);
    CHECK(StreamTokenLedger_TakeReady(0U) == STREAM_TOKEN_OK);
    s = Snap();
    CHECK(s.ready_hold_active == 1U);
    CHECK(s.ready_mask == 0U);
    CHECK(StreamTokenLedger_CommitReadyClaim(0U) == STREAM_TOKEN_OK);
    s = Snap();
    CHECK(s.ready_hold_active == 0U);
    CHECK(s.ready_claim_count == 1U);
}

static void CaseCancelFlow(void)
{
    R2_BufferPoolSnapshot pool;

    Setup(2U);
    CHECK(StreamTokenLedger_TakeFreeForAdmission(2U) == STREAM_TOKEN_OK);
    CHECK(R2_BufferPool_CommitDmaRotation(0U, 2U) == R2_BUFFER_POOL_OK);
    CHECK(StreamTokenLedger_PublishReady(0U) == STREAM_TOKEN_OK);
    CHECK(StreamTokenLedger_TakeReady(0U) == STREAM_TOKEN_OK);
    CHECK(R2_BufferPool_CancelReady(0U) == R2_BUFFER_POOL_OK);
    CHECK(StreamTokenLedger_RecordFreeSend(
        STREAM_TOKEN_FREE_CANCEL, 0U) == STREAM_TOKEN_OK);
    pool = PoolSnap();
    CHECK(StreamTokenLedger_ValidateStable(&pool) == STREAM_TOKEN_OK);
}

static void CaseCompleteFlow(void)
{
    R2_BufferPoolSnapshot pool;

    Setup(2U);
    CHECK(StreamTokenLedger_TakeFreeForAdmission(2U) == STREAM_TOKEN_OK);
    CHECK(R2_BufferPool_CommitDmaRotation(0U, 2U) == R2_BUFFER_POOL_OK);
    CHECK(StreamTokenLedger_PublishReady(0U) == STREAM_TOKEN_OK);
    CHECK(StreamTokenLedger_TakeReady(0U) == STREAM_TOKEN_OK);
    CHECK(R2_BufferPool_ClaimReady(0U) == R2_BUFFER_POOL_OK);
    CHECK(StreamTokenLedger_CommitReadyClaim(0U) == STREAM_TOKEN_OK);
    CHECK(R2_BufferPool_ReleaseProcessing(0U) == R2_BUFFER_POOL_OK);
    CHECK(StreamTokenLedger_RecordFreeSend(
        STREAM_TOKEN_FREE_COMPLETE, 0U) == STREAM_TOKEN_OK);
    pool = PoolSnap();
    CHECK(StreamTokenLedger_ValidateStable(&pool) == STREAM_TOKEN_OK);
}

static void CaseConcurrentWindows(void)
{
    StreamTokenSnapshot s;

    Setup(4U);
    CHECK(StreamTokenLedger_TakeFreeForAdmission(2U) == STREAM_TOKEN_OK);
    CHECK(StreamTokenLedger_PublishReady(0U) == STREAM_TOKEN_OK);
    CHECK(StreamTokenLedger_TakeReady(0U) == STREAM_TOKEN_OK);

    /* A DMA admission can coexist with a Processing-side READY hold. */
    CHECK(StreamTokenLedger_TakeFreeForAdmission(3U) == STREAM_TOKEN_OK);
    s = Snap();
    CHECK(s.ready_hold_active == 1U);
    CHECK(s.admission_active == 1U);
    CHECK(s.ready_hold_buffer == 0U);
    CHECK(s.admission_replacement == 3U);
}

static void CasePublishHeldReadyRejected(void)
{
    StreamTokenSnapshot s;

    Setup(4U);

    CHECK(StreamTokenLedger_TakeFreeForAdmission(2U) == STREAM_TOKEN_OK);
    CHECK(StreamTokenLedger_PublishReady(0U) == STREAM_TOKEN_OK);
    CHECK(StreamTokenLedger_TakeReady(0U) == STREAM_TOKEN_OK);

    CHECK(StreamTokenLedger_TakeFreeForAdmission(3U) == STREAM_TOKEN_OK);
    CHECK(StreamTokenLedger_PublishReady(0U) == STREAM_TOKEN_DUPLICATE);

    s = Snap();
    CHECK(s.faulted == 1U);
    CHECK(s.ready_hold_active == 1U);
    CHECK(s.ready_hold_buffer == 0U);
    CHECK(s.admission_active == 1U);
    CHECK(s.admission_replacement == 3U);
}

static void CaseStableRejectedDuringAdmission(void)
{
    R2_BufferPoolSnapshot pool;
    Setup(2U);
    CHECK(StreamTokenLedger_TakeFreeForAdmission(2U) == STREAM_TOKEN_OK);
    pool = PoolSnap();
    CHECK(StreamTokenLedger_ValidateStable(&pool) ==
        STREAM_TOKEN_TRANSACTION_ACTIVE);
}

static void CaseStableRejectedDuringReadyHold(void)
{
    R2_BufferPoolSnapshot pool;
    Setup(2U);
    CHECK(StreamTokenLedger_TakeFreeForAdmission(2U) == STREAM_TOKEN_OK);
    CHECK(StreamTokenLedger_PublishReady(0U) == STREAM_TOKEN_OK);
    CHECK(StreamTokenLedger_TakeReady(0U) == STREAM_TOKEN_OK);
    pool = PoolSnap();
    CHECK(StreamTokenLedger_ValidateStable(&pool) ==
        STREAM_TOKEN_TRANSACTION_ACTIVE);
}

static void CaseMissingFree(void)
{
    Setup(1U);
    CHECK(StreamTokenLedger_TakeFreeForAdmission(2U) == STREAM_TOKEN_OK);
    CHECK(StreamTokenLedger_TakeFreeForAdmission(2U) ==
        STREAM_TOKEN_TRANSACTION_ACTIVE);
}

static void CaseMissingReady(void)
{
    Setup(2U);
    CHECK(StreamTokenLedger_TakeReady(0U) == STREAM_TOKEN_MISSING);
}

static void CaseWrongCancel(void)
{
    Setup(2U);
    CHECK(StreamTokenLedger_RecordFreeSend(
        STREAM_TOKEN_FREE_CANCEL, 2U) == STREAM_TOKEN_WRONG_SOURCE);
}

static void CaseCompleteReplacementRejected(void)
{
    Setup(2U);
    CHECK(StreamTokenLedger_TakeFreeForAdmission(2U) == STREAM_TOKEN_OK);
    CHECK(StreamTokenLedger_RecordFreeSend(
        STREAM_TOKEN_FREE_COMPLETE, 2U) == STREAM_TOKEN_WRONG_SOURCE);
}

static void CaseCancelReadyPrimitive(void)
{
    R2_BufferState state;

    R2_BufferPool_Reset();
    CHECK(R2_BufferPool_Activate(2U) == R2_BUFFER_POOL_OK);
    CHECK(R2_BufferPool_CommitDmaRotation(0U, 2U) == R2_BUFFER_POOL_OK);
    CHECK(R2_BufferPool_CancelReady(0U) == R2_BUFFER_POOL_OK);
    CHECK(R2_BufferPool_GetState(0U, &state) == R2_BUFFER_POOL_OK);
    CHECK(state == R2_BUFFER_STATE_FREE);
    CHECK(R2_BufferPool_CancelReady(0U) ==
        R2_BUFFER_POOL_ILLEGAL_TRANSITION);
}

static void CaseStableMismatch(void)
{
    R2_BufferPoolSnapshot pool;

    Setup(2U);
    CHECK(R2_BufferPool_CommitDmaRotation(0U, 2U) == R2_BUFFER_POOL_OK);
    pool = PoolSnap();
    CHECK(StreamTokenLedger_ValidateStable(&pool) ==
        STREAM_TOKEN_RECONCILE_ERROR);
}

static void CaseFullRoundTrip(void)
{
    R2_BufferPoolSnapshot pool;

    Setup(2U);

    CHECK(StreamTokenLedger_TakeFreeForAdmission(2U) == STREAM_TOKEN_OK);
    CHECK(R2_BufferPool_CommitDmaRotation(0U, 2U) == R2_BUFFER_POOL_OK);
    CHECK(StreamTokenLedger_PublishReady(0U) == STREAM_TOKEN_OK);

    CHECK(StreamTokenLedger_TakeReady(0U) == STREAM_TOKEN_OK);
    CHECK(R2_BufferPool_ClaimReady(0U) == R2_BUFFER_POOL_OK);
    CHECK(StreamTokenLedger_CommitReadyClaim(0U) == STREAM_TOKEN_OK);

    CHECK(R2_BufferPool_ReleaseProcessing(0U) == R2_BUFFER_POOL_OK);
    CHECK(StreamTokenLedger_RecordFreeSend(
        STREAM_TOKEN_FREE_COMPLETE, 0U) == STREAM_TOKEN_OK);

    pool = PoolSnap();
    CHECK(pool.free_count == 2U);
    CHECK(pool.ready_count == 0U);
    CHECK(pool.processing_count == 0U);
    CHECK(pool.dma_owned_count == 2U);
    CHECK(StreamTokenLedger_ValidateStable(&pool) == STREAM_TOKEN_OK);
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
    {"seal_incomplete", CaseSealIncomplete},
    {"duplicate_init", CaseDuplicateInit},
    {"admission_publish", CaseAdmissionPublish},
    {"ready_claim", CaseReadyClaim},
    {"cancel_flow", CaseCancelFlow},
    {"complete_flow", CaseCompleteFlow},
    {"concurrent_windows", CaseConcurrentWindows},
    {"publish_held_ready_rejected", CasePublishHeldReadyRejected},
    {"stable_reject_admission", CaseStableRejectedDuringAdmission},
    {"stable_reject_ready_hold", CaseStableRejectedDuringReadyHold},
    {"missing_free", CaseMissingFree},
    {"missing_ready", CaseMissingReady},
    {"wrong_cancel", CaseWrongCancel},
    {"complete_replacement_rejected", CaseCompleteReplacementRejected},
    {"cancel_ready_primitive", CaseCancelReadyPrimitive},
    {"stable_mismatch", CaseStableMismatch},
    {"full_round_trip", CaseFullRoundTrip}
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

    (void)puts("F0-3 StreamTokenLedger native tests: PASS");
    return EXIT_SUCCESS;
}
