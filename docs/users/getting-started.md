# Getting Started

**Audience:** end users and new contributors.
**Purpose:** build the project and run the main binaries.
**Last reviewed:** 2026-09-15.

## Prerequisites

- CMake 3.20 or newer.
- A C++23 compiler and standard library. The current verification uses Apple
  Clang on macOS; other compiler/version combinations need validation. Current
  CMake flags are Unix-style, so MSVC support is not established.
- Python 3.9+ and external Pasmo 0.5.5 for assembly workflow/round-trip tests.
  They are optional for the C++ core.
- Python 3.14+ for the independent LSP; Node/npm and VS Code for its editor client.
- Network access on first configure only if building the GUI targets.

The core library, tests, examples, and headless Spectrum machine do not need GUI
dependencies. Use `-DZ80_BUILD_UI=OFF` for offline/headless builds.

## Build

Always build out of source:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DZ80_BUILD_UI=ON
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
ctest --test-dir build --output-on-failure
```

Individual tests are binaries in `build/`, for example:

```bash
./build/cpu_test
./build/instruction_timing_test
./build/floating_bus_test
```

See [Testing](../testers/testing.md) for optional inputs and skip reporting.
A green CTest run may include ROM checks that exited without exercising a ROM.

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

## Develop source and reconstruct bytes

Use the [Spectrum example](../../examples/spectrum-dev/README.md) for editor setup,
Pasmo paths, manifests and a fresh build/run. For standalone binary reconstruction:

```sh
./build/z80_disassemble --org 0x8000 program.bin > program.asm
```

Read [the export contract](../testers/disassembly-verification.md) before relying
on it: the supported range is nonempty, non-wrapping and at most 65,535 bytes.

## Next

- Use the ZX Spectrum viewer: [Spectrum Viewer](spectrum-viewer.md).
- Use the debugger: [Debugger](debugger.md).
- Diagnose tests and compatibility: [Testing](../testers/testing.md).
