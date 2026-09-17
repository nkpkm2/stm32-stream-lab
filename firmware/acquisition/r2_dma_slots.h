#ifndef R2_DMA_SLOTS_H
#define R2_DMA_SLOTS_H

#include <stdint.h>

#include "r2_buffer_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

#define R2_DMA_SLOT_COUNT 2U

typedef enum
{
    R2_DMA_SLOT_M0 = 0,
    R2_DMA_SLOT_M1 = 1
} R2_DmaSlot;

typedef enum
{
    R2_DMA_SLOTS_OK = 0,
    R2_DMA_SLOTS_INVALID_ARGUMENT,
    R2_DMA_SLOTS_NOT_INITIALIZED,
    R2_DMA_SLOTS_ALREADY_INITIALIZED,
    R2_DMA_SLOTS_INVALID_CT,
    R2_DMA_SLOTS_INVALID_SLOT,
    R2_DMA_SLOTS_INVALID_BUFFER_ID,
    R2_DMA_SLOTS_DUPLICATE_BINDING,
    R2_DMA_SLOTS_STALE_PLAN,
    R2_DMA_SLOTS_CT_CHANGED,
    R2_DMA_SLOTS_INVARIANT_ERROR
} R2_DmaSlotsStatus;

typedef struct
{
    uint32_t initialized;
    uint32_t mapping_epoch;
    uint32_t violation_count;
    R2_BufferId m0_buffer;
    R2_BufferId m1_buffer;
} R2_DmaSlotsSnapshot;

typedef struct
{
    uint32_t ct_snapshot;
    R2_DmaSlot active_slot;
    R2_DmaSlot inactive_slot;
    R2_BufferId active_buffer;
    R2_BufferId completed_buffer;
    uint32_t mapping_epoch;
} R2_DmaSlotsObservation;

typedef struct
{
    uint32_t valid;
    uint32_t ct_snapshot;
    uint32_t mapping_epoch;
    R2_DmaSlot active_slot;
    R2_DmaSlot inactive_slot;
    R2_BufferId completed_buffer;
    R2_BufferId replacement_buffer;
} R2_DmaSlotsRebindPlan;

/*
 * R2-W2 scope:
 *   This module models only the logical M0/M1 slot mapping and CT semantics.
 *   It does not access DMA registers, change MxAR, inspect BufferPool states,
 *   move queue tokens, or perform ownership transitions.
 *
 * STM32 DBM CT interpretation used by this project:
 *   CT == 0: M0 is active/current, therefore M1 is inactive/completed.
 *   CT == 1: M1 is active/current, therefore M0 is inactive/completed.
 *
 * Concurrency contract:
 *   Calls are externally serialized. No locks or atomics are provided here.
 *
 * Hardware commit boundary:
 *   PrepareInactiveRebind is read-only. CommitPreparedRebind updates only the
 *   software mapping and MUST be called by future W3 integration only after
 *   the corresponding inactive hardware MxAR write has been proven safe and
 *   completed. W2 itself is not proof that an MxAR write is safe.
 */

void R2_DmaSlots_Reset(void);

R2_DmaSlotsStatus R2_DmaSlots_Initialize(
    R2_BufferId m0_buffer,
    R2_BufferId m1_buffer);

R2_DmaSlotsStatus R2_DmaSlots_ObserveCt(
    uint32_t ct_bit,
    R2_DmaSlotsObservation *out);

R2_DmaSlotsStatus R2_DmaSlots_GetBoundBuffer(
    R2_DmaSlot slot,
    R2_BufferId *out_buffer);

R2_DmaSlotsStatus R2_DmaSlots_PrepareInactiveRebind(
    uint32_t ct_bit,
    R2_BufferId replacement_buffer,
    R2_DmaSlotsRebindPlan *out_plan);

R2_DmaSlotsStatus R2_DmaSlots_CheckPlanCt(
    const R2_DmaSlotsRebindPlan *plan,
    uint32_t current_ct_bit);

R2_DmaSlotsStatus R2_DmaSlots_CommitPreparedRebind(
    const R2_DmaSlotsRebindPlan *plan);

R2_DmaSlotsStatus R2_DmaSlots_GetSnapshot(
    R2_DmaSlotsSnapshot *out);

R2_DmaSlotsStatus R2_DmaSlots_Validate(void);

#ifdef __cplusplus
}
#endif

#endif
