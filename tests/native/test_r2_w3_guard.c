#include "r2_w3_guard.h"
#include "r2_dma_slots.h"
#include "r2_buffer_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return EXIT_FAILURE; } } while (0)

static int Run(const char *name)
{
    uint32_t seq;
    uint32_t nominal;
    const uint32_t safe = R2_W3_BLOCK_CYCLES + 1000U;
    if (strcmp(name, "safe") == 0)
    {
        for (seq = 1U; seq <= 8U; ++seq)
        {
            CHECK(R2_W3_CheckWindow(seq, seq & 1U, seq & 1U,
                seq * R2_W3_BLOCK_CYCLES + 1000U, 250U, 1U) == R2_W3_GUARD_OK);
        }
    }
    else if (strcmp(name, "invalid_sequence") == 0)
    {
        CHECK(R2_W3_CheckWindow(0U, 1U, 1U, safe, 256U, 1U) == R2_W3_GUARD_EVENT);
        CHECK(R2_W3_CheckWindow(9U, 1U, 1U, safe, 256U, 1U) == R2_W3_GUARD_EVENT);
        CHECK(R2_W3_CheckWindow(UINT32_MAX, 1U, 1U, safe, 256U, 1U) == R2_W3_GUARD_EVENT);
    }
    else if (strcmp(name, "ct_change") == 0)
    {
        CHECK(R2_W3_CheckWindow(1U, 1U, 0U, safe, 256U, 1U) == R2_W3_GUARD_CT);
    }
    else if (strcmp(name, "wrong_phase") == 0)
    {
        CHECK(R2_W3_CheckWindow(1U, 0U, 0U, safe, 256U, 1U) == R2_W3_GUARD_CT);
        CHECK(R2_W3_CheckWindow(1U, 2U, 2U, safe, 256U, 1U) == R2_W3_GUARD_CT);
    }
    else if (strcmp(name, "early") == 0)
    {
        CHECK(R2_W3_CheckWindow(1U, 1U, 1U, R2_W3_BLOCK_CYCLES - 1U, 256U, 1U) == R2_W3_GUARD_TIME);
    }
    else if (strcmp(name, "budget_boundary") == 0)
    {
        uint32_t end = R2_W3_BLOCK_CYCLES + R2_W3_WRITE_LIMIT_CYCLES - R2_W3_FINAL_RESERVE_CYCLES;
        CHECK(R2_W3_CheckWindow(1U, 1U, 1U, end, 256U, 1U) == R2_W3_GUARD_OK);
        CHECK(R2_W3_CheckWindow(1U, 1U, 1U, end + 1U, 256U, 1U) == R2_W3_GUARD_TIME);
    }
    else if (strcmp(name, "ct_aba_late") == 0)
    {
        CHECK(R2_W3_CheckWindow(1U, 1U, 1U, 3U * R2_W3_BLOCK_CYCLES + 1000U,
            256U, 1U) == R2_W3_GUARD_TIME);
    }
    else if (strcmp(name, "ndtr") == 0)
    {
        CHECK(R2_W3_CheckWindow(1U, 1U, 1U, safe, 0U, 1U) == R2_W3_GUARD_NDTR);
        CHECK(R2_W3_CheckWindow(1U, 1U, 1U, safe, 257U, 1U) == R2_W3_GUARD_NDTR);
        CHECK(R2_W3_CheckWindow(1U, 1U, 1U, safe, 1U, 1U) == R2_W3_GUARD_NDTR);
        CHECK(R2_W3_CheckWindow(1U, 1U, 1U, safe, 191U, 1U) == R2_W3_GUARD_NDTR);
        CHECK(R2_W3_CheckWindow(1U, 1U, 1U, safe, 192U, 1U) == R2_W3_GUARD_OK);
    }
    else if (strcmp(name, "hardware_error") == 0)
    {
        CHECK(R2_W3_CheckWindow(1U, 1U, 1U, safe, 256U, 0U) == R2_W3_GUARD_HARDWARE);
    }
    else if (strcmp(name, "dwt_wrap") == 0)
    {
        uint32_t epoch = UINT32_MAX - 100U;
        uint32_t now = epoch + safe;
        CHECK(R2_W3_CheckWindow(1U, 1U, 1U, (uint32_t)(now - epoch), 256U, 1U) == R2_W3_GUARD_OK);
    }
    else if ((strcmp(name, "eight_step_model") == 0) || (strcmp(name, "rejected_no_model_change") == 0))
    {
        R2_BufferPoolSnapshot pool;
        R2_DmaSlotsSnapshot slots;
        R2_DmaSlotsRebindPlan plan;
        uint32_t registers[2] = {0U, 1U}; /* IDs, not real STM32 addresses */
        R2_BufferPool_Reset();
        R2_DmaSlots_Reset();
        CHECK(R2_BufferPool_Activate(8U) == R2_BUFFER_POOL_OK);
        CHECK(R2_DmaSlots_Initialize(0U, 1U) == R2_DMA_SLOTS_OK);
        for (seq = 1U; seq <= 8U; ++seq)
        {
            uint32_t ct = seq & 1U;
            uint32_t old_active = registers[ct];
            CHECK(R2_DmaSlots_PrepareInactiveRebind(ct, (R2_BufferId)(seq + 1U), &plan) == R2_DMA_SLOTS_OK);
            nominal = seq * R2_W3_BLOCK_CYCLES;
            if (strcmp(name, "rejected_no_model_change") == 0)
            {
                CHECK(R2_W3_CheckWindow(seq, ct, ct, nominal + 2U * R2_W3_BLOCK_CYCLES, 256U, 1U) == R2_W3_GUARD_TIME);
                CHECK(R2_DmaSlots_GetSnapshot(&slots) == R2_DMA_SLOTS_OK);
                CHECK(slots.mapping_epoch == seq);
                CHECK(registers[0] == slots.m0_buffer && registers[1] == slots.m1_buffer);
                CHECK(R2_BufferPool_GetSnapshot(&pool) == R2_BUFFER_POOL_OK);
                CHECK(pool.ready_count == seq - 1U && pool.free_count == 9U - seq);
            }
            CHECK(R2_W3_CheckWindow(seq, ct, ct, nominal + 1000U, 255U, 1U) == R2_W3_GUARD_OK);
            CHECK(R2_DmaSlots_CheckPlanCt(&plan, ct) == R2_DMA_SLOTS_OK);
            CHECK(plan.completed_buffer == (R2_BufferId)(seq - 1U));
            registers[ct ^ 1U] = seq + 1U;
            CHECK(registers[ct] == old_active);
            CHECK(R2_DmaSlots_CommitPreparedRebind(&plan) == R2_DMA_SLOTS_OK);
            CHECK(R2_BufferPool_CommitDmaRotation(plan.completed_buffer, plan.replacement_buffer) == R2_BUFFER_POOL_OK);
            CHECK(R2_DmaSlots_GetSnapshot(&slots) == R2_DMA_SLOTS_OK);
            CHECK(R2_BufferPool_GetSnapshot(&pool) == R2_BUFFER_POOL_OK);
            CHECK(slots.m0_buffer == registers[0] && slots.m1_buffer == registers[1]);
            CHECK(pool.free_count == 8U - seq && pool.ready_count == seq);
            CHECK(pool.dma_owned_count == 2U && pool.processing_count == 0U);
            CHECK(pool.states[slots.m0_buffer] == R2_BUFFER_STATE_DMA_OWNED);
            CHECK(pool.states[slots.m1_buffer] == R2_BUFFER_STATE_DMA_OWNED);
        }
        CHECK(registers[0] == 8U && registers[1] == 9U);
        CHECK(slots.mapping_epoch == 9U);
    }
    else { fprintf(stderr, "Unknown case: %s\n", name); return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    static const char *const cases[] = {
        "safe", "invalid_sequence", "ct_change", "wrong_phase", "early",
        "budget_boundary", "ct_aba_late", "ndtr", "hardware_error", "dwt_wrap",
        "eight_step_model", "rejected_no_model_change"
    };
    size_t i;
    if (argc == 2) { return Run(argv[1]); }
    if (argc != 1) { return EXIT_FAILURE; }
    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        if (Run(cases[i]) != EXIT_SUCCESS) { return EXIT_FAILURE; }
    }
    puts("R2-W3 guard/model native tests: PASS (no hardware simulated)");
    return EXIT_SUCCESS;
}
