#include "r2_dma_slots.h"

#include <stddef.h>

#define R2_DMA_SLOTS_INVALID_BUFFER R2_BUFFER_POOL_INVALID_ID

typedef struct
{
    uint32_t initialized;
    uint32_t mapping_epoch;
    uint32_t violation_count;
    R2_BufferId bound_buffer[R2_DMA_SLOT_COUNT];
} R2_DmaSlotsStorage;

static R2_DmaSlotsStorage slots =
{
    0U,
    0U,
    0U,
    {R2_DMA_SLOTS_INVALID_BUFFER, R2_DMA_SLOTS_INVALID_BUFFER}
};

static int IsValidBufferId(R2_BufferId id)
{
    return (uint32_t)id < R2_BUFFER_POOL_MAX_BUFFERS;
}

static int IsValidSlot(R2_DmaSlot slot)
{
    return (slot == R2_DMA_SLOT_M0) || (slot == R2_DMA_SLOT_M1);
}

static R2_DmaSlotsStatus Violation(R2_DmaSlotsStatus status)
{
    if (slots.violation_count != UINT32_MAX)
    {
        ++slots.violation_count;
    }
    return status;
}

static int InvariantsHold(void)
{
    if (slots.initialized == 0U)
    {
        return (slots.mapping_epoch == 0U) &&
            (slots.bound_buffer[R2_DMA_SLOT_M0] == R2_DMA_SLOTS_INVALID_BUFFER) &&
            (slots.bound_buffer[R2_DMA_SLOT_M1] == R2_DMA_SLOTS_INVALID_BUFFER);
    }

    if (slots.initialized != 1U)
    {
        return 0;
    }

    if ((slots.mapping_epoch == 0U) ||
        !IsValidBufferId(slots.bound_buffer[R2_DMA_SLOT_M0]) ||
        !IsValidBufferId(slots.bound_buffer[R2_DMA_SLOT_M1]))
    {
        return 0;
    }

    return slots.bound_buffer[R2_DMA_SLOT_M0] !=
        slots.bound_buffer[R2_DMA_SLOT_M1];
}

static R2_DmaSlotsStatus RequireInitialized(void)
{
    if (!InvariantsHold())
    {
        return Violation(R2_DMA_SLOTS_INVARIANT_ERROR);
    }
    if (slots.initialized == 0U)
    {
        return Violation(R2_DMA_SLOTS_NOT_INITIALIZED);
    }
    return R2_DMA_SLOTS_OK;
}

static R2_DmaSlotsStatus DecodeCt(
    uint32_t ct_bit,
    R2_DmaSlot *active_slot,
    R2_DmaSlot *inactive_slot)
{
    if (ct_bit > 1U)
    {
        return R2_DMA_SLOTS_INVALID_CT;
    }

    if (ct_bit == 0U)
    {
        *active_slot = R2_DMA_SLOT_M0;
        *inactive_slot = R2_DMA_SLOT_M1;
    }
    else
    {
        *active_slot = R2_DMA_SLOT_M1;
        *inactive_slot = R2_DMA_SLOT_M0;
    }

    return R2_DMA_SLOTS_OK;
}

void R2_DmaSlots_Reset(void)
{
    slots.initialized = 0U;
    slots.mapping_epoch = 0U;
    slots.violation_count = 0U;
    slots.bound_buffer[R2_DMA_SLOT_M0] = R2_DMA_SLOTS_INVALID_BUFFER;
    slots.bound_buffer[R2_DMA_SLOT_M1] = R2_DMA_SLOTS_INVALID_BUFFER;
}

R2_DmaSlotsStatus R2_DmaSlots_Initialize(
    R2_BufferId m0_buffer,
    R2_BufferId m1_buffer)
{
    if (!InvariantsHold())
    {
        return Violation(R2_DMA_SLOTS_INVARIANT_ERROR);
    }
    if (slots.initialized != 0U)
    {
        return Violation(R2_DMA_SLOTS_ALREADY_INITIALIZED);
    }
    if (!IsValidBufferId(m0_buffer) || !IsValidBufferId(m1_buffer))
    {
        return Violation(R2_DMA_SLOTS_INVALID_BUFFER_ID);
    }
    if (m0_buffer == m1_buffer)
    {
        return Violation(R2_DMA_SLOTS_DUPLICATE_BINDING);
    }

    slots.bound_buffer[R2_DMA_SLOT_M0] = m0_buffer;
    slots.bound_buffer[R2_DMA_SLOT_M1] = m1_buffer;
    slots.mapping_epoch = 1U;
    slots.initialized = 1U;
    return R2_DMA_SLOTS_OK;
}

