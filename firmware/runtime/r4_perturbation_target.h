#ifndef R4_PERTURBATION_TARGET_H
#define R4_PERTURBATION_TARGET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* This probe intentionally observes the production R3 publication-to-worker
 * path without calling RuntimeEvent.  It is compiled into the R4 runtime but
 * every endpoint is a no-op unless the dedicated A/B image is selected. */
#define R4_PERTURBATION_RESPONSE_SAMPLES 33U

typedef struct
{
    uint32_t release_raw;
    uint32_t worker_start_raw;
    uint32_t worker_complete_raw;
} R4_PerturbationResponseSample;

typedef struct
{
    uint32_t release_count;
    uint32_t completed_count;
    uint32_t overflow_count;
    R4_PerturbationResponseSample samples[R4_PERTURBATION_RESPONSE_SAMPLES];
} R4_PerturbationResponseSnapshot;

void R4_PerturbationTarget_Reset(void);
void R4_PerturbationTarget_OnReleaseFromIsr(void);
void R4_PerturbationTarget_OnWorkerStart(void);
void R4_PerturbationTarget_OnWorkerComplete(void);
void R4_PerturbationTarget_GetSnapshot(R4_PerturbationResponseSnapshot *out);

#ifdef __cplusplus
}
#endif

#endif
