cmake_minimum_required(VERSION 3.20)

project(Q6A_Hexagon_DSP_Benchmark
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(q6a-dsp-benchmark
    src/main.cpp
    src/benchmark.cpp
    src/thermal.cpp

    workloads/motor_control/motor_control.cpp
    workloads/motor_control/motor_control_fixed.cpp

    validation/motor_control/motor_control_benchmark.cpp
)

target_include_directories(q6a-dsp-benchmark
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/workloads/motor_control
        ${CMAKE_CURRENT_SOURCE_DIR}/validation/motor_control
)

target_compile_options(q6a-dsp-benchmark
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
)

target_compile_definitions(q6a-dsp-benchmark
    PRIVATE
        Q6A_DSP_BENCHMARK_VERSION="${PROJECT_VERSION}"
)

# Keep the fixed-point CPU baseline scalar.
# This provides a clean CPU reference for the later HVX comparison.
set_source_files_properties(
    workloads/motor_control/motor_control_fixed.cpp
    PROPERTIES
        COMPILE_OPTIONS "-fno-tree-vectorize;-fno-tree-slp-vectorize"
)
