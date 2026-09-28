cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME cubemx)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

# === R0 FREERTOS V11.1.0 BEGIN ===
# Project-owned integration. Do not replace with CubeMX bundled middleware.
set(FREERTOS_KERNEL_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS-Kernel"
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${FREERTOS_KERNEL_DIR}/tasks.c
    ${FREERTOS_KERNEL_DIR}/queue.c
    ${FREERTOS_KERNEL_DIR}/list.c
    ${FREERTOS_KERNEL_DIR}/portable/GCC/ARM_CM4F/port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/r0_freertos_smoke.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${FREERTOS_KERNEL_DIR}/include
    ${FREERTOS_KERNEL_DIR}/portable/GCC/ARM_CM4F
)
# === R0 FREERTOS V11.1.0 END ===

# === R2 DIAGNOSTIC PROFILES BEGIN ===
option(STREAM_LAB_R2_W3 "Build the bounded W3 rebinding experiment instead of R1 soak" OFF)
option(STREAM_LAB_R2_W4 "Build the W4 queue/processing ownership round-trip experiment" OFF)
option(STREAM_LAB_R2_W5 "Build the W5 controlled capacity-drop experiment" OFF)
option(STREAM_LAB_R2_W6 "Build one W6 mandatory K-matrix experiment" OFF)
option(STREAM_LAB_R2_CT "Add the R2 final control-traffic acceptance path to a W6 build" OFF)
option(STREAM_LAB_FOUNDATION_ADC_DBM_DRIVER
    "Compile the F0-2 AdcDbmDriver foundation module without migrating historical profiles" OFF)
option(STREAM_LAB_FOUNDATION_OWNERSHIP_CORE
    "Compile the F0-3 pure logical stream ownership core" OFF)
option(STREAM_LAB_FOUNDATION_TOKEN_LEDGER
    "Compile the F0-3 pure token-authority ledger" OFF)
option(STREAM_LAB_FOUNDATION_QUEUE_ADAPTER
    "Compile the F0-3 FreeRTOS queue transport adapter" OFF)
option(STREAM_LAB_R3_WORKER_CONTRACT
    "Compile the R3-W2 worker lifecycle contract foundation" OFF)

option(STREAM_LAB_R3_WORKER_TASKS
    "Compile the R3-W2 persistent Processing/Interference task glue" OFF)
option(STREAM_LAB_R3_LIFECYCLE
    "Compile the R3 transactional lifecycle coordinator foundation" OFF)

option(STREAM_LAB_R3_W2_HW
    "Build the R3-W2 target-only worker hardware harness" OFF)
option(STREAM_LAB_R3_W3_HW
    "Build the R3-W3 transactional START target hardware harness" OFF)
option(STREAM_LAB_R3_W4_HW
    "Build the R3-W4 safe STOP target hardware harness" OFF)
option(STREAM_LAB_R3_W5_HW
    "Build the R3-W5 command/isolation target hardware harness" OFF)
option(STREAM_LAB_R3_W6_HW
    "Build the R3-W6 500-cycle lifecycle target harness" OFF)
option(STREAM_LAB_R4_R3_LIFECYCLE_REGRESSION
    "Bind R3-W6 lifecycle hardware evidence to the R4 accounting runtime" OFF)
option(STREAM_LAB_R4_RUNTIME
    "Compile the R4 Clock64/RuntimeEvent/TickService integration" OFF)
option(STREAM_LAB_R4_ACCOUNTING
    "Enable R4 accounting trace hooks; OFF is reserved for the paired R4 perturbation reference profile" ON)
option(STREAM_LAB_R4_HW
    "Build the R4 target Clock64/runtime-window harness" OFF)
option(STREAM_LAB_R4_T04_DIAGNOSTIC
    "Build the non-formal T04 same-event diagnostic witness" OFF)
set(STREAM_LAB_R4_HW_SOAK_MS "0" CACHE STRING
    "R4 target-harness real DMA/IRQ soak duration in milliseconds (0 disables)")
set(STREAM_LAB_R4_HW_CASE "" CACHE STRING
    "R4 formal hardware case: T12_SOAK, T15_Q0, T15_RELEASE, T17_ATOMIC, COMMIT_BUDGET, SYNTHETIC_TASKS, DMA_WINDOW, TICK_GAP, MICROBENCH, MASK_RESTORE, TIME_REGRESSION, COMMIT_PENDING_IRQ, WINDOW_INTERSECTION, CUTOFF_LIVE, PERTURBATION_AB, R2_NORMAL_ANCHOR, or R2_DROP_ANCHOR")
set_property(CACHE STREAM_LAB_R4_HW_CASE PROPERTY STRINGS
    T12_SOAK T15_Q0 T15_RELEASE T17_ATOMIC COMMIT_BUDGET SYNTHETIC_TASKS DMA_WINDOW TICK_GAP MICROBENCH MASK_RESTORE TIME_REGRESSION COMMIT_PENDING_IRQ WINDOW_INTERSECTION CUTOFF_LIVE RESPONSE_SYNTHETIC MASK_TIMING COMBINED_SERVICE PERTURBATION_AB R2_NORMAL_ANCHOR R2_DROP_ANCHOR)
