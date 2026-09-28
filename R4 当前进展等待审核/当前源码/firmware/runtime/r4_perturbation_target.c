#include "r4_perturbation_target.h"

#include <string.h>

#if defined(STREAM_LAB_R4_PERTURBATION_AB)
#include "stm32f4xx.h"

typedef struct
{
    volatile uint32_t release_count;
    volatile uint32_t completed_count;
    volatile uint32_t overflow_count;
    volatile uint32_t next_worker_sample;
    volatile uint32_t active_sample;
    R4_PerturbationResponseSample samples[R4_PERTURBATION_RESPONSE_SAMPLES];
} R4_PerturbationStorage;

static R4_PerturbationStorage storage;

void R4_PerturbationTarget_Reset(void)
{
    (void)memset(&storage, 0, sizeof(storage));
    storage.active_sample = R4_PERTURBATION_RESPONSE_SAMPLES;
    __DMB();
}

void R4_PerturbationTarget_OnReleaseFromIsr(void)
{
    uint32_t index = storage.release_count;

    if (index >= R4_PERTURBATION_RESPONSE_SAMPLES)
    {
        /* The frozen population is bounded; subsequent workload events are
         * deliberately not part of the sampled evidence population. */
        return;
    }
    storage.samples[index].release_raw = DWT->CYCCNT;
    __DMB();
    storage.release_count = index + 1U;
}

void R4_PerturbationTarget_OnWorkerStart(void)
{
    uint32_t index = storage.next_worker_sample;

    if (index >= storage.release_count)
    {
        storage.active_sample = R4_PERTURBATION_RESPONSE_SAMPLES;
        return;
    }
    storage.samples[index].worker_start_raw = DWT->CYCCNT;
    storage.active_sample = index;
    __DMB();
    storage.next_worker_sample = index + 1U;
}

void R4_PerturbationTarget_OnWorkerComplete(void)
{
    uint32_t index = storage.active_sample;

    if (index >= R4_PERTURBATION_RESPONSE_SAMPLES)
    {
        return;
    }
    storage.samples[index].worker_complete_raw = DWT->CYCCNT;
    __DMB();
    storage.completed_count = storage.completed_count + 1U;
    storage.active_sample = R4_PERTURBATION_RESPONSE_SAMPLES;
}

void R4_PerturbationTarget_GetSnapshot(R4_PerturbationResponseSnapshot *out)
{
    uint32_t index;

    if (out == NULL)
    {
        return;
    }
    __DMB();
    out->release_count = storage.release_count;
    out->completed_count = storage.completed_count;
    out->overflow_count = storage.overflow_count;
    for (index = 0U; index < R4_PERTURBATION_RESPONSE_SAMPLES; ++index)
    {
        out->samples[index] = storage.samples[index];
    }
}

#else

void R4_PerturbationTarget_Reset(void) {}
void R4_PerturbationTarget_OnReleaseFromIsr(void) {}
void R4_PerturbationTarget_OnWorkerStart(void) {}
void R4_PerturbationTarget_OnWorkerComplete(void) {}
void R4_PerturbationTarget_GetSnapshot(R4_PerturbationResponseSnapshot *out)
{
    if (out != NULL)
    {
        (void)memset(out, 0, sizeof(*out));
    }
}
#endif
