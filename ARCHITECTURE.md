# Architecture

**Status:** living document. Records the layering and the policy model that keep
three different use cases sharing one engine without interfering.
**Date:** 2026-06-06

This project started as a single highly-optimized digital twin, then grew a
debugger, and is now growing machine emulators. This document names the shape
that keeps all three first-class.

---

## 1. The three use cases (the forces)

| Use case | What it demands | What it must **not** pay for |
|---|---|---|
| **Mass IoT twin** (100s–1000s of instances) | max throughput, small per-instance footprint, real peripheral I/O (GPIO, serial, …) | any observation, UI, device, or timing machinery |
| **Debugger** | memory/I/O observation, stepping, breakpoints, coverage/SMC, UI | hard-realtime accuracy |
| **Machine emulator** (e.g. ZX Spectrum) | realtime → cycle-accurate timing, devices, interrupts | the debugger; the twin's zero-overhead budget |

The design goal: each use case is a **configuration** of one engine, paying only
for what it uses.

## 2. Layers

```
CORE ENGINE      z80_cpu (src/)                  Z80 + its environment policies
     ▲
CAPABILITIES     z80_debugger_core   z80_machine  (siblings — neither depends on the other)
     ▲
FRONTENDS        twin runner   debugger/spectrum app   (each composes the capabilities it needs)
```

**Rules:**
- A capability is a **UI-free static library** depending only on layers beneath
  it. Capabilities are **siblings**; one never depends on another.
- The **only** UI/graphics dependency lives in frontends.
- Tests link the relevant library and run **headless** (this is the primary
  anti-rot guarantee, not the UI).

## 3. The CPU's environment = two compile-time policies

The CPU is parameterized on the two halves of its environment — what it reads/
writes *into* (memory) and what it talks *to* (I/O):

```cpp
template <class Memory = FastMemory, class Io = FastIo>
class CPUImpl { Memory memory; Io io; /* … */ };

using CPU = CPUImpl<FastMemory, FastIo>;   // preserves existing usage
```

- **Compile-time** binding ⇒ each configuration is fully inlined and **zero-cost**;
  a GPIO twin carries no Spectrum-keyboard logic and vice-versa.
- **Interrupts** are an *external method* (`Interrupt()`), not a policy — the
  twin loop never calls it, so it costs nothing when unused.

This is the mechanism. The use cases are just **named instantiations** of it.

## 4. Named configurations (use case → config)

```cpp
// Mass IoT twin — the performance reference; specialise I/O per deployment:
using CPU          = CPUImpl<FastMemory, FastIo>;        // default
//                   CPUImpl<FastMemory, GpioIo>         // real Raspberry Pi pins
//                   CPUImpl<FastMemory, SerialIo>       // UART / serial chip

// ZX Spectrum (runnable and debuggable):
using SpectrumCPU  = CPUImpl<ObservableMemory, SpectrumIo>;
```

Each picks its environment at compile time; nothing bleeds across; the bare
`FastMemory + FastIo` stays the benchmark-guarded reference (§8).

## 5. Memory policies

- **`FastMemory`** — zero-overhead `std::array`; the twin's plug. The hot path is
  a direct indexed access.
- **`ObservableMemory`** — a **multi-observer write hook** (a small list of
  `(addr, old, new)` callbacks). Used by the debugger *and* machines; both attach
  observers, so a *running* machine is also debuggable. This **subsumes** the
  earlier single-hook `DebugMemory` (which becomes this).

Reads are never hooked, so observation costs nothing on fetch/operand traffic.

## 6. I/O policies

The `Io` policy answers "what does this Z80 talk to," with the **full 16-bit
port** (`(A<<8)|n` for `IN/OUT (n)`, `BC` for `(C)` forms). Adopting the policy
*is* the 16-bit-addressing fix the Spectrum keyboard needs — there's no separate
hook.

```cpp
struct Io {                          // policy contract
    uint8_t In(uint16_t port);
    void    Out(uint16_t port, uint8_t value);
};
```

- **`FastIo`** — today's 256-byte array behaviour; the twin default.
- **`SpectrumIo`** — routes ports to ULA (`0xFE`), Kempston, etc.
- **`GpioIo` / `SerialIo` / …** — drive real hardware or device models.

A policy is a thin seam; stateful device logic (e.g. the ULA) lives in a device
object the policy forwards to, so devices stay independently testable and can be
shared across the I/O *and* memory seams (the ULA needs both).

## 7. The debugger over policy configurations

The disassembler and symbol table are already CPU-agnostic (they use a
byte-reader callback). Only `DebugSession` references the concrete CPU. So:

- **Decision (recommended, pending final confirmation): template `DebugSession`
  on the CPU configuration**, constrained to an `ObservableMemory` plug, and
  explicitly instantiate it for each shipped config (e.g. the Spectrum config
  and a generic `CPUImpl<ObservableMemory, FastIo>`). Disassembler/symbols stay
  compiled and shared.
- This lets the debugger operate on the **real** compile-time config it ships —
  you debug exactly what runs — while keeping the twin on bare policies.
- (Considered alternative: fix the debugger to one instrumented config with a
  runtime-routing I/O. Simpler lib, but then the debugged machine uses runtime
  I/O routing rather than its real `SpectrumIo`. Rejected for losing fidelity.)

## 8. The performance invariant (sacred)

`CPUImpl<FastMemory, FastIo>` is the **null configuration** and the performance
reference. Every capability must be **opt-in and zero-cost when off**, achieved
by compile-time policy selection — never by runtime flags on the hot path. The
`performance_benchmark` is the guardrail: **a throughput regression there is a
build failure.** This single rule is what stops the twin from eroding as the
debugger and emulators grow.

## 9. Module layout & build targets

```
src/                  core engine            -> z80_cpu        (FastMemory, ObservableMemory, FastIo)
debugger/{exec,disasm,symbols}  capability   -> z80_debugger_core
machine/                        capability   -> z80_machine    (Spectrum: SpectrumIo, ULA, decoder, …)
debugger/ui  (+ machine UI panels)  frontend -> z80_debugger   (+ imgui)
tests/                           headless tests link the relevant library
```

Directory names are kept as-is for now (the **layering rules** matter more than
folder names); a `core/` + `apps/` rename can happen later at a clean boundary
if desired. New capabilities follow §2's sibling rule.

---

*Companion docs: [DEBUGGER_DESIGN.md](DEBUGGER_DESIGN.md) (debugger),
[SPECTRUM_DESIGN.md](SPECTRUM_DESIGN.md) (the Spectrum machine + ULA/PAL timing),
[DEBUGGER_ROADMAP.md](DEBUGGER_ROADMAP.md) (reverse-engineering vision),
[STATUS.md](STATUS.md) (current state).*