option(STREAM_LAB_R5_METRICS
    "Compile the R5 cohort/cutoff/sealed-results core" OFF)
option(STREAM_LAB_R5_HW
    "Build the R5 target synthetic cohort/sealing harness" OFF)
option(STREAM_LAB_R6_PREDICTION
    "Compile the R6 cost/admission prediction and start-binding core" OFF)
option(STREAM_LAB_R6_HW
    "Build the R6 target prediction/binding harness" OFF)
set(STREAM_LAB_R3_W3_HW_CASE "" CACHE STRING
    "R3-W3 target hardware case: START_A, T06_A, T06_B, or START_C")
set_property(CACHE STREAM_LAB_R3_W3_HW_CASE PROPERTY STRINGS
    START_A T06_A T06_B START_C)
set(STREAM_LAB_R3_W4_HW_CASE "" CACHE STRING
    "R3-W4 target hardware case: T04_A, T04_B, STOP_A, STOP_B, STOP_C, STOP_D, or STOP_E")
set_property(CACHE STREAM_LAB_R3_W4_HW_CASE PROPERTY STRINGS
    T04_A T04_B STOP_A STOP_B STOP_C STOP_D STOP_E)
set(STREAM_LAB_R3_W5_HW_CASE "" CACHE STRING
    "R3-W5 target hardware case: T13_A, T13_C, T13_D, T13_G, T20_A, or T20_B")
set_property(CACHE STREAM_LAB_R3_W5_HW_CASE PROPERTY STRINGS
    T13_A T13_C T13_D T13_G T20_A T20_B)
set(STREAM_LAB_R3_W6_HW_ANCHOR "" CACHE STRING "R3-W6 target anchor: A or B")
set_property(CACHE STREAM_LAB_R3_W6_HW_ANCHOR PROPERTY STRINGS A B)
set(STREAM_LAB_R3_W2_HW_CASE "" CACHE STRING
    "R3-W2 target hardware case: T03_A, T03_C, T03_D, T05_A, or T05_B")
set_property(CACHE STREAM_LAB_R3_W2_HW_CASE PROPERTY STRINGS
    T03_A T03_C T03_D T05_A T05_B)
set(STREAM_LAB_R2_CT_EVENTS "96" CACHE STRING "W6 event count for R2 control-traffic acceptance")
set_property(CACHE STREAM_LAB_R2_CT_EVENTS PROPERTY STRINGS 96 800)
set(STREAM_LAB_R2_CT_RUN_TIMEOUT_MS "1000" CACHE STRING "W6 run timeout for R2 control-traffic acceptance")
set_property(CACHE STREAM_LAB_R2_CT_RUN_TIMEOUT_MS PROPERTY STRINGS 1000 1500)
set(STREAM_LAB_R2_W6_K "1" CACHE STRING "W6 backlog capacity K: 1, 2, 4, or 8")
set_property(CACHE STREAM_LAB_R2_W6_K PROPERTY STRINGS 1 2 4 8)
set(STREAM_LAB_R2_W6_MODE "NORMAL" CACHE STRING "W6 matrix mode: NORMAL or DROP")
set_property(CACHE STREAM_LAB_R2_W6_MODE PROPERTY STRINGS NORMAL DROP)
set(R2_DIAGNOSTIC_PROFILE_COUNT 0)
foreach(profile_name
    STREAM_LAB_R2_W3 STREAM_LAB_R2_W4 STREAM_LAB_R2_W5 STREAM_LAB_R2_W6)
    if(${profile_name})
        math(EXPR R2_DIAGNOSTIC_PROFILE_COUNT "${R2_DIAGNOSTIC_PROFILE_COUNT}+1")
    endif()
endforeach()
if(R2_DIAGNOSTIC_PROFILE_COUNT GREATER 1)
    message(FATAL_ERROR "R2 diagnostic profiles W3/W4/W5/W6 are mutually exclusive")
endif()