R2_DmaSlotsStatus R2_DmaSlots_ObserveCt(
    uint32_t ct_bit,
    R2_DmaSlotsObservation *out)
{
    R2_DmaSlotsStatus status;
    R2_DmaSlot active_slot;
    R2_DmaSlot inactive_slot;
    R2_DmaSlotsObservation observation;

    if (out == NULL)
    {
        return Violation(R2_DMA_SLOTS_INVALID_ARGUMENT);
    }

    status = RequireInitialized();
    if (status != R2_DMA_SLOTS_OK)
    {
        return status;
    }

    status = DecodeCt(ct_bit, &active_slot, &inactive_slot);
    if (status != R2_DMA_SLOTS_OK)
    {
        return Violation(status);
    }

    observation.ct_snapshot = ct_bit;
    observation.active_slot = active_slot;
    observation.inactive_slot = inactive_slot;
    observation.active_buffer = slots.bound_buffer[active_slot];
    observation.completed_buffer = slots.bound_buffer[inactive_slot];
    observation.mapping_epoch = slots.mapping_epoch;

    *out = observation;
    return R2_DMA_SLOTS_OK;
}

R2_DmaSlotsStatus R2_DmaSlots_GetBoundBuffer(
    R2_DmaSlot slot,
    R2_BufferId *out_buffer)
{
    R2_DmaSlotsStatus status;

    if (out_buffer == NULL)
    {
        return Violation(R2_DMA_SLOTS_INVALID_ARGUMENT);
    }

    status = RequireInitialized();
    if (status != R2_DMA_SLOTS_OK)
    {
        return status;
    }

    if (!IsValidSlot(slot))
    {
        return Violation(R2_DMA_SLOTS_INVALID_SLOT);
    }

    *out_buffer = slots.bound_buffer[slot];
    return R2_DMA_SLOTS_OK;
}

R2_DmaSlotsStatus R2_DmaSlots_PrepareInactiveRebind(
    uint32_t ct_bit,
    R2_BufferId replacement_buffer,
    R2_DmaSlotsRebindPlan *out_plan)
{
    R2_DmaSlotsStatus status;
    R2_DmaSlotsObservation observation;
    R2_DmaSlotsRebindPlan plan;

    if (out_plan == NULL)
    {
        return Violation(R2_DMA_SLOTS_INVALID_ARGUMENT);
    }

    status = R2_DmaSlots_ObserveCt(ct_bit, &observation);
    if (status != R2_DMA_SLOTS_OK)
    {
        return status;
    }

    if (!IsValidBufferId(replacement_buffer))
    {
        return Violation(R2_DMA_SLOTS_INVALID_BUFFER_ID);
    }

    if ((replacement_buffer == slots.bound_buffer[R2_DMA_SLOT_M0]) ||
        (replacement_buffer == slots.bound_buffer[R2_DMA_SLOT_M1]))
    {
        return Violation(R2_DMA_SLOTS_DUPLICATE_BINDING);
    }

    plan.valid = 1U;
    plan.ct_snapshot = observation.ct_snapshot;
    plan.mapping_epoch = observation.mapping_epoch;
    plan.active_slot = observation.active_slot;
    plan.inactive_slot = observation.inactive_slot;
    plan.completed_buffer = observation.completed_buffer;
    plan.replacement_buffer = replacement_buffer;

    *out_plan = plan;
    return R2_DMA_SLOTS_OK;
}

