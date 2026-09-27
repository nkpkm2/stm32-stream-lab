#include "r6_hw_harness.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "r6_prediction.h"
#include "r6_start_binding.h"

#define R6_HW_STACK_WORDS 512U

volatile R6HwHarnessResult g_r6_hw_result;
static StaticTask_t harness_tcb;
static StackType_t harness_stack[R6_HW_STACK_WORDS];
/* Prediction objects contain bounded 32-block traces.  Keep them in the
 * explicit static SRAM budget rather than silently overflowing a task stack. */
static R6CostTable harness_cost;
static R6PredictionConfig t10_config;
static R6PredictionConfig unlock_config;
static R6Prediction t10_prediction;
static R6Prediction unlock_prediction;
static R6StartBinding harness_binding;
static R6StartBindingSlot harness_binding_slot;

static R6CostTable Cost(void)
{
    R6CostTable value;
    (void)memset(&value, 0, sizeof(value));
    value.key.pipeline_id = UINT64_C(0x10); value.key.coefficient_version = UINT64_C(0x11);
    value.key.window_version = UINT64_C(0x12); value.key.feature_set = UINT64_C(0x13);
    value.key.datatype = UINT64_C(0x14); value.key.compile_flags = UINT64_C(0x15);
    value.key.library_version = UINT64_C(0x16); value.key.dac_profile = UINT64_C(0x17);
    value.key.instrumentation_profile = UINT64_C(0x18);
    value.commit_prefix_cycles = 5U; value.commit_suffix_cycles = 5U;
    value.processing_epilogue_cycles = 50U;
    return value;
}

static R6StartBinding Binding(void)
{
    R6StartBinding value;
    (void)memset(&value, 0, sizeof(value));
    value.kind = R6_RUN_PERFORMANCE; value.purpose = R6_PURPOSE_VALIDATION;
    value.prediction = R6_PREDICTION_BOUND;
    value.prediction_artifact_id = UINT64_C(0x202609270001);
    value.resolved_config_hash = UINT64_C(0x1001); value.model_hash = UINT64_C(0x1002);
    value.calibration_hash = UINT64_C(0x1003); value.instrumentation_profile_hash = UINT64_C(0x1004);
    value.platform_profile_hash = UINT64_C(0x1005); value.phase_window_hash = UINT64_C(0x1006);
    value.result_schema_hash = UINT64_C(0x1007);
    return value;
}

static void HarnessTask(void *argument)
{
    (void)argument;
    harness_cost = Cost();
    (void)memset(&t10_config, 0, sizeof(t10_config));
    (void)memset(&unlock_config, 0, sizeof(unlock_config));
    t10_config.first_nominal_trigger_cycles = 100U;
    t10_config.block_period_cycles = 100U; t10_config.block_count = 1U;
    t10_config.pending_free_count = 1U; t10_config.pending_free_at_cycles[0] = 105U;
    unlock_config.first_nominal_trigger_cycles = 100U;
    unlock_config.block_period_cycles = 20U; unlock_config.block_count = 2U;
    unlock_config.initial_free_tokens = 1U;
    harness_binding = Binding();
    (void)memset(&harness_binding_slot, 0, sizeof(harness_binding_slot));
    harness_cost.irq_admission_offset_cycles = 10U;
    harness_cost.dsp_work_cycles = 5U;
    g_r6_hw_result.prediction_status = (uint32_t)R6Prediction_Evaluate(&harness_cost,
        &t10_config, &t10_prediction);
    g_r6_hw_result.t10_nominal = t10_prediction.blocks[0].nominal_trigger_cycles;
    g_r6_hw_result.t10_admission = t10_prediction.blocks[0].admission_cycles;
    g_r6_hw_result.t10_lock = t10_prediction.blocks[0].lock_cycles;
    g_r6_hw_result.t10_admitted = t10_prediction.admitted_count;
    harness_cost.irq_admission_offset_cycles = 0U;
    harness_cost.dsp_work_cycles = 0U;
    if (g_r6_hw_result.prediction_status == (uint32_t)R6_PREDICTION_OK)
    {
        g_r6_hw_result.prediction_status = (uint32_t)R6Prediction_Evaluate(&harness_cost,
            &unlock_config, &unlock_prediction);
    }
    g_r6_hw_result.first_unlock = unlock_prediction.blocks[0].unlock_cycles;
    g_r6_hw_result.first_processing_available = unlock_prediction.blocks[0].processing_available_cycles;
    g_r6_hw_result.second_admission = unlock_prediction.blocks[1].admission_cycles;
    g_r6_hw_result.second_processing_start = unlock_prediction.blocks[1].processing_start_cycles;
    g_r6_hw_result.unlock_epilogue_admitted = unlock_prediction.admitted_count;
    g_r6_hw_result.binding_status = (uint32_t)R6StartBinding_Validate(&harness_binding);
    g_r6_hw_result.freeze_status = (uint32_t)R6StartBinding_Freeze(&harness_binding_slot, &harness_binding);
    g_r6_hw_result.second_freeze_status = (uint32_t)R6StartBinding_Freeze(&harness_binding_slot, &harness_binding);
    g_r6_hw_result.artifact_id = harness_binding.prediction_artifact_id;
    g_r6_hw_result.resolved_config_hash = harness_binding.resolved_config_hash;
    g_r6_hw_result.model_hash = harness_binding.model_hash;
    g_r6_hw_result.calibration_hash = harness_binding.calibration_hash;
    g_r6_hw_result.instrumentation_profile_hash = harness_binding.instrumentation_profile_hash;
    g_r6_hw_result.platform_profile_hash = harness_binding.platform_profile_hash;
    g_r6_hw_result.phase_window_hash = harness_binding.phase_window_hash;
    g_r6_hw_result.result_schema_hash = harness_binding.result_schema_hash;
    g_r6_hw_result.pass = (g_r6_hw_result.prediction_status == (uint32_t)R6_PREDICTION_OK) &&
        (g_r6_hw_result.t10_admitted == 1U) && (g_r6_hw_result.t10_nominal == 100U) &&
        (g_r6_hw_result.t10_admission == 110U) && (g_r6_hw_result.t10_lock == 115U) &&
        (g_r6_hw_result.unlock_epilogue_admitted == 2U) &&
        (g_r6_hw_result.first_unlock == 110U) &&
        (g_r6_hw_result.first_processing_available == 160U) &&
        (g_r6_hw_result.second_admission == 120U) &&
        (g_r6_hw_result.second_processing_start == 160U) &&
        (g_r6_hw_result.binding_status == (uint32_t)R6_BINDING_OK) &&
        (g_r6_hw_result.freeze_status == (uint32_t)R6_BINDING_OK) &&
        (g_r6_hw_result.second_freeze_status == (uint32_t)R6_BINDING_ALREADY_FROZEN) ? 1U : 0U;
    __DMB();
    g_r6_hw_result.complete_magic = R6_HW_COMPLETE;
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000U));
}

void R6_HW_Start(void)
{
    TaskHandle_t task;
    (void)memset((void *)&g_r6_hw_result, 0, sizeof(g_r6_hw_result));
    g_r6_hw_result.magic = R6_HW_MAGIC;
    task = xTaskCreateStatic(HarnessTask, "R6HW", R6_HW_STACK_WORDS, NULL,
        tskIDLE_PRIORITY + 3U, harness_stack, &harness_tcb);
    if (task == NULL)
    {
        g_r6_hw_result.complete_magic = R6_HW_COMPLETE;
        return;
    }
    vTaskStartScheduler();
    g_r6_hw_result.complete_magic = R6_HW_COMPLETE;
}