if(STREAM_LAB_R3_W3_HW OR STREAM_LAB_R3_W4_HW OR STREAM_LAB_R3_W5_HW OR STREAM_LAB_R3_W6_HW)
    if(R2_DIAGNOSTIC_PROFILE_COUNT GREATER 0 OR STREAM_LAB_R3_W2_HW OR
       (STREAM_LAB_R3_W3_HW AND STREAM_LAB_R3_W4_HW) OR
       (STREAM_LAB_R3_W3_HW AND STREAM_LAB_R3_W5_HW) OR
       (STREAM_LAB_R3_W4_HW AND STREAM_LAB_R3_W5_HW) OR
       ((STREAM_LAB_R3_W3_HW OR STREAM_LAB_R3_W4_HW OR STREAM_LAB_R3_W5_HW) AND STREAM_LAB_R3_W6_HW))
        message(FATAL_ERROR "R3 W3/W4/W5/W6 hardware harnesses are exclusive with R2 and W2 harness profiles.")
    endif()
    if(STREAM_LAB_R3_W3_HW)
        if(NOT STREAM_LAB_R3_LIFECYCLE)
            message(FATAL_ERROR "R3-W3 hardware harness requires STREAM_LAB_R3_LIFECYCLE=ON.")
        endif()
        if(STREAM_LAB_R3_W3_HW_CASE STREQUAL "START_A")
            set(R3_W3_HW_CASE_ID_VALUE 1)
        elseif(STREAM_LAB_R3_W3_HW_CASE STREQUAL "T06_A")
            set(R3_W3_HW_CASE_ID_VALUE 2)
        elseif(STREAM_LAB_R3_W3_HW_CASE STREQUAL "T06_B")
            set(R3_W3_HW_CASE_ID_VALUE 3)
        elseif(STREAM_LAB_R3_W3_HW_CASE STREQUAL "START_C")
            set(R3_W3_HW_CASE_ID_VALUE 4)
        else()
            message(FATAL_ERROR "STREAM_LAB_R3_W3_HW_CASE must be START_A, T06_A, T06_B, or START_C.")
        endif()
    endif()
    if(STREAM_LAB_R3_W4_HW)
        if(NOT STREAM_LAB_R3_LIFECYCLE)
            message(FATAL_ERROR "R3-W4 hardware harness requires STREAM_LAB_R3_LIFECYCLE=ON.")
        endif()
        if(STREAM_LAB_R3_W4_HW_CASE STREQUAL "T04_A")
            set(R3_W4_HW_CASE_ID_VALUE 1)
        elseif(STREAM_LAB_R3_W4_HW_CASE STREQUAL "T04_B")
            set(R3_W4_HW_CASE_ID_VALUE 5)
        elseif(STREAM_LAB_R3_W4_HW_CASE STREQUAL "STOP_A")
            set(R3_W4_HW_CASE_ID_VALUE 2)
        elseif(STREAM_LAB_R3_W4_HW_CASE STREQUAL "STOP_B")
            set(R3_W4_HW_CASE_ID_VALUE 3)
        elseif(STREAM_LAB_R3_W4_HW_CASE STREQUAL "STOP_C")
            set(R3_W4_HW_CASE_ID_VALUE 6)
        elseif(STREAM_LAB_R3_W4_HW_CASE STREQUAL "STOP_D")
            set(R3_W4_HW_CASE_ID_VALUE 4)
        elseif(STREAM_LAB_R3_W4_HW_CASE STREQUAL "STOP_E")
            set(R3_W4_HW_CASE_ID_VALUE 7)
        else()
            message(FATAL_ERROR "STREAM_LAB_R3_W4_HW_CASE must be T04_A, T04_B, STOP_A, STOP_B, STOP_C, STOP_D, or STOP_E.")
        endif()
    endif()
    if(STREAM_LAB_R3_W5_HW)
        if(NOT STREAM_LAB_R3_LIFECYCLE)
            message(FATAL_ERROR "R3-W5 hardware harness requires STREAM_LAB_R3_LIFECYCLE=ON.")
        endif()
        if(STREAM_LAB_R3_W5_HW_CASE STREQUAL "T13_A")
            set(R3_W5_HW_CASE_ID_VALUE 1)
        elseif(STREAM_LAB_R3_W5_HW_CASE STREQUAL "T13_C")
            set(R3_W5_HW_CASE_ID_VALUE 2)
        elseif(STREAM_LAB_R3_W5_HW_CASE STREQUAL "T13_D")
            set(R3_W5_HW_CASE_ID_VALUE 3)
        elseif(STREAM_LAB_R3_W5_HW_CASE STREQUAL "T13_G")
            set(R3_W5_HW_CASE_ID_VALUE 4)
        elseif(STREAM_LAB_R3_W5_HW_CASE STREQUAL "T20_A")
            set(R3_W5_HW_CASE_ID_VALUE 5)
        elseif(STREAM_LAB_R3_W5_HW_CASE STREQUAL "T20_B")
            set(R3_W5_HW_CASE_ID_VALUE 6)
        else()
            message(FATAL_ERROR "STREAM_LAB_R3_W5_HW_CASE must be T13_A, T13_C, T13_D, T13_G, T20_A, or T20_B.")
        endif()
    endif()
    if(STREAM_LAB_R3_W6_HW)
        if(NOT STREAM_LAB_R3_LIFECYCLE)
            message(FATAL_ERROR "R3-W6 hardware harness requires STREAM_LAB_R3_LIFECYCLE=ON.")
        endif()
        if(NOT STREAM_LAB_R4_R3_LIFECYCLE_REGRESSION OR
           NOT STREAM_LAB_R4_RUNTIME OR NOT STREAM_LAB_R4_ACCOUNTING)
            message(FATAL_ERROR
                "R3-W6 formal lifecycle evidence requires its explicit R4 accounting profile.")
        endif()
        if(STREAM_LAB_R3_W6_HW_ANCHOR STREQUAL "A")
            set(R3_W6_HW_ANCHOR_VALUE 1)
        elseif(STREAM_LAB_R3_W6_HW_ANCHOR STREQUAL "B")
            set(R3_W6_HW_ANCHOR_VALUE 2)
        else()
            message(FATAL_ERROR "STREAM_LAB_R3_W6_HW_ANCHOR must be A or B.")
        endif()
    endif()
