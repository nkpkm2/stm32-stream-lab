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
set(STREAM_LAB_R2_CT_EVENTS "96" CACHE STRING "W6 event count for R2 control-traffic acceptance")
set_property(CACHE STREAM_LAB_R2_CT_EVENTS PROPERTY STRINGS 96 800)
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

if(STREAM_LAB_R2_CT AND NOT STREAM_LAB_R2_W6)
    message(FATAL_ERROR "STREAM_LAB_R2_CT is a modifier of STREAM_LAB_R2_W6 and requires W6")
endif()
if(NOT STREAM_LAB_R2_CT_EVENTS MATCHES "^(96|800)$")
    message(FATAL_ERROR "STREAM_LAB_R2_CT_EVENTS must be 96 or 800")
endif()
if(NOT STREAM_LAB_R2_CT AND NOT STREAM_LAB_R2_CT_EVENTS STREQUAL "96")
    message(FATAL_ERROR "Non-default STREAM_LAB_R2_CT_EVENTS requires STREAM_LAB_R2_CT=ON")
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
if(STREAM_LAB_R2_W6)
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
