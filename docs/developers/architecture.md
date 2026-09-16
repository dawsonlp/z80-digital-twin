# Architecture

**Audience:** developers extending the CPU, machine, debugger, or tooling.
**Last reviewed:** 2026-09-15.
**Source of truth:** [CMake](../../CMakeLists.txt), [CPU](../../src/z80_cpu.h),
[DebugSession](../../debugger/exec/debug_session.h), and
[SpectrumMachine](../../machine/spectrum/spectrum_machine.h).

## Layers and ownership

| Layer | Target / location | Responsibility |
|---|---|---|
| CPU | `z80_cpu`, `src/` | Registers, instruction semantics, T-states, memory/I/O policy access. Static library. |
| Debugger capability | `z80_debugger_core`, `debugger/{exec,disasm,symbols}/` | Execution control, evidence, disassembly and symbols. UI-free static library. |
| Machine capability | `z80_machine`, `machine/` | Spectrum ULA, keyboard, tape, beeper, video and instruction/frame scheduling. Header-only INTERFACE target. |
| Frontends | `z80_debugger`, `spectrum`, `spectrum_probe` | Compose capabilities for interactive or headless use. |
| Source tools | `lsp-z80/`, `tools/spectrum_dev.py`, `z80_disassemble` | Independent language server, external assembler workflow and byte-preserving source export. |

The machine and debugger capability targets each depend on the CPU, not each
other. Frontends compose them. GLFW, ImGui, OpenGL, native file dialogs and
`z80_audio` belong to GUI targets, enabled by `Z80_BUILD_UI`. Headless tools and
tests build without those dependencies.

## Compile-time environment policies

`CPUImpl<Memory, Io>` selects both policies at compile time. CPU definitions
remain in `src/z80_cpu.cpp` with explicit instantiations; adding an arbitrary
policy requires a corresponding compiled instantiation, not just a new alias.

| Memory policy | Current use | Behavior |
|---|---|---|
| `FastMemory` | Bare CPU and throughput reference | Direct byte storage without observation. |
| `ObservableMemory` | Spectrum viewer | Multiple write observers, ROM protection, refused-write observation and host loading. |
| `MetadataMemory` | Debugger and headless probe | Composes observable memory and adds byte revisions, instruction capture and attributed access summaries. |

Rich analysis stays out of the cheaper memory policies. `MetadataMemory`
distinguishes instruction-stream reads, CPU data access, interrupt access and
host mutation. Inspection and ULA reads do not count as CPU data reads. These
are software access summaries, not a cycle-exact bus trace.

The bare `CPU` alias uses `FastMemory` and the default `OpenBusIo`. Other I/O
policies are `LatchedIo`, `CallbackIo`, and `ObservableIo<Inner>`. Ports retain
their 16-bit addresses. `CallbackIo` forwards to machine devices;
`ObservableIo` optionally records transactions. The UI reads that log rather
than issuing side-effecting port reads to refresh a display.

## Actual configurations

```cpp
// z80::CPU
CPUImpl<FastMemory, OpenBusIo>

// SpectrumMachine::Cpu: viewer
CPUImpl<ObservableMemory, ObservableIo<CallbackIo>>

// DebugCPU == DebugSpectrumMachine::Cpu: debugger and probe
CPUImpl<MetadataMemory, ObservableIo<CallbackIo>>
```

`SpectrumMachineImpl<Memory>` shares scheduling and device wiring across both
machine aliases. `DebugSession` is a concrete class over `DebugCPU`, not a
session template. It references the same CPU owned by `DebugSpectrumMachine`.
Without a Spectrum attached, its callback I/O reads as open bus. There is no
separate `SpectrumIo` policy in the current source.

## Instruction and machine boundaries

`CPUImpl::Step()` advances one opcode/prefix stage. `StepInstruction()` owns
bounded whole-instruction execution and reports completion, HALT or budget
exhaustion. `DebugSession` retains pending captures across incomplete prefix
slices and publishes instruction evidence only on completion.

`SpectrumMachineImpl::prepare_execution()` and `advance_execution()` bracket
execution. Viewer frame batching and debugger/probe instruction control use
this same lifecycle. Partial frames survive pause/resume; rendering uses the
latest completed frame. See [Spectrum design](spectrum-machine-design.md) for
the existing early-HALT and one-shot frame-interrupt limitations.

## Analysis and persistence boundaries

Byte activity belongs to `MetadataMemory`; completed instruction evidence and
counts belong to `InstructionHistory` in the debugger core. The latest record
per address survives eviction from the separate 8192-event recent-history
queue. UI panels interpret those records without owning execution state.

Analysis is currently in memory only. JSON `.sym` files persist typed labels
and descriptions, not execution evidence or machine state. Architectural vector
labels are defaults, not evidence that code executed. See
[debugger design](debugger-design.md) for retention and reset contracts.

The LSP runs as an independent Python process over stdio. VS Code tasks and
terminal users invoke the same external Pasmo workflow and inspectable build
artifacts. Starting a fresh debugger from a build is implemented; live reload
and durable reverse-engineering projects remain future work.

## Performance and support limits

The bare CPU is the performance reference. Policy selection avoids adding
metadata work to that configuration, but performance claims require a measured
Release comparison. `performance_benchmark` is a standalone executable; CMake
does not enforce a throughput threshold as a build or CTest failure.

Current verification is primarily macOS. CMake contains Unix-style compiler
flags; do not treat the use of C++23 or cross-platform GUI libraries as proof of
Windows/MSVC support. The installed-header layout also remains incomplete:
installation flattens headers although CPU includes use `memory/` and `io/`.
Use the source-tree CMake targets until packaging is corrected.

Historical rationale: [previous architecture](../archive/architecture-pre-2026-09-15.md).
Current work: [roadmap](roadmap.md). Validation: [testing](../testers/testing.md).