elseif(NOT STREAM_LAB_R3_W3_HW_CASE STREQUAL "")
    message(FATAL_ERROR "STREAM_LAB_R3_W3_HW_CASE requires STREAM_LAB_R3_W3_HW=ON.")
elseif(NOT STREAM_LAB_R3_W4_HW_CASE STREQUAL "")
    message(FATAL_ERROR "STREAM_LAB_R3_W4_HW_CASE requires STREAM_LAB_R3_W4_HW=ON.")
elseif(STREAM_LAB_R3_W2_HW)
    if(R2_DIAGNOSTIC_PROFILE_COUNT GREATER 0)
        message(FATAL_ERROR
            "STREAM_LAB_R3_W2_HW must not be combined with historical R2 profiles.")
    endif()
    if(NOT STREAM_LAB_R3_WORKER_CONTRACT OR
       NOT STREAM_LAB_R3_WORKER_TASKS)
        message(FATAL_ERROR
            "STREAM_LAB_R3_W2_HW requires R3 worker contract and task glue.")
    endif()
    if(STREAM_LAB_FOUNDATION_QUEUE_ADAPTER)
        message(FATAL_ERROR
            "STREAM_LAB_R3_W2_HW uses its test authority shim and must not link the production queue-adapter/RunAuthority profile.")
    endif()
    if(STREAM_LAB_R3_W2_HW_CASE STREQUAL "T03_A")
        set(R3_W2_HW_CASE_ID_VALUE 1)
    elseif(STREAM_LAB_R3_W2_HW_CASE STREQUAL "T03_C")
        set(R3_W2_HW_CASE_ID_VALUE 2)
    elseif(STREAM_LAB_R3_W2_HW_CASE STREQUAL "T03_D")
        set(R3_W2_HW_CASE_ID_VALUE 3)
    elseif(STREAM_LAB_R3_W2_HW_CASE STREQUAL "T05_A")
        set(R3_W2_HW_CASE_ID_VALUE 4)
    elseif(STREAM_LAB_R3_W2_HW_CASE STREQUAL "T05_B")
        set(R3_W2_HW_CASE_ID_VALUE 5)
    else()
        message(FATAL_ERROR "STREAM_LAB_R3_W2_HW_CASE must select one frozen W2 case.")
    endif()
endif()

if(STREAM_LAB_R2_CT AND NOT STREAM_LAB_R2_W6)
    message(FATAL_ERROR "STREAM_LAB_R2_CT is a modifier of STREAM_LAB_R2_W6 and requires W6")
endif()
if(NOT STREAM_LAB_R2_CT_EVENTS MATCHES "^(96|800)$")
    message(FATAL_ERROR "STREAM_LAB_R2_CT_EVENTS must be 96 or 800")
endif()
if(NOT STREAM_LAB_R2_CT AND NOT STREAM_LAB_R2_CT_EVENTS STREQUAL "96")
    message(FATAL_ERROR "Non-default STREAM_LAB_R2_CT_EVENTS requires STREAM_LAB_R2_CT=ON")
endif()
if(NOT STREAM_LAB_R2_CT_RUN_TIMEOUT_MS MATCHES "^(1000|1500)$")
    message(FATAL_ERROR "STREAM_LAB_R2_CT_RUN_TIMEOUT_MS must be 1000 or 1500")
endif()
if(NOT STREAM_LAB_R2_CT AND NOT STREAM_LAB_R2_CT_RUN_TIMEOUT_MS STREQUAL "1000")
    message(FATAL_ERROR "Non-default STREAM_LAB_R2_CT_RUN_TIMEOUT_MS requires STREAM_LAB_R2_CT=ON")
endif()

if(STREAM_LAB_R2_W6)
    if(NOT STREAM_LAB_R2_W6_K MATCHES "^(1|2|4|8)$")
        message(FATAL_ERROR "STREAM_LAB_R2_W6_K must be 1, 2, 4, or 8")
    endif()
    string(TOUPPER "${STREAM_LAB_R2_W6_MODE}" R2_W6_MODE_UPPER)
    if(NOT R2_W6_MODE_UPPER STREQUAL "NORMAL" AND
       NOT R2_W6_MODE_UPPER STREQUAL "DROP")
        message(FATAL_ERROR "STREAM_LAB_R2_W6_MODE must be NORMAL or DROP")
    endif()
endif()
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition"
)

# === R2->R3 RUNTIME FOUNDATION F0-2 BEGIN ===
# Compile-only gate.  No historical R1/R2 runtime path calls this module yet.
if(STREAM_LAB_FOUNDATION_ADC_DBM_DRIVER)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/adc_dbm_driver.c"
    )
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/adc_dbm_driver.c"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror"
    )
endif()
# === R2->R3 RUNTIME FOUNDATION F0-2 END ===

