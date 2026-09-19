cmake_minimum_required(VERSION 3.20)
project(r2_native LANGUAGES C)

if(CMAKE_CROSSCOMPILING OR CMAKE_C_COMPILER MATCHES "arm-none-eabi")
    message(FATAL_ERROR "R2 native tests need a HOST compiler, not the STM32 toolchain.")
endif()

function(r2_apply_strict_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /WX)
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    else()
        message(FATAL_ERROR "Strict-warning policy is defined only for GCC, Clang, and MSVC.")
    endif()
endfunction()

option(R2_NATIVE_SANITIZERS "Run host tests with address/undefined sanitizers" OFF)

function(r2_apply_sanitizers target_name)
    if(R2_NATIVE_SANITIZERS)
        if(MSVC OR NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            message(FATAL_ERROR "This sanitizer profile requires non-MSVC GCC/Clang.")
        endif()
        target_compile_options(${target_name} PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target_name} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

add_executable(test_r2_buffer_pool
    test_r2_buffer_pool.c
    ../../firmware/buffer_pool/r2_buffer_pool.c
)
target_include_directories(test_r2_buffer_pool PRIVATE ../../firmware/buffer_pool)
set_target_properties(test_r2_buffer_pool PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
r2_apply_strict_warnings(test_r2_buffer_pool)
r2_apply_sanitizers(test_r2_buffer_pool)

add_executable(test_r2_dma_slots
    test_r2_dma_slots.c
    ../../firmware/acquisition/r2_dma_slots.c
)
target_include_directories(test_r2_dma_slots PRIVATE
    ../../firmware/acquisition
    ../../firmware/buffer_pool
)
set_target_properties(test_r2_dma_slots PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
r2_apply_strict_warnings(test_r2_dma_slots)
r2_apply_sanitizers(test_r2_dma_slots)

enable_testing()

set(R2_W1_CASES
    reset activate_k1 activate_k2 activate_k4 activate_k8
    invalid_k find_free rotation round_trip double_release
    claim_nonready completed_not_dma replacement_not_free same_id invalid_id
    exhaustion reset_after_activity queries null_arguments unactivated
    reactivation transaction_matrix deterministic_model_walk snapshot_copy
)
foreach(case_name IN LISTS R2_W1_CASES)
    add_test(NAME r2_w1.${case_name} COMMAND test_r2_buffer_pool ${case_name})
    set_tests_properties(r2_w1.${case_name} PROPERTIES TIMEOUT 30)
endforeach()

add_test(NAME r2_w1.assertion_probe COMMAND test_r2_buffer_pool --assertion-probe)
set_tests_properties(r2_w1.assertion_probe PROPERTIES WILL_FAIL TRUE TIMEOUT 5)

set(R2_W2_CASES
    reset cold_initialize initialize invalid_initialize already_initialized
    observe_ct0 observe_ct1 invalid_ct get_bound invalid_slot
    prepare_no_mutation prepare_duplicate prepare_invalid
    check_plan_ct ct_changed commit_ct0 commit_ct1
    stale_plan forged_plan repeat_commit alternating_sequence
    null_arguments uninitialized reset_after_activity snapshot_copy
)
foreach(case_name IN LISTS R2_W2_CASES)
    add_test(NAME r2_w2.${case_name} COMMAND test_r2_dma_slots ${case_name})
    set_tests_properties(r2_w2.${case_name} PROPERTIES TIMEOUT 30)
endforeach()

# W3 pure guard + bounded software model only: no peripheral emulation.
add_executable(test_r2_w3_guard
    test_r2_w3_guard.c
    ../../firmware/acquisition/r2_dma_slots.c
    ../../firmware/buffer_pool/r2_buffer_pool.c
)
target_include_directories(test_r2_w3_guard PRIVATE
    ../../firmware/acquisition ../../firmware/buffer_pool)
set_target_properties(test_r2_w3_guard PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
r2_apply_strict_warnings(test_r2_w3_guard)
r2_apply_sanitizers(test_r2_w3_guard)
set(R2_W3_CASES
    safe invalid_sequence ct_change wrong_phase early budget_boundary
    ct_aba_late ndtr hardware_error dwt_wrap eight_step_model
    rejected_no_model_change)
foreach(case_name IN LISTS R2_W3_CASES)
    add_test(NAME r2_w3.${case_name} COMMAND test_r2_w3_guard ${case_name})
    set_tests_properties(r2_w3.${case_name} PROPERTIES TIMEOUT 10)
endforeach()

# W4 software ownership/queue model. The real FreeRTOS queue hook is verified
# by the target build and hardware experiment, not emulated by this executable.
add_executable(test_r2_w4_model
    test_r2_w4_model.c
    ../../firmware/acquisition/r2_dma_slots.c
    ../../firmware/buffer_pool/r2_buffer_pool.c
)
target_include_directories(test_r2_w4_model PRIVATE
    ../../firmware/acquisition ../../firmware/buffer_pool)
set_target_properties(test_r2_w4_model PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
r2_apply_strict_warnings(test_r2_w4_model)
r2_apply_sanitizers(test_r2_w4_model)
set(R2_W4_CASES
    roundtrip_64 fifo_rotation conservation exhaustion_boundary
    guard_safe guard_late guard_ndtr guard_ct processing_state
    final_mapping_distinct)
foreach(case_name IN LISTS R2_W4_CASES)
    add_test(NAME r2_w4.${case_name} COMMAND test_r2_w4_model ${case_name})
    set_tests_properties(r2_w4.${case_name} PROPERTIES TIMEOUT 10)
endforeach()

# W5 controlled capacity-drop model. This validates the ownership/mapping
# semantics of retaining the inactive DMA buffer when no FREE token exists.
add_executable(test_r2_w5_model
    test_r2_w5_model.c
    ../../firmware/acquisition/r2_dma_slots.c
    ../../firmware/buffer_pool/r2_buffer_pool.c
)
target_include_directories(test_r2_w5_model PRIVATE
    ../../firmware/acquisition ../../firmware/buffer_pool)
set_target_properties(test_r2_w5_model PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
r2_apply_strict_warnings(test_r2_w5_model)
r2_apply_sanitizers(test_r2_w5_model)
set(R2_W5_CASES
    drop_no_mutation drop_preserves_epoch consecutive_drops release_recovery
    conservation drop_no_violation guard_safe guard_late guard_ndtr guard_ct
    drop_dma_owned deterministic_64)
foreach(case_name IN LISTS R2_W5_CASES)
    add_test(NAME r2_w5.${case_name} COMMAND test_r2_w5_model ${case_name})
    set_tests_properties(r2_w5.${case_name} PROPERTIES TIMEOUT 10)
endforeach()

# W6 mandatory K matrix. Build the same ownership model for every supported K
# in both NORMAL and DROP mode so compile-time K/mode contracts are exercised.
function(r2_add_w6_matrix_target k_value mode_name drop_value)
    string(TOLOWER "${mode_name}" mode_lower)
    set(target_name "test_r2_w6_k${k_value}_${mode_lower}")

    add_executable(${target_name}
        test_r2_w6_matrix.c
        ../../firmware/acquisition/r2_dma_slots.c
        ../../firmware/buffer_pool/r2_buffer_pool.c
    )
    target_include_directories(${target_name} PRIVATE
        ../../firmware/acquisition ../../firmware/buffer_pool)
    target_compile_definitions(${target_name} PRIVATE
        R2_W6_K=${k_value}U
        R2_W6_DROP_MODE=${drop_value}U)
    set_target_properties(${target_name} PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    r2_apply_strict_warnings(${target_name})
    r2_apply_sanitizers(${target_name})

    foreach(case_name configuration guard matrix final_conservation)
        add_test(
            NAME r2_w6.k${k_value}.${mode_lower}.${case_name}
            COMMAND ${target_name} ${case_name})
        set_tests_properties(
            r2_w6.k${k_value}.${mode_lower}.${case_name}
            PROPERTIES TIMEOUT 20)
    endforeach()
endfunction()

foreach(k_value 1 2 4 8)
    r2_add_w6_matrix_target(${k_value} NORMAL 0)
    r2_add_w6_matrix_target(${k_value} DROP 1)
endforeach()

# R2 final control-traffic protocol tests are opt-in so the sealed W6 Native
# profile remains exactly 116 registered tests when this option is OFF.
option(R2_CT_NATIVE_TESTS "Build R2 control-traffic protocol tests" OFF)
if(R2_CT_NATIVE_TESTS)
    add_executable(test_r2_ct_protocol
        test_r2_ct_protocol.c
        ../../firmware/acquisition/r2_ct_protocol.c
    )
    target_include_directories(test_r2_ct_protocol PRIVATE
        ../../firmware/acquisition)
    set_target_properties(test_r2_ct_protocol PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    r2_apply_strict_warnings(test_r2_ct_protocol)
    r2_apply_sanitizers(test_r2_ct_protocol)

    foreach(case_name
        crc_standard encode_zero encode_max parse_fragmented
        resync bad_length bad_crc version)
        add_test(NAME r2_ct.${case_name} COMMAND test_r2_ct_protocol ${case_name})
        set_tests_properties(r2_ct.${case_name} PROPERTIES TIMEOUT 10)
    endforeach()
endif()
# R2->R3 foundation guard tests are opt-in so the sealed R2 native profile and
# its 124-test R2+CT identity remain unchanged when this option is OFF.
option(FOUNDATION_ADC_DBM_NATIVE_TESTS
    "Build F0-2 AdcDbmDriver pure completion-guard tests" OFF)
if(FOUNDATION_ADC_DBM_NATIVE_TESTS)
    add_executable(test_adc_dbm_rebind_guard
        test_adc_dbm_rebind_guard.c
    )
    target_include_directories(test_adc_dbm_rebind_guard PRIVATE
        ../../firmware/acquisition)
    set_target_properties(test_adc_dbm_rebind_guard PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    r2_apply_strict_warnings(test_adc_dbm_rebind_guard)
    r2_apply_sanitizers(test_adc_dbm_rebind_guard)

    foreach(case_name
        policy_256 policy_512 invalid_policy safe event ct hardware ndtr
        time_boundary dwt_wrap n512_boundary)
        add_test(
            NAME foundation.adc_dbm.${case_name}
            COMMAND test_adc_dbm_rebind_guard ${case_name})
        set_tests_properties(
            foundation.adc_dbm.${case_name}
            PROPERTIES TIMEOUT 10)
    endforeach()
endif()
# F0-3 Stage 1: pure logical ownership/mapping composition. This is opt-in so
# the sealed R2+CT and F0-2 native-test identities remain unchanged by default.
option(FOUNDATION_STREAM_OWNERSHIP_NATIVE_TESTS
    "Build F0-3 StreamOwnership pure logical transaction tests" OFF)
if(FOUNDATION_STREAM_OWNERSHIP_NATIVE_TESTS)
    add_executable(test_stream_ownership_core
        test_stream_ownership_core.c
        ../../firmware/runtime/stream_ownership_core.c
        ../../firmware/acquisition/r2_dma_slots.c
        ../../firmware/buffer_pool/r2_buffer_pool.c
    )
    target_include_directories(test_stream_ownership_core PRIVATE
        ../../firmware/runtime
        ../../firmware/acquisition
        ../../firmware/buffer_pool)
    set_target_properties(test_stream_ownership_core PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    r2_apply_strict_warnings(test_stream_ownership_core)
    r2_apply_sanitizers(test_stream_ownership_core)

    foreach(case_name
        reset init_k1 init_k2 init_k4 init_k8 invalid_k double_init_faults
        prepare_keep_no_mutation commit_keep_no_mutation
        prepare_rebind_no_mutation commit_rebind alternating_eight
        keep_after_exhaustion drop_streak_then_rebind
        sequence_gap_faults duplicate_keep_faults
        stale_plan_rejected replacement_not_free_faults forged_plan_faults
        keep_replacement_rejected snapshot)
        add_test(
            NAME foundation.stream_ownership.${case_name}
            COMMAND test_stream_ownership_core ${case_name})
        set_tests_properties(
            foundation.stream_ownership.${case_name}
            PROPERTIES TIMEOUT 10)
    endforeach()
endif()

# F0-3 Stage 2A: pure token-authority mirror plus READY->FREE CANCEL primitive.
option(FOUNDATION_STREAM_TOKEN_NATIVE_TESTS
    "Build F0-3 StreamTokenLedger pure transaction tests" OFF)
if(FOUNDATION_STREAM_TOKEN_NATIVE_TESTS)
    add_executable(test_stream_token_ledger
        test_stream_token_ledger.c
        ../../firmware/runtime/stream_token_ledger.c
        ../../firmware/buffer_pool/r2_buffer_pool.c
    )
    target_include_directories(test_stream_token_ledger PRIVATE
        ../../firmware/runtime
        ../../firmware/buffer_pool)
    set_target_properties(test_stream_token_ledger PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    r2_apply_strict_warnings(test_stream_token_ledger)
    r2_apply_sanitizers(test_stream_token_ledger)

    foreach(case_name
        reset init_k1 init_k2 init_k4 init_k8 invalid_k
        seal_incomplete duplicate_init admission_publish ready_claim
        cancel_flow complete_flow concurrent_windows
        publish_held_ready_rejected
        stable_reject_admission stable_reject_ready_hold
        missing_free missing_ready wrong_cancel
        complete_replacement_rejected cancel_ready_primitive
        stable_mismatch full_round_trip)
        add_test(
            NAME foundation.stream_token.${case_name}
            COMMAND test_stream_token_ledger ${case_name})
        set_tests_properties(
            foundation.stream_token.${case_name}
            PROPERTIES TIMEOUT 10)
    endforeach()
endif()
