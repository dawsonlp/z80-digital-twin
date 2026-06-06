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
// Bare Z80 — honest default (open bus); the performance reference:
using CPU          = CPUImpl<FastMemory, OpenBusIo>;
// Mass IoT twin — specialise I/O per deployment:
//                   CPUImpl<FastMemory, GpioIo>         // real Raspberry Pi pins
//                   CPUImpl<FastMemory, SerialIo>       // UART / serial chip
//                   CPUImpl<FastMemory, LatchedIo>      // simple latched ports (opt-in)

// ZX Spectrum, runnable: real device behaviour, zero observation overhead:
using SpectrumCPU      = CPUImpl<ObservableMemory, SpectrumIo>;
// ZX Spectrum, in the debugger: same truth + bus-transaction observation:
using SpectrumDebugCPU = CPUImpl<ObservableMemory, ObservableIo<SpectrumIo>>;
```

Each picks its environment at compile time; nothing bleeds across; the bare
`FastMemory + OpenBusIo` stays the benchmark-guarded reference (§8).

## 5. Memory policies

- **`FastMemory`** — zero-overhead `std::array`; the twin's plug. The hot path is
  a direct indexed access.
- **`ObservableMemory`** — a **multi-observer write hook** (a small list of
  `(addr, old, new)` callbacks). Used by the debugger *and* machines; both attach
  observers, so a *running* machine is also debuggable. This **subsumes** the
  earlier single-hook `DebugMemory` (which becomes this).

Reads are never hooked, so observation costs nothing on fetch/operand traffic.

## 6. I/O policies — I/O is a *device*, not storage

Real Z80 I/O has no implicit storage. `OUT` is a **transient bus pulse** (~2
T-states); whether a value persists is entirely up to the external device (it
must latch it). `IN` reads a device's **live state**, which may be unrelated to
anything written (write and read can hit different hardware behind one port).
Memory is just the special case where the device — RAM — latches every address.

The `Io` policy reflects this: it's an **event/query** seam with the **full
16-bit port** (`(A<<8)|n` for `IN/OUT (n)`, `BC` for `(C)` forms) — and adopting
it *is* the 16-bit-addressing fix the Spectrum keyboard needs.

```cpp
struct Io {                          // policy contract — events, not a store
    uint8_t In(uint16_t port);                 // ask the device for live state
    void    Out(uint16_t port, uint8_t value); // hand the device a transient write
};
```

Devices (truth):
- **`OpenBusIo`** — the honest **default** for "nothing attached": `Out` is
  discarded; `In` returns the floating-bus value (`0xFF`). No round-trip.
- **`LatchedIo`** — a bank of read/write latches (the old 256-byte array, *named
  for what it is*: one legitimate-but-simplistic device, handy for tests and
  simple IoT models — **not** "how I/O works").
- **`SpectrumIo`** — real machine behaviour: `OUT 0xFE` latches only the
  border/MIC/speaker bits, `IN 0xFE` returns keyboard+EAR, unmapped ports →
  open bus.
- **`GpioIo` / `SerialIo` / …** — drive real hardware or device models.

Observation (debugger), mirroring memory:
- **`ObservableIo<Inner>`** — a **decorator** that forwards `In`/`Out` to the
  real device and **records each transaction** (direction, port, value, cycle)
  for the debugger. It shows a *bus-transaction log*, never a fake "current value
  of each port."

> **Why this matters beyond accuracy:** a real `IN` can have side effects
> (clear a flag, advance a FIFO). So the debugger must **never poll-read ports**
> to display them — it may only observe transactions the program itself makes.
> The old storing-array model silently licensed that wrong behaviour; the device
> model forbids it.

A policy is a thin seam; stateful device logic (e.g. the ULA) lives in a device
object the policy forwards to, so devices stay independently testable and can be
shared across the I/O *and* memory seams (the ULA needs both).

**Default decision (i):** the bare `CPU` defaults to `OpenBusIo` — correctness is
the default; the convenient `LatchedIo` round-trip is opt-in (a few port-poking
tests/examples select it explicitly).

## 7. The debugger over policy configurations

**Decided (a):** the whole stack is parameterized on **one** config, threaded
through consistently. The config is named once as a type alias; everything that
touches the CPU uses it:

```cpp
using AppCpu     = CPUImpl<ObservableMemory, ObservableIo<SpectrumIo>>;  // this build's config
using AppSession = DebugSession<AppCpu>;                                 // Machine<AppCpu> too
```

- `DebugSession` and `Machine` are **templates on the config** (constrained to an
  `ObservableMemory` plug); they are explicitly instantiated for each shipped
  config.
- The **disassembler and symbol table stay non-templated and shared** — they are
  config-agnostic (byte-reader callback / address maps). Only code that touches
  the CPU is parameterized; that *is* the consistent rule, not an exception.
- The **UI binds the alias** (`UiContext`/panels reference `AppSession`), so
  panels aren't templated per-config — the single config choice lives at the
  alias.
- **Consequence:** the integrated app is built as one config — the Spectrum
  config — which also serves generic-Z80 debugging (arbitrary code simply
  doesn't exercise the Spectrum ports). The mass twin is a separate build on
  bare policies (`CPUImpl<FastMemory, FastIo>` etc.), with no debugger or UI.
- (Rejected alternative (b): fixing the debugger to a runtime-routing I/O — it
  would debug a proxy rather than the real `SpectrumIo`, losing fidelity.)

## 8. The performance invariant (sacred)

`CPUImpl<FastMemory, FastIo>` is the **null configuration** and the performance
reference. Every capability must be **opt-in and zero-cost when off**, achieved
by compile-time policy selection — never by runtime flags on the hot path. The
`performance_benchmark` is the guardrail: **a throughput regression there is a
build failure.** This single rule is what stops the twin from eroding as the
debugger and emulators grow.

## 9. Module layout & build targets

```
src/                  core engine            -> z80_cpu        (memory: FastMemory/ObservableMemory; io: OpenBusIo/LatchedIo/ObservableIo)
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