# === R2->R3 RUNTIME FOUNDATION F0-3 STAGE1 BEGIN ===
if(STREAM_LAB_FOUNDATION_OWNERSHIP_CORE)
    if(STREAM_LAB_R2_W3 OR STREAM_LAB_R2_W4 OR
       STREAM_LAB_R2_W5 OR STREAM_LAB_R2_W6)
        message(FATAL_ERROR
            "F0-3 ownership-core compile gate must not be combined with historical R2 profiles.")
    endif()
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/stream_ownership_core.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../buffer_pool/r2_buffer_pool.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/r2_dma_slots.c"
    )
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime"
        "${CMAKE_CURRENT_SOURCE_DIR}/../buffer_pool"
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition"
    )
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/stream_ownership_core.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../buffer_pool/r2_buffer_pool.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/r2_dma_slots.c"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror"
    )
endif()
# === R2->R3 RUNTIME FOUNDATION F0-3 STAGE1 END ===

# === R2->R3 RUNTIME FOUNDATION F0-3 STAGE2A BEGIN ===
if(STREAM_LAB_FOUNDATION_TOKEN_LEDGER)
    if(NOT STREAM_LAB_FOUNDATION_OWNERSHIP_CORE)
        message(FATAL_ERROR
            "F0-3 token-ledger foundation requires STREAM_LAB_FOUNDATION_OWNERSHIP_CORE=ON.")
    endif()
    if(STREAM_LAB_R2_W3 OR STREAM_LAB_R2_W4 OR
       STREAM_LAB_R2_W5 OR STREAM_LAB_R2_W6)
        message(FATAL_ERROR
            "F0-3 token-ledger compile gate must not be combined with historical R2 profiles.")
    endif()
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/stream_token_ledger.c"
    )
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime"
        "${CMAKE_CURRENT_SOURCE_DIR}/../buffer_pool"
    )
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/stream_token_ledger.c"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror"
    )
endif()
# === R2->R3 RUNTIME FOUNDATION F0-3 STAGE2A END ===

# === R2->R3 RUNTIME FOUNDATION F0-3 STAGE2B1 BEGIN ===
if(STREAM_LAB_FOUNDATION_QUEUE_ADAPTER)
    if(NOT STREAM_LAB_FOUNDATION_OWNERSHIP_CORE OR
       NOT STREAM_LAB_FOUNDATION_TOKEN_LEDGER)
        message(FATAL_ERROR
            "F0-3 queue adapter requires ownership core and token ledger.")
    endif()
    if(STREAM_LAB_R2_W3 OR STREAM_LAB_R2_W4 OR
       STREAM_LAB_R2_W5 OR STREAM_LAB_R2_W6)
        message(FATAL_ERROR
            "F0-3 queue adapter must not be combined with historical R2 profiles.")
    endif()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        STREAM_LAB_FOUNDATION_QUEUE_ADAPTER=1
    )
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/stream_queue_adapter.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/stream_run_authority.c"
    )
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime"
        "${CMAKE_CURRENT_SOURCE_DIR}/../buffer_pool"
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition"
    )
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/stream_queue_adapter.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/stream_run_authority.c"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror"
    )
endif()
# === R2->R3 RUNTIME FOUNDATION F0-3 STAGE2B1 END ===

# === R3-W2 WORKER CONTRACT FOUNDATION BEGIN ===
if(STREAM_LAB_R3_WORKER_CONTRACT)
    if(STREAM_LAB_R2_W3 OR STREAM_LAB_R2_W4 OR
       STREAM_LAB_R2_W5 OR STREAM_LAB_R2_W6)
        message(FATAL_ERROR
            "R3 worker contract must not be combined with historical R2 profiles.")
    endif()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        STREAM_LAB_R3_WORKER_CONTRACT=1
    )
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_worker_contract.c"
    )
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime"
    )
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_worker_contract.c"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-mthumb"
    )
endif()
# === R3-W2 WORKER CONTRACT FOUNDATION END ===

# === R3-W2B WORKER TASK GLUE BEGIN ===
if(STREAM_LAB_R3_WORKER_TASKS)
    if(NOT STREAM_LAB_R3_WORKER_CONTRACT)
        message(FATAL_ERROR
            "R3 worker tasks require the worker contract.")
    endif()
    if(NOT STREAM_LAB_R3_W2_HW AND
       NOT STREAM_LAB_FOUNDATION_QUEUE_ADAPTER)
        message(FATAL_ERROR
            "R3 worker tasks require the production queue adapter unless the frozen W2-HW authority shim profile is active.")
    endif()
    if(STREAM_LAB_R3_W2_HW AND
       STREAM_LAB_FOUNDATION_QUEUE_ADAPTER)
        message(FATAL_ERROR
            "R3 W2-HW must substitute the test authority shim for production RunAuthority.")
    endif()
    if(STREAM_LAB_R2_W3 OR STREAM_LAB_R2_W4 OR
       STREAM_LAB_R2_W5 OR STREAM_LAB_R2_W6)
        message(FATAL_ERROR
            "R3 worker tasks must not be combined with historical R2 profiles.")
    endif()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        STREAM_LAB_R3_WORKER_TASKS=1
    )
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_worker_tasks.c"
    )
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime"
    )
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_worker_tasks.c"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-mthumb"
    )
