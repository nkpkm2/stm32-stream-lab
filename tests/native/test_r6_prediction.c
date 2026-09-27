#include "r6_prediction.h"
#include "r6_start_binding.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { (void)fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #x, __FILE__, __LINE__); exit(EXIT_FAILURE); } } while (0)

static R6CostTable Cost(void)
{
    R6CostTable value;
    (void)memset(&value, 0, sizeof(value));
    value.key.pipeline_id = 1U; value.key.coefficient_version = 2U;
    value.key.window_version = 3U; value.key.feature_set = 4U;
    value.key.datatype = 5U; value.key.compile_flags = 6U;
    value.key.library_version = 7U; value.key.dac_profile = 8U;
    value.key.instrumentation_profile = 9U;
    value.commit_prefix_cycles = 5U; value.commit_suffix_cycles = 5U;
    value.processing_epilogue_cycles = 50U;
    return value;
}
static void CaseT10(void)
{
    R6CostTable cost = Cost();
    R6PredictionConfig config = { 100U, 100U, 1U, 0U, 1U, { 105U } };
    R6Prediction prediction;
    cost.irq_admission_offset_cycles = 10U;
    cost.dsp_work_cycles = 5U;
    CHECK(R6Prediction_Evaluate(&cost, &config, &prediction) == R6_PREDICTION_OK);
    CHECK(prediction.admitted_count == 1U && prediction.dropped_count == 0U);
    CHECK(prediction.blocks[0].nominal_trigger_cycles == 100U);
    CHECK(prediction.blocks[0].admission_cycles == 110U);
    CHECK(prediction.blocks[0].lock_cycles == 115U);
}
static void CaseUnlockBeforeEpilogue(void)
{
    R6CostTable cost = Cost();
    R6PredictionConfig config = { 100U, 20U, 2U, 1U, 0U, { 0U } };
    R6Prediction prediction;
    CHECK(R6Prediction_Evaluate(&cost, &config, &prediction) == R6_PREDICTION_OK);
    CHECK(prediction.admitted_count == 2U && prediction.dropped_count == 0U);
    CHECK(prediction.blocks[0].unlock_cycles == 110U);
    CHECK(prediction.blocks[0].processing_available_cycles == 160U);
    CHECK(prediction.blocks[1].admission_cycles == 120U);
    CHECK(prediction.blocks[1].processing_start_cycles == 160U);
}
static R6StartBinding Validation(void)
{
    R6StartBinding value;
    (void)memset(&value, 0, sizeof(value));
    value.kind = R6_RUN_PERFORMANCE; value.purpose = R6_PURPOSE_VALIDATION;
    value.prediction = R6_PREDICTION_BOUND; value.prediction_artifact_id = 1U;
    value.resolved_config_hash = 2U; value.model_hash = 3U; value.calibration_hash = 4U;
    value.instrumentation_profile_hash = 5U; value.platform_profile_hash = 6U;
    value.phase_window_hash = 7U; value.result_schema_hash = 8U;
    return value;
}
static void CaseBinding(void)
{
    R6StartBinding calibration;
    R6StartBinding validation = Validation();
    R6StartBindingSlot slot = { 0U, { 0 } };
    (void)memset(&calibration, 0, sizeof(calibration));
    calibration.kind = R6_RUN_PERFORMANCE;
    calibration.purpose = R6_PURPOSE_CALIBRATION;
    calibration.prediction = R6_PREDICTION_NOT_APPLICABLE;
    CHECK(R6StartBinding_Validate(&calibration) == R6_BINDING_OK);
    validation.model_hash = 0U;
    CHECK(R6StartBinding_Validate(&validation) == R6_BINDING_INCOMPLETE_ARTIFACT);
    validation = Validation();
    CHECK(R6StartBinding_Freeze(&slot, &validation) == R6_BINDING_OK);
    CHECK(R6StartBinding_Freeze(&slot, &validation) == R6_BINDING_ALREADY_FROZEN);
    validation.kind = R6_RUN_FAULT_STALL;
    CHECK(R6StartBinding_Validate(&validation) == R6_BINDING_INVALID_COMBINATION);
}
int main(int argc, char **argv)
{
    if (argc != 2) return EXIT_FAILURE;
    if (strcmp(argv[1], "t10") == 0) CaseT10();
    else if (strcmp(argv[1], "unlock") == 0) CaseUnlockBeforeEpilogue();
    else if (strcmp(argv[1], "binding") == 0) CaseBinding();
    else return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
