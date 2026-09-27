#include "r6_start_binding.h"

#include <string.h>

static uint32_t ArtifactComplete(const R6StartBinding *b)
{
    return (b->prediction_artifact_id != 0U) && (b->resolved_config_hash != 0U) &&
        (b->model_hash != 0U) && (b->calibration_hash != 0U) &&
        (b->instrumentation_profile_hash != 0U) && (b->platform_profile_hash != 0U) &&
        (b->phase_window_hash != 0U) && (b->result_schema_hash != 0U);
}

R6BindingStatus R6StartBinding_Validate(const R6StartBinding *binding)
{
    if (binding == NULL) return R6_BINDING_INVALID_ARGUMENT;
    if ((binding->kind == R6_RUN_PERFORMANCE) &&
        ((binding->purpose == R6_PURPOSE_CALIBRATION) ||
         (binding->purpose == R6_PURPOSE_DIAGNOSTIC)) &&
        (binding->prediction == R6_PREDICTION_NOT_APPLICABLE) &&
        (binding->prediction_artifact_id == 0U)) return R6_BINDING_OK;
    if ((binding->kind == R6_RUN_PERFORMANCE) &&
        (binding->purpose == R6_PURPOSE_VALIDATION) &&
        (binding->prediction == R6_PREDICTION_BOUND))
    {
        return ArtifactComplete(binding) != 0U ? R6_BINDING_OK :
            R6_BINDING_INCOMPLETE_ARTIFACT;
    }
    if ((binding->kind == R6_RUN_FAULT_STALL) &&
        (binding->purpose == R6_PURPOSE_FAULT_TEST) &&
        (binding->prediction == R6_PREDICTION_NOT_APPLICABLE) &&
        (binding->prediction_artifact_id == 0U)) return R6_BINDING_OK;
    return R6_BINDING_INVALID_COMBINATION;
}

R6BindingStatus R6StartBinding_Freeze(R6StartBindingSlot *slot,
    const R6StartBinding *binding)
{
    R6BindingStatus status;
    if ((slot == NULL) || (binding == NULL)) return R6_BINDING_INVALID_ARGUMENT;
    if (slot->frozen != 0U) return R6_BINDING_ALREADY_FROZEN;
    status = R6StartBinding_Validate(binding);
    if (status != R6_BINDING_OK) return status;
    (void)memcpy(&slot->value, binding, sizeof(*binding));
    slot->frozen = 1U;
    return R6_BINDING_OK;
}
