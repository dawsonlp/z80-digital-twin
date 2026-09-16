# Decisions

**Audience:** developers.
**Purpose:** record durable architectural decisions without preserving stale
roadmap text as current work.
**Last reviewed:** 2026-09-15.

## CPU Environment Is Policy-Based

The generic CPU core is templated over memory and I/O policies. Machine-specific
behavior, such as Spectrum ULA ports, stays outside the generic CPU.

Consequence: the fast core path can remain simple, while debugger and machine
configurations add observation or device behavior explicitly.

## I/O Is Device Behavior, Not Port Storage

The default bare-Z80 I/O behavior is open bus. Latching is an opt-in test/simple
device policy, not the default model.

Consequence: Spectrum keyboard, floating bus, EAR, MIC, and border behavior live
in the machine/ULA layer.

## Debugger Runs The Real CPU Configuration

`DebugSession` drives the CPU owned by `DebugSpectrumMachine`, using
`MetadataMemory` and `ObservableIo<CallbackIo>`. The viewer uses the same
`SpectrumMachineImpl` runtime with cheaper `ObservableMemory`.

Consequence: debugger observations are observations of the running machine, not
a proxy.

## Floating Bus Belongs To The ULA

The floating bus is a Spectrum ULA/bus behavior. It is implemented in the
Spectrum ULA path and verified independently by `floating_bus_test`.

Full rationale: [floating-bus-design.md](floating-bus-design.md).

## External Compatibility Assets Stay Local

ROMs, game tapes, and game-derived goldens are not committed. Harnesses must
skip cleanly when those assets are absent.

Consequence: the in-repo test suite stays legally clean and green on a fresh
checkout, while local compatibility runs can still be strict.

## CPU Owns Instruction Completion; Machine Owns Time Progress

Bounded `StepInstruction()` belongs to the CPU. Debugger control and viewer frame
batching use shared Spectrum prepare/advance hooks. Pause and rendering do not
advance emulated time. HALT and interrupt fidelity remain explicit limitations.

## Optional Analysis and Independent Retention

Rich byte accounting belongs to `MetadataMemory`, leaving the other policies
separate. Latest per-address evidence outlives chronological queue eviction.
Executed/self-modified facts are independent of current-byte validity. Evidence
is in memory only; a symbol file is not a saved analysis session.

## External Tools and Inspectable Artifacts

Pasmo 0.5.5 is the first assembler/dialect. Editor tasks and terminal commands
share an ordinary CLI, binary/symbol outputs and build manifest. The independent
LSP does not embed an assembler or require a running emulator. The current launch
starts a new debugger; live reload remains future work.