R2_DmaSlotsStatus R2_DmaSlots_CheckPlanCt(
    const R2_DmaSlotsRebindPlan *plan,
    uint32_t current_ct_bit)
{
    R2_DmaSlotsStatus status;
    R2_DmaSlot active_slot;
    R2_DmaSlot inactive_slot;

    if (plan == NULL)
    {
        return Violation(R2_DMA_SLOTS_INVALID_ARGUMENT);
    }

    status = RequireInitialized();
    if (status != R2_DMA_SLOTS_OK)
    {
        return status;
    }

    status = DecodeCt(current_ct_bit, &active_slot, &inactive_slot);
    if (status != R2_DMA_SLOTS_OK)
    {
        return Violation(status);
    }

    if ((plan->valid != 1U) ||
        (plan->mapping_epoch != slots.mapping_epoch) ||
        (plan->ct_snapshot != current_ct_bit) ||
        (plan->active_slot != active_slot) ||
        (plan->inactive_slot != inactive_slot) ||
        !IsValidSlot(plan->active_slot) ||
        !IsValidSlot(plan->inactive_slot) ||
        !IsValidBufferId(plan->completed_buffer) ||
        !IsValidBufferId(plan->replacement_buffer) ||
        (plan->completed_buffer != slots.bound_buffer[plan->inactive_slot]) ||
        (plan->replacement_buffer == slots.bound_buffer[R2_DMA_SLOT_M0]) ||
        (plan->replacement_buffer == slots.bound_buffer[R2_DMA_SLOT_M1]))
    {
        if (plan->ct_snapshot != current_ct_bit)
        {
            return Violation(R2_DMA_SLOTS_CT_CHANGED);
        }
        return Violation(R2_DMA_SLOTS_STALE_PLAN);
    }

    return R2_DMA_SLOTS_OK;
}

R2_DmaSlotsStatus R2_DmaSlots_CommitPreparedRebind(
    const R2_DmaSlotsRebindPlan *plan)
{
    R2_DmaSlotsStatus status;
    R2_DmaSlot expected_active_slot;
    R2_DmaSlot expected_inactive_slot;

    if (plan == NULL)
    {
        return Violation(R2_DMA_SLOTS_INVALID_ARGUMENT);
    }

    status = RequireInitialized();
    if (status != R2_DMA_SLOTS_OK)
    {
        return status;
    }

    status = DecodeCt(
        plan->ct_snapshot,
        &expected_active_slot,
        &expected_inactive_slot);
    if (status != R2_DMA_SLOTS_OK)
    {
        return Violation(R2_DMA_SLOTS_STALE_PLAN);
    }

    if ((plan->valid != 1U) ||
        (plan->mapping_epoch != slots.mapping_epoch) ||
        (slots.mapping_epoch == UINT32_MAX) ||
        !IsValidSlot(plan->active_slot) ||
        !IsValidSlot(plan->inactive_slot) ||
        (plan->active_slot != expected_active_slot) ||
        (plan->inactive_slot != expected_inactive_slot) ||
        (plan->active_slot == plan->inactive_slot) ||
        !IsValidBufferId(plan->completed_buffer) ||
        !IsValidBufferId(plan->replacement_buffer) ||
        (plan->completed_buffer != slots.bound_buffer[plan->inactive_slot]) ||
        (plan->replacement_buffer == slots.bound_buffer[R2_DMA_SLOT_M0]) ||
        (plan->replacement_buffer == slots.bound_buffer[R2_DMA_SLOT_M1]))
    {
        return Violation(R2_DMA_SLOTS_STALE_PLAN);
    }

    slots.bound_buffer[plan->inactive_slot] = plan->replacement_buffer;
    ++slots.mapping_epoch;

    return R2_DMA_SLOTS_OK;
}

R2_DmaSlotsStatus R2_DmaSlots_GetSnapshot(
    R2_DmaSlotsSnapshot *out)
{
    R2_DmaSlotsSnapshot snapshot;

    if (out == NULL)
    {
        return Violation(R2_DMA_SLOTS_INVALID_ARGUMENT);
    }

    if (!InvariantsHold())
    {
        return Violation(R2_DMA_SLOTS_INVARIANT_ERROR);
    }

    snapshot.initialized = slots.initialized;
    snapshot.mapping_epoch = slots.mapping_epoch;
    snapshot.violation_count = slots.violation_count;
    snapshot.m0_buffer = slots.bound_buffer[R2_DMA_SLOT_M0];
    snapshot.m1_buffer = slots.bound_buffer[R2_DMA_SLOT_M1];

    *out = snapshot;
    return R2_DMA_SLOTS_OK;
}

R2_DmaSlotsStatus R2_DmaSlots_Validate(void)
{
    return InvariantsHold() ? R2_DMA_SLOTS_OK :
        Violation(R2_DMA_SLOTS_INVARIANT_ERROR);
}