endif()
# === R3-W2B WORKER TASK GLUE END ===

# === R3-W3 TRANSACTIONAL LIFECYCLE FOUNDATION BEGIN ===
if(STREAM_LAB_R3_LIFECYCLE)
    if(NOT STREAM_LAB_FOUNDATION_QUEUE_ADAPTER OR
       NOT STREAM_LAB_FOUNDATION_ADC_DBM_DRIVER OR
       NOT STREAM_LAB_R3_WORKER_TASKS)
        message(FATAL_ERROR
            "R3 lifecycle requires QueueAdapter/RunAuthority, ADC DBM driver, and persistent workers.")
    endif()
    if(STREAM_LAB_R3_W2_HW)
        message(FATAL_ERROR "R3 lifecycle is not part of the frozen W2 hardware profile.")
    endif()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE STREAM_LAB_R3_LIFECYCLE=1)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_lifecycle.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_command_ledger.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_result_store.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_w3_runtime.c")
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime")
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_lifecycle.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_command_ledger.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_result_store.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_w3_runtime.c"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-mthumb")
endif()
# === R3-W3 TRANSACTIONAL LIFECYCLE FOUNDATION END ===

# === R4 RUNTIME ACCOUNTING FOUNDATION BEGIN ===
if(STREAM_LAB_R4_RUNTIME)
    if(NOT STREAM_LAB_R3_LIFECYCLE)
        message(FATAL_ERROR
            "R4 runtime requires the R3 lifecycle/queue integration profile.")
    endif()
    if(STREAM_LAB_R4_ACCOUNTING)
        set(R4_ACCOUNTING_VALUE 1)
    else()
        set(R4_ACCOUNTING_VALUE 0)
    endif()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        STREAM_LAB_R4_RUNTIME=1
        STREAM_LAB_R4_ACCOUNTING=${R4_ACCOUNTING_VALUE})
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r4_clock64.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r4_runtime_event.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r4_runtime_target.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r4_perturbation_target.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r4_tick_service.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r4_tick_service_target.c")
    if(STREAM_LAB_R4_T04_DIAGNOSTIC)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
            STREAM_LAB_R4_T04_DIAGNOSTIC=1)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r4_completion_witness.c")
        set_source_files_properties(
            "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r4_completion_witness.c"
            PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-mthumb")
    endif()
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime")
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r4_clock64.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r4_runtime_event.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r4_runtime_target.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r4_perturbation_target.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r4_tick_service.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r4_tick_service_target.c"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-mthumb")
endif()
# === R4 RUNTIME ACCOUNTING FOUNDATION END ===

# === R5 COHORT / SEALED RESULTS FOUNDATION BEGIN ===
if(STREAM_LAB_R5_METRICS)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE STREAM_LAB_R5_METRICS=1)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r5_run_metrics.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r5_result_store.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r5_observer.c")
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime")
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r5_run_metrics.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r5_result_store.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r5_observer.c"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-mthumb")
endif()

if(STREAM_LAB_R5_HW)
    if(NOT STREAM_LAB_R5_METRICS)
        message(FATAL_ERROR "R5 hardware harness requires STREAM_LAB_R5_METRICS=ON.")
    endif()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE STREAM_LAB_R5_HW=1)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r5_hw_harness.c")
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime")
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r5_hw_harness.c"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-mthumb")
endif()
# === R5 COHORT / SEALED RESULTS FOUNDATION END ===

# === R6 COST / PREDICTION FOUNDATION BEGIN ===
if(STREAM_LAB_R6_PREDICTION)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE STREAM_LAB_R6_PREDICTION=1)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r6_prediction.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r6_start_binding.c")
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime")
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r6_prediction.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r6_start_binding.c"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-mthumb")
endif()
if(STREAM_LAB_R6_HW)
    if(NOT STREAM_LAB_R6_PREDICTION)
        message(FATAL_ERROR "R6 hardware harness requires STREAM_LAB_R6_PREDICTION=ON.")
    endif()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE STREAM_LAB_R6_HW=1)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r6_hw_harness.c")
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r6_hw_harness.c"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-mthumb")
endif()
# === R6 COST / PREDICTION FOUNDATION END ===

