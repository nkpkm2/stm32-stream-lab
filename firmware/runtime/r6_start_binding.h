#ifndef R6_START_BINDING_H
#define R6_START_BINDING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { R6_RUN_PERFORMANCE = 1, R6_RUN_FAULT_STALL = 2 } R6RunKind;
typedef enum {
    R6_PURPOSE_CALIBRATION = 1,
    R6_PURPOSE_DIAGNOSTIC = 2,
    R6_PURPOSE_VALIDATION = 3,
    R6_PURPOSE_FAULT_TEST = 4
} R6RunPurpose;
typedef enum { R6_PREDICTION_NOT_APPLICABLE = 0, R6_PREDICTION_BOUND = 1 } R6PredictionBinding;
typedef enum {
    R6_BINDING_OK = 0,
    R6_BINDING_INVALID_ARGUMENT,
    R6_BINDING_INVALID_COMBINATION,
    R6_BINDING_INCOMPLETE_ARTIFACT,
    R6_BINDING_ALREADY_FROZEN
} R6BindingStatus;

typedef struct
{
    R6RunKind kind;
    R6RunPurpose purpose;
    R6PredictionBinding prediction;
    uint64_t prediction_artifact_id;
    uint64_t resolved_config_hash;
    uint64_t model_hash;
    uint64_t calibration_hash;
    uint64_t instrumentation_profile_hash;
    uint64_t platform_profile_hash;
    uint64_t phase_window_hash;
    uint64_t result_schema_hash;
} R6StartBinding;

typedef struct { uint32_t frozen; R6StartBinding value; } R6StartBindingSlot;

/* The MCU validates that a host-prepared artifact is bound, but deliberately
 * cannot claim that the host has persisted a PC-side file. */
R6BindingStatus R6StartBinding_Validate(const R6StartBinding *binding);
R6BindingStatus R6StartBinding_Freeze(R6StartBindingSlot *slot,
    const R6StartBinding *binding);

#ifdef __cplusplus
}
#endif

#endif /* R6_START_BINDING_H */
