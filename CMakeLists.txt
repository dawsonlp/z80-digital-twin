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
# Main Executable
# =============================================================================
# Executable Targets (Commented out - files not yet created)
# =============================================================================

# TODO: Uncomment when files are created
# add_executable(gcd_example src/main.cpp)
# target_link_libraries(gcd_example PRIVATE z80_cpu)

add_executable(cpu_test tests/cpu_test.cpp)
target_link_libraries(cpu_test PRIVATE z80_cpu)

# Example programs
add_executable(gcd_example examples/gcd_example.cpp)
target_link_libraries(gcd_example PRIVATE z80_cpu)

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
message(STATUS "  z80_cpu        - Core Z80 CPU emulation library")
message(STATUS "")