if(STREAM_LAB_R4_HW)
    if(NOT STREAM_LAB_R4_RUNTIME)
        message(FATAL_ERROR "R4 hardware harness requires STREAM_LAB_R4_RUNTIME=ON.")
    endif()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE STREAM_LAB_R4_HW=1)
    if(STREAM_LAB_R4_T04_DIAGNOSTIC AND
       NOT STREAM_LAB_R4_HW_CASE STREQUAL "COMMIT_BUDGET")
        message(FATAL_ERROR
            "STREAM_LAB_R4_T04_DIAGNOSTIC requires COMMIT_BUDGET and is not formal acceptance.")
    endif()
    if(NOT STREAM_LAB_R4_HW_SOAK_MS MATCHES "^[0-9]+$")
        message(FATAL_ERROR "STREAM_LAB_R4_HW_SOAK_MS must be an unsigned integer.")
    endif()
    if(STREAM_LAB_R4_HW_CASE STREQUAL "T12_SOAK")
        set(R4_HW_CASE_ID_VALUE 1)
    elseif(STREAM_LAB_R4_HW_CASE STREQUAL "T15_Q0")
        set(R4_HW_CASE_ID_VALUE 2)
    elseif(STREAM_LAB_R4_HW_CASE STREQUAL "T15_RELEASE")
        set(R4_HW_CASE_ID_VALUE 3)
    elseif(STREAM_LAB_R4_HW_CASE STREQUAL "T17_ATOMIC")
        set(R4_HW_CASE_ID_VALUE 4)
    elseif(STREAM_LAB_R4_HW_CASE STREQUAL "COMMIT_BUDGET")
        set(R4_HW_CASE_ID_VALUE 5)
    elseif(STREAM_LAB_R4_HW_CASE STREQUAL "SYNTHETIC_TASKS")
        set(R4_HW_CASE_ID_VALUE 6)
    elseif(STREAM_LAB_R4_HW_CASE STREQUAL "DMA_WINDOW")
        set(R4_HW_CASE_ID_VALUE 7)
    elseif(STREAM_LAB_R4_HW_CASE STREQUAL "TICK_GAP")
        set(R4_HW_CASE_ID_VALUE 8)
    elseif(STREAM_LAB_R4_HW_CASE STREQUAL "MICROBENCH")
        set(R4_HW_CASE_ID_VALUE 9)
    elseif(STREAM_LAB_R4_HW_CASE STREQUAL "MASK_RESTORE")
        set(R4_HW_CASE_ID_VALUE 10)
    elseif(STREAM_LAB_R4_HW_CASE STREQUAL "TIME_REGRESSION")
        set(R4_HW_CASE_ID_VALUE 11)
    elseif(STREAM_LAB_R4_HW_CASE STREQUAL "COMMIT_PENDING_IRQ")
        set(R4_HW_CASE_ID_VALUE 12)
    elseif(STREAM_LAB_R4_HW_CASE STREQUAL "WINDOW_INTERSECTION")
        set(R4_HW_CASE_ID_VALUE 13)
    elseif(STREAM_LAB_R4_HW_CASE STREQUAL "RESPONSE_SYNTHETIC")
        set(R4_HW_CASE_ID_VALUE 14)
    elseif(STREAM_LAB_R4_HW_CASE STREQUAL "MASK_TIMING")
        set(R4_HW_CASE_ID_VALUE 15)
    elseif(STREAM_LAB_R4_HW_CASE STREQUAL "COMBINED_SERVICE")
        set(R4_HW_CASE_ID_VALUE 16)
    elseif(STREAM_LAB_R4_HW_CASE STREQUAL "PERTURBATION_AB")
        set(R4_HW_CASE_ID_VALUE 17)
    elseif(STREAM_LAB_R4_HW_CASE STREQUAL "R2_NORMAL_ANCHOR")
        set(R4_HW_CASE_ID_VALUE 18)
    elseif(STREAM_LAB_R4_HW_CASE STREQUAL "R2_DROP_ANCHOR")
        set(R4_HW_CASE_ID_VALUE 19)
    elseif(STREAM_LAB_R4_HW_CASE STREQUAL "CUTOFF_LIVE")
        set(R4_HW_CASE_ID_VALUE 20)
    else()
        message(FATAL_ERROR "STREAM_LAB_R4_HW_CASE must select one formal R4 case.")
    endif()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        R4_HW_SOAK_MS=${STREAM_LAB_R4_HW_SOAK_MS}U
        R4_HW_CASE_ID=${R4_HW_CASE_ID_VALUE}U
        R4_HW_ACCOUNTING_ENABLED=${R4_ACCOUNTING_VALUE})
    if(STREAM_LAB_R4_HW_CASE STREQUAL "PERTURBATION_AB")
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
            STREAM_LAB_R4_PERTURBATION_AB=1)
    endif()
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r4_hw_harness.c")
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r4_hw_harness.c"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-mthumb")
    # The R4 timing gate is a benchmark profile, not the size-oriented default
    # Release build.  Make the exact optimisation choice explicit and local to
    # the complete queue/ledger/RuntimeEvent path that is under measurement.
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r4_clock64.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r4_runtime_event.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r4_runtime_target.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r4_tick_service_target.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/stream_queue_adapter.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/stream_token_ledger.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/stream_ownership_core.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../buffer_pool/r2_buffer_pool.c"
        "${FREERTOS_KERNEL_DIR}/queue.c"
        PROPERTIES COMPILE_OPTIONS "-O3;-Wall;-Wextra;-Werror;-mthumb")
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -flto)
    target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -flto)
endif()

