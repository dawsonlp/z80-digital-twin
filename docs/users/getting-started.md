# Getting Started

**Audience:** end users and new contributors.
**Purpose:** build the project and run the main binaries.
**Last reviewed:** 2026-06-09.

## Prerequisites

- CMake 3.20 or newer.
- A C++23 compiler: Clang 16+, GCC 13+, or MSVC 2022+.
- Network access on first configure only if building the GUI targets.

The core library, tests, examples, and headless Spectrum machine do not need GUI
dependencies. Use `-DZ80_BUILD_UI=OFF` for offline/headless builds.

## Build

Always build out of source:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Headless/offline build:

```bash
cmake -S . -B build -DZ80_BUILD_UI=OFF
cmake --build build -j
```

Debug build:

```bash
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DZ80_BUILD_UI=OFF
cmake --build build-debug -j
```

## Run Tests

```bash
ctest --test-dir build
```

Individual tests are binaries in `build/`, for example:

```bash
./build/cpu_test
./build/instruction_timing_test
./build/floating_bus_test
```

ROM-dependent tests skip cleanly when no ROM is configured.

## Main Programs

```bash
./build/gcd_example 1071 462
./build/gcd_stress_test 10000
./build/performance_benchmark --quick
./build/spectrum_probe spec48.rom --boot 150 --frames 0 --screen
./build/z80_debugger --demo gcd
./build/spectrum spec48.rom
```

ROMs and game tapes are copyrighted and not included. See
[Test Assets](../testers/test-assets.md) for local asset conventions.

## Next

- Use the ZX Spectrum viewer: [Spectrum Viewer](spectrum-viewer.md).
- Use the debugger: [Debugger](debugger.md).
- Diagnose tests and compatibility: [Testing](../testers/testing.md).
