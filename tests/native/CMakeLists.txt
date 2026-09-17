cmake_minimum_required(VERSION 3.20)
project(r2_buffer_pool_native LANGUAGES C)

if(CMAKE_CROSSCOMPILING OR CMAKE_C_COMPILER MATCHES "arm-none-eabi")
    message(FATAL_ERROR "R2-W1 needs a HOST native compiler, not the STM32 toolchain.")
endif()

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

if(MSVC)
    target_compile_options(test_r2_buffer_pool PRIVATE /W4 /WX)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_r2_buffer_pool PRIVATE -Wall -Wextra -Wpedantic -Werror)
else()
    message(FATAL_ERROR "Strict-warning policy is defined only for GCC, Clang, and MSVC.")
endif()

option(R2_NATIVE_SANITIZERS "Run host tests with address/undefined sanitizers" OFF)
if(R2_NATIVE_SANITIZERS)
    if(MSVC OR NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "This sanitizer profile requires non-MSVC GCC/Clang.")
    endif()
    target_compile_options(test_r2_buffer_pool PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(test_r2_buffer_pool PRIVATE -fsanitize=address,undefined)
endif()

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

# The test's CHECK macro must remain live even in Release with NDEBUG.
add_test(NAME r2_w1.assertion_probe COMMAND test_r2_buffer_pool --assertion-probe)
set_tests_properties(r2_w1.assertion_probe PROPERTIES WILL_FAIL TRUE TIMEOUT 5)