# === R3 TARGET HARDWARE HARNESSES BEGIN ===
if(STREAM_LAB_R3_W3_HW)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        STREAM_LAB_R3_W3_HW=1
        R3_W3_HW_CASE_ID=${R3_W3_HW_CASE_ID_VALUE}U)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime"
        "${CMAKE_CURRENT_SOURCE_DIR}/../buffer_pool"
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition")
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_w3_hw_harness.c")
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_w3_hw_harness.c"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-mthumb")
elseif(STREAM_LAB_R3_W4_HW)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        STREAM_LAB_R3_W4_HW=1
        R3_W4_HW_CASE_ID=${R3_W4_HW_CASE_ID_VALUE}U)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime"
        "${CMAKE_CURRENT_SOURCE_DIR}/../buffer_pool"
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition")
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_w4_hw_harness.c")
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_w4_hw_harness.c"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-mthumb")
elseif(STREAM_LAB_R3_W5_HW)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        STREAM_LAB_R3_W5_HW=1
        R3_W5_HW_CASE_ID=${R3_W5_HW_CASE_ID_VALUE}U)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime"
        "${CMAKE_CURRENT_SOURCE_DIR}/../buffer_pool"
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition")
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_w5_hw_harness.c")
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_w5_hw_harness.c"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-mthumb")
elseif(STREAM_LAB_R3_W6_HW)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        STREAM_LAB_R3_W6_HW=1 R3_W6_HW_ANCHOR=${R3_W6_HW_ANCHOR_VALUE}U
        STREAM_LAB_R4_R3_LIFECYCLE_REGRESSION=1)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime" "${CMAKE_CURRENT_SOURCE_DIR}/../buffer_pool"
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition")
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_w6_hw_harness.c")
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_w6_hw_harness.c"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-mthumb")
elseif(STREAM_LAB_R3_W2_HW)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        STREAM_LAB_R3_W2_HW=1
        R3_W2_HW_CASE_ID=${R3_W2_HW_CASE_ID_VALUE}U
    )
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime"
        "${CMAKE_CURRENT_SOURCE_DIR}/../buffer_pool"
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition"
    )
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_w2_hw_harness.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_w2_hw_authority_shim.c"
    )
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_w2_hw_harness.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/r3_w2_hw_authority_shim.c"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-mthumb"
    )
elseif(STREAM_LAB_R2_W6)
    if(R2_W6_MODE_UPPER STREQUAL "DROP")
        set(R2_W6_DROP_VALUE 1)
    else()
        set(R2_W6_DROP_VALUE 0)
    endif()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        STREAM_LAB_R2_W6=1
        R2_W6_K=${STREAM_LAB_R2_W6_K}U
        R2_W6_DROP_MODE=${R2_W6_DROP_VALUE}U
    )
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../buffer_pool"
    )
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/r2_w6_matrix.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/r2_dma_slots.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../buffer_pool/r2_buffer_pool.c"
    )
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/r2_w6_matrix.c"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror"
    )
    if(STREAM_LAB_R2_CT)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE STREAM_LAB_R2_CT=1)
        if(NOT STREAM_LAB_R2_CT_EVENTS STREQUAL "96")
            target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
                R2_W6_EVENTS=${STREAM_LAB_R2_CT_EVENTS}U
            )
        endif()
        if(NOT STREAM_LAB_R2_CT_RUN_TIMEOUT_MS STREQUAL "1000")
            target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
                R2_W6_RUN_TIMEOUT_MS=${STREAM_LAB_R2_CT_RUN_TIMEOUT_MS}U
            )
        endif()
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/r2_ct_protocol.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/r2_ct_control.c"
        )
        set_source_files_properties(
            "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/r2_ct_protocol.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/r2_ct_control.c"
            PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror"
        )
    endif()
elseif(STREAM_LAB_R2_W5)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE STREAM_LAB_R2_W5=1)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../buffer_pool"
    )
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/r2_w5_capacity.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/r2_dma_slots.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../buffer_pool/r2_buffer_pool.c"
    )
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/r2_w5_capacity.c"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror"
    )
elseif(STREAM_LAB_R2_W4)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE STREAM_LAB_R2_W4=1)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../buffer_pool"
    )
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/r2_w4_roundtrip.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/r2_dma_slots.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../buffer_pool/r2_buffer_pool.c"
    )
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/r2_w4_roundtrip.c"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror"
    )
elseif(STREAM_LAB_R2_W3)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE STREAM_LAB_R2_W3=1)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../buffer_pool"
    )
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/r2_w3_rebind.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/r2_dma_slots.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../buffer_pool/r2_buffer_pool.c"
    )
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/r2_w3_rebind.c"
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror"
    )
else()
# === R1 ACQUISITION DRIVER BEGIN ===
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/r1_acquisition.c"
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition"
)
# === R1 ACQUISITION DRIVER END ===
# === R1 BRINGUP HARNESS BEGIN ===
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../acquisition/r1_bringup.c"
)
# === R1 BRINGUP HARNESS END ===

endif()
# === R2 DIAGNOSTIC PROFILES END ===
