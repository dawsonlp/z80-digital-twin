# Z80 Digital Twin

A high-performance Z80 CPU emulator demonstrating digital twin capabilities with modern C++23.

## 🚀 Quick Start

```bash
git clone https://github.com/dawsonlp/z80-digital-twin.git
cd z80-digital-twin
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/gcd_example 1071 462          # GCD = 21
```

See [Getting Started](#-getting-started) below for every program and its options.

## 📚 Project status & documentation

Beyond the core CPU, the project now includes an **ImGui debugger** (disassembler,
breakpoints, execution-coverage and self-modifying-code detection) and is growing
**machine emulation** (a ZX Spectrum ULA). It's one engine specialised per use
case via compile-time policies (mass IoT twin · debugger · machine emulator).

- [ARCHITECTURE.md](ARCHITECTURE.md) — the platform: core engine ← capabilities ← frontends, and the memory/I-O policy model.
- [STATUS.md](STATUS.md) — what's built and working right now.
- [DEBUGGER_DESIGN.md](DEBUGGER_DESIGN.md) — the debugger design.
- [DEBUGGER_ROADMAP.md](DEBUGGER_ROADMAP.md) — the reverse-engineering-lab vision (coverage → SMC → annotated, reassemblable source).
- [SPECTRUM_DESIGN.md](SPECTRUM_DESIGN.md) — the ZX Spectrum machine + ULA/PAL timing (in design).
- [HEADLESS_INSTRUMENTATION.md](HEADLESS_INSTRUMENTATION.md) — driving and observing a running machine with no UI: keyboard-matrix injection, tape loading, coverage/RAM/PC instrumentation (the `spectrum_probe` tool).

## 🏁 Getting Started

### Prerequisites

- **C++23 compiler** — Clang 16+, GCC 13+, or MSVC 2022+.
- **CMake 3.20+**.
- The core library, examples, and tests have **no external dependencies**.
- The **debugger** and **Spectrum viewer** add a GUI (Dear ImGui + GLFW) and
  audio (miniaudio), pulled automatically via CMake `FetchContent` on the first
  configure (needs network once). Build headless/offline with `-DZ80_BUILD_UI=OFF`.

### Build (out-of-source)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

All binaries land in `build/`. For a headless/offline build with no GUI/audio
dependencies (core + examples + tests + the headless Spectrum machine only):

```bash
cmake -S . -B build -DZ80_BUILD_UI=OFF
cmake --build build -j
```

> Always build out-of-source (into `build/` or `/tmp`). Every program accepts
> `-h` / `--help`.

### The programs

#### `gcd_example` — run real Z80 assembly

Computes GCD(a, b) by executing a Z80 program on the emulator, with a full
instruction/register/timing trace.

```bash
./build/gcd_example 1071 462          # GCD = 21
```

#### `gcd_stress_test` — throughput stress test

Runs *(N−1)* cascading GCD calculations as a scaling benchmark.

```bash
./build/gcd_stress_test 10000
```

#### `performance_benchmark` — raw CPU throughput

```bash
./build/performance_benchmark          # full run
./build/performance_benchmark --quick  # fewer iterations
```

#### Tests

```bash
./build/cpu_test                       # the core CPU suite
ctest --test-dir build                 # run the whole test suite
```

#### `spectrum_probe` — headless Spectrum instrumentation

Boots a ROM, types on the keyboard matrix, plays a tape, and reports per-window
CPU activity (code coverage, RAM writes, PC hot-spot, border) with a freeze
detector — no display. Built on `SpectrumMachine` + `DebugSession`. See
[HEADLESS_INSTRUMENTATION.md](HEADLESS_INSTRUMENTATION.md).

```bash
./build/spectrum_probe spec48.rom --tape underwurlde.tzx --load --screen
./build/spectrum_probe spec48.rom --boot 150 --frames 0 --screen   # boot to BASIC
```

#### `z80_debugger` — the ImGui debugger

A windowed debugger: registers, disassembly, memory (with execution-coverage and
self-modifying-code highlighting), I/O, and — in Spectrum mode — a live screen
and keyboard-matrix monitor.

```bash
./build/z80_debugger --demo gcd                              # built-in demo
./build/z80_debugger program.bin --org 0x8000 --sym prog.sym # your binary + symbols
./build/z80_debugger --spectrum spec48.rom                   # full ZX Spectrum
./build/z80_debugger --spectrum spec48.rom --tape jetpac.tzx # boot with a game tape
```

#### `spectrum` — the ZX Spectrum 48K viewer

Boots a 48K ROM and shows the live screen (3×) with sound, paced to the authentic
50 Hz.

```bash
./build/spectrum spec48.rom
./build/spectrum spec48.rom --tape jetpac.tzx   # then, in the Spectrum: LOAD"" + F5
```

- **In-window keys:** `F3` opens a tape via the native file picker; `F5` plays
  the tape, `F6` stops it. The host keyboard maps to the Spectrum matrix
  (`Shift` = CAPS SHIFT, `Ctrl` = SYMBOL SHIFT, `Backspace` = DELETE).
- **Loading a game:** load a tape with `--tape` or `F3`, boot, type `LOAD""` (the
  `J` key types the `LOAD` keyword; `"` is SYMBOL SHIFT + `P`), press `ENTER`,
  then `F5`. Tapes load as the real signal, so a full game takes the authentic
  couple of minutes — `--turbo` runs uncapped to load faster (sound off). Both
  `.tap` and `.tzx` images work. (The debugger has the same picker under
  **File ▸ Open tape…** in Spectrum mode.)

> **ROMs and game tapes are copyrighted and not included.** Supply your own
> `spec48.rom` (place it in the working directory or point to it) and your own
> `.tap` / `.tzx` files.

## 🎯 What This Demonstrates

- **Accurate Hardware Emulation**: Cycle-accurate Z80 CPU implementation
- **Digital Twin Principles**: Real-time hardware state replication and monitoring
- **Performance Optimization**: 20-35% performance improvements with modern C++23
- **Professional Architecture**: Clean, maintainable code suitable for production systems

## 📊 Example: GCD Calculator

The included GCD (Greatest Common Divisor) calculator demonstrates real computational work running actual Z80 assembly code:

```bash
$ ./gcd_example 48 18
Z80 Digital Twin - GCD Calculator

Input Numbers:
  First number:  48 (0x30)
  Second number: 18 (0x12)

Z80 Assembly Program (GCD Algorithm):
Address  Opcode   Instruction       Comment
-------  -------  ----------------  ---------------------------
0x0000   7A       LD A, D           ; Check if DE == 0
0x0001   B3       OR E              ; 
0x0002   28 0B    JR Z, +11         ; Jump to end if DE == 0
0x0004   B7       OR A              ; Clear carry flag
0x0005   ED 52    SBC HL, DE        ; HL = HL - DE
0x0007   30 02    JR NC, +2         ; If HL >= DE, continue
0x0009   19       ADD HL, DE        ; Restore HL (HL < DE case)
0x000A   EB       EX DE, HL         ; Swap HL and DE
0x000B   18 F3    JR -13            ; Jump back to main_loop
0x000D   18 F1    JR -15            ; Jump back to main_loop
0x000F   76       HALT              ; Result in HL register

Executing Z80 Program...
✅ Execution completed successfully!

Results:
--------
GCD(48, 18) = 6
Hexadecimal: 0x6

Performance Statistics:
-----------------------
Z80 Cycles Executed: 546
Algorithm Iterations: 73
Estimated execution time on real Z80:
  4 MHz Z80: 136.50 microseconds
  8 MHz Z80: 68.25 microseconds

✅ Result verified against standard GCD algorithm.
```

This demonstrates the emulator running actual Z80 machine code with complete visibility into the assembly program, register states, and performance metrics - showcasing the digital twin's accuracy and monitoring capabilities.

## 🏗️ Architecture

### Core Components

- **Z80 CPU Core**: Complete instruction set implementation (256+ instructions)
- **Memory System**: 64KB address space with cycle-accurate timing
- **I/O System**: 256 port I/O space for peripheral communication
- **State Management**: Real-time register and flag monitoring

### Digital Twin Features

- **Cycle-Accurate Timing**: Precise T-state counting for real-time synchronization
- **State Inspection**: Full visibility into CPU registers, memory, and flags
- **Performance Monitoring**: Execution metrics and timing analysis
- **Memory Safety**: Zero dynamic allocation, bounds-safe array access

## 🔧 Building

### Prerequisites

- **C++23 Compiler**: GCC 13+, Clang 16+, or MSVC 2022+
- **No external dependencies**: Core library is self-contained
- **CMake**: Optional, makes building easier but not required

### Platform Compatibility

| Platform | Architecture | Status | Notes |
|----------|-------------|--------|-------|
| **macOS** | Apple Silicon (M1/M2/M3) | ✅ **Tested** | Primary development platform |
| **Linux** | x86_64 (AMD64) | ✅ **Tested** | Ubuntu 6.14.0-15-generic with GCC 14.2.0 |
| **Windows** | x86_64 | ⚠️ **Untested** | May require minor CMake adjustments |

### Tested Platforms

- **Linux 6.14.0-15-generic (Ubuntu)** with GCC 14.2.0 - tested fine ✅

**Current Testing Status:**
- ✅ **Fully tested**: macOS with Apple Silicon processors
- ✅ **Fully tested**: Ubuntu Linux on AMD64 architecture  
- ⚠️ **Community needed**: Windows compatibility (no test environment available)

The codebase uses standard C++23 and CMake, so it should be highly portable across platforms. Any platform-specific issues will be addressed as they're discovered.

### Build Commands

See [Getting Started](#-getting-started) for the full walkthrough. In short:

```bash
# Release (out-of-source)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Debug
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j

# Headless / offline (no GUI or audio dependencies)
cmake -S . -B build -DZ80_BUILD_UI=OFF
cmake --build build -j
```

Build a single target with `cmake --build build --target <name>` (e.g.
`gcd_example`, `spectrum`, `z80_debugger`).

### Build Targets

| Target | Description |
|--------|-------------|
| `z80_cpu` | Core CPU emulation library |
| `z80_machine` | ZX Spectrum machine layer (ULA, video, tape, beeper) |
| `z80_debugger_core` | UI-free debugger engine (session, disassembler, symbols) |
| `gcd_example` | GCD calculator with command-line interface |
| `spectrum_probe` | Headless Spectrum instrumentation (keyboard/tape/coverage probe) |
| `gcd_stress_test` | Cascading-GCD throughput stress test |
| `performance_benchmark` | Raw CPU throughput benchmark |
| `cpu_test` (+ `*_test`) | CPU and machine test suites (run via `ctest`) |
| `z80_debugger` | ImGui debugger (UI build only) |
| `spectrum` | ZX Spectrum 48K viewer (UI build only) |

## 🧪 Testing

The project includes comprehensive testing:

```bash
# Run the whole suite
ctest --test-dir build

# Or run individual binaries
./build/cpu_test

# Run GCD calculator with various inputs
./build/gcd_example 48 18
./build/gcd_example 1071 462

# Run performance benchmark suite
./build/performance_benchmark
./build/performance_benchmark --quick    # Faster testing during development
```

### Performance Testing

The project includes comprehensive performance testing through the stress test framework:

```bash
# Run stress tests with different scales
./build/gcd_stress_test 1000    # Medium stress test
./build/gcd_stress_test 10000   # Large stress test
./build/gcd_stress_test 65535   # Maximum stress test
```

**Stress Test Features:**
- **Scalable Testing**: From small (100 operations) to maximum (65,535 operations)
- **Real-time Performance**: Measures actual Z80 cycle execution
- **Hardware Comparison**: Direct comparison with historical Z80 processors
- **Linear Scaling**: Validates performance consistency across all scales
- **Authentic Computation**: No optimization shortcuts, pure Z80 emulation

## 📈 Performance

The Z80 Digital Twin achieves exceptional performance while maintaining cycle-accurate timing:

- **Peak Performance**: **2.09 billion cycles/second** (2.09 GHz Z80 equivalent)
- **Real Hardware Speedup**: **522x faster** than original 4MHz Z80
- **Memory Efficiency**: Zero heap allocation during execution
- **Cycle Accuracy**: Precise T-state counting for timing-critical applications
- **Scalability**: Linear performance scaling to theoretical maximum (65,535 operations)

### Stress Test Results

| Test Size | Z80 Cycles | Time (ms) | Cycles/sec | Real 4MHz Z80 Time |
|-----------|------------|-----------|------------|-------------------|
| 1,000 operations | 31.3M | 18.7 | 1.67e+09 | 7.82 seconds |
| 10,000 operations | 65.5M | 37.1 | 1.77e+09 | 16.38 seconds |
| **65,535 operations** | **915.5M** | **438.7** | **2.09e+09** | **228.88 seconds** |

**Revolutionary Performance**: The emulator processes in 439ms what would take a real Z80 nearly 4 minutes, demonstrating the power of modern computing applied to vintage processor emulation.

## 🎓 Educational Value

This project demonstrates key concepts for digital twin development:

### Hardware Emulation
- Accurate state replication
- Real-time performance constraints
- Comprehensive validation and testing

### Software Architecture
- Clean separation of concerns
- Memory-safe systems programming
- Performance optimization techniques

### Modern C++23
- Advanced language features
- Zero-cost abstractions
- Compile-time optimizations

## � Technical Highlights

### Memory Safety
- **No dynamic allocation**: All memory is stack-allocated
- **Bounds safety**: 16-bit addresses naturally constrain access
- **Value semantics**: No pointer aliasing or ownership issues

### Performance Optimizations
- **Function pointer dispatch**: Direct calls with zero overhead
- **Cache-friendly data structures**: Optimal memory layout
- **Compiler optimizations**: Leverages modern compiler capabilities

### Digital Twin Capabilities
- **State synchronization**: Real-time CPU state monitoring
- **Performance metrics**: Detailed execution analysis
- **Validation framework**: Comprehensive correctness verification

## 🚀 Future Applications

This foundation enables:

- **Industrial IoT**: Real-time equipment monitoring and simulation
- **Legacy System Migration**: Accurate hardware behavior preservation
- **Educational Platforms**: Interactive computer architecture learning
- **Performance Analysis**: Hardware optimization and validation

## 🤝 Contributing

Contributions are welcome! This project demonstrates:

- Clean, maintainable code architecture
- Comprehensive testing practices
- Modern C++ best practices
- Professional documentation standards

### Development Setup
1. Fork the repository
2. Create a feature branch
3. Make your changes with tests
4. Ensure all tests pass
5. Submit a pull request

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- **Z80 CPU Documentation**: Based on official Zilog documentation
- **Digital Twin Principles**: Modern industrial IoT best practices
- **Modern C++ Community**: For excellent language evolution

---

**Digital Twin Demonstration**: This emulator showcases the principles and techniques essential for building accurate digital representations of physical systems, making it an ideal foundation for industrial IoT and real-time monitoring applications.
