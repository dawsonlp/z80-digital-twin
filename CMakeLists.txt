#
# Z80 Digital Twin - High-performance Z80 CPU emulator
# Copyright (c) 2025 Larry Dawson
# Licensed under the MIT License (see LICENSE file)
#

cmake_minimum_required(VERSION 3.20)
project(Z80DigitalTwin VERSION 1.0.0 LANGUAGES CXX)

# =============================================================================
# Project Configuration
# =============================================================================

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# =============================================================================
# Compiler Flags
# =============================================================================

# Common flags for all configurations
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

# Debug flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

# Release flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")

# =============================================================================
# Source Files
# =============================================================================

set(CORE_SOURCES
    src/z80_cpu.cpp
)

set(CORE_HEADERS
    src/z80_cpu.h
    src/memory/fast_memory.h
    src/memory/debug_memory.h
)

# =============================================================================
# Core Library
# =============================================================================

# Create the core Z80 CPU library
add_library(z80_cpu STATIC
    ${CORE_SOURCES}
    ${CORE_HEADERS}
)

target_include_directories(z80_cpu PUBLIC src)
target_compile_features(z80_cpu PUBLIC cxx_std_23)

# =============================================================================
# Debugger Core Library (no UI dependencies)
# =============================================================================

# Execution core: owns the debug loop, stepping, breakpoints, watchpoints.
# UI-free and unit-testable. Disassembler/symbols will join this library next.
add_library(z80_debugger_core STATIC
    debugger/exec/debug_session.cpp
    debugger/exec/debug_session.h
    debugger/disasm/disassembler.cpp
    debugger/disasm/disassembler.h
    debugger/symbols/symbol_table.cpp
    debugger/symbols/symbol_table.h
)

target_include_directories(z80_debugger_core PUBLIC debugger/exec debugger/disasm debugger/symbols)
target_link_libraries(z80_debugger_core PUBLIC z80_cpu)
target_compile_features(z80_debugger_core PUBLIC cxx_std_23)

# =============================================================================
# Main Executable
# =============================================================================
# Executable Targets (Commented out - files not yet created)
# =============================================================================

# TODO: Uncomment when files are created
# add_executable(gcd_example src/main.cpp)
# target_link_libraries(gcd_example PRIVATE z80_cpu)

add_executable(cpu_test tests/cpu_test.cpp)
target_link_libraries(cpu_test PRIVATE z80_cpu)

# DebugMemory plug verification (write hook + plug parity)
add_executable(debug_memory_test tests/debug_memory_test.cpp)
target_link_libraries(debug_memory_test PRIVATE z80_cpu)

# Debugger execution core (stepping, breakpoints, watchpoints)
add_executable(debug_session_test tests/debug_session_test.cpp)
target_link_libraries(debug_session_test PRIVATE z80_debugger_core)

# Disassembler (all prefixes, length + mnemonic + operands, symbol resolution)
add_executable(disassembler_test tests/disassembler_test.cpp)
target_link_libraries(disassembler_test PRIVATE z80_debugger_core)

# Symbol table (JSON .sym load/save, resolver, forgiving parse)
add_executable(symbol_table_test tests/symbol_table_test.cpp)
target_link_libraries(symbol_table_test PRIVATE z80_debugger_core)

# Performance benchmark
add_executable(performance_benchmark tests/performance_benchmark.cpp)
target_link_libraries(performance_benchmark PRIVATE z80_cpu)

# Example programs
add_executable(gcd_example examples/gcd_example.cpp)
target_link_libraries(gcd_example PRIVATE z80_cpu)

add_executable(gcd_stress_test examples/gcd_stress_test.cpp)
target_link_libraries(gcd_stress_test PRIVATE z80_cpu)

# =============================================================================
# Debugger UI (ImGui) — optional, pulls GLFW + Dear ImGui via FetchContent
# =============================================================================
# Disable with -DZ80_BUILD_UI=OFF for offline / headless builds where the
# debugger core and tests are all that's needed.
option(Z80_BUILD_UI "Build the ImGui debugger UI (requires network on first configure)" ON)

if(Z80_BUILD_UI)
    include(FetchContent)

    # --- GLFW (windowing) ---------------------------------------------------
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)

    # --- Dear ImGui (no CMakeLists upstream; compile the sources we need) ----
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.90.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(imgui)

    find_package(OpenGL REQUIRED)

    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    # SYSTEM so consumers don't get warnings from ImGui's own headers.
    target_include_directories(imgui SYSTEM PUBLIC
        ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
    # Don't lint vendored third-party sources.
    target_compile_options(imgui PRIVATE -w)

    # --- The debugger application -------------------------------------------
    add_executable(z80_debugger
        debugger/main.cpp
        debugger/ui/debugger_app.cpp
        debugger/ui/debugger_app.h
        debugger/ui/symbol_edit.cpp
        debugger/ui/symbol_edit.h
        debugger/ui/panels/control_panel.cpp
        debugger/ui/panels/registers_panel.cpp
        debugger/ui/panels/disassembly_panel.cpp
        debugger/ui/panels/memory_panel.cpp
        debugger/ui/panels/io_panel.cpp)
    target_include_directories(z80_debugger PRIVATE debugger/ui debugger/ui/panels)
    target_link_libraries(z80_debugger PRIVATE z80_debugger_core imgui)
    target_compile_features(z80_debugger PRIVATE cxx_std_23)
endif()

# =============================================================================
# Installation
# =============================================================================

install(TARGETS z80_cpu
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(FILES ${CORE_HEADERS}
    DESTINATION include/z80
)

# =============================================================================
# Summary
# =============================================================================

message(STATUS "")
message(STATUS "Z80 Digital Twin Configuration Summary:")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "")
message(STATUS "Available targets:")
message(STATUS "  z80_cpu               - Core Z80 CPU emulation library")
message(STATUS "  cpu_test              - Comprehensive CPU functionality tests")
message(STATUS "  performance_benchmark - Performance benchmark suite")
message(STATUS "  gcd_example           - GCD calculator demonstration")
message(STATUS "")
