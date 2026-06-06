# Project Status — Where We Are

**As of:** 2026-06-06
**Branch of record:** `main`

A snapshot of what's built and working, taken before starting the simulated-
hardware (Spectrum ULA) work. For the forward-looking reverse-engineering vision
see [DEBUGGER_ROADMAP.md](DEBUGGER_ROADMAP.md); for the debugger architecture see
[DEBUGGER_DESIGN.md](DEBUGGER_DESIGN.md).

---

## Core engine

- **Z80 CPU** — full instruction set incl. CB/ED/DD/FD/DDCB/FDCB; cycle-accurate
  T-state counting; maskable-interrupt injection (`Interrupt()`: IM 0/1/2, IFF
  handling, HALT wake, EI one-instruction deferral).
  ([src/z80_cpu.h](src/z80_cpu.h), [src/z80_cpu.cpp](src/z80_cpu.cpp))
- **Pluggable memory + I/O (compile-time policies)** — `CPUImpl<Memory, Io>`
  with `using CPU = CPUImpl<FastMemory, OpenBusIo>`:
  - `FastMemory` — zero-overhead production/benchmark plug (~2 GHz-equivalent).
  - `ObservableMemory` — write-intercepting `operator[]` proxy fanning exact
    `(address, old, new)` events to a list of observers (debugger + future
    devices can attach at once); reads stay cheap.
  - `OpenBusIo` — honest default (`Out` discarded, `In` → `0xFF`); `LatchedIo` —
    256 read/write latches (opt-in); `ObservableIo<Inner>` — decorator that logs
    bus transactions. `IN`/`OUT` carry the **full 16-bit port** (A or BC on the
    high byte).
- **Direction** ([ARCHITECTURE.md](ARCHITECTURE.md)): the CPU foundations are in
  place (policies + interrupt). Next is the Device/Machine layer + PAL frame
  clock, then the ULA (`SpectrumIo` + video) — see
  [SPECTRUM_DESIGN.md](SPECTRUM_DESIGN.md).

## Debugger core (UI-free, unit-tested) — `z80_debugger_core`

- **DebugSession** — owns the execution loop: full-instruction stepping (across
  prefixes), Step-Over (temporary return breakpoint), bounded `RunSlice` with
  inline breakpoints, write-watchpoints + dirty tracking via an ObservableMemory
  observer, Run/Pause/Reset.
- **Disassembler** — stateless octal decoder, all prefixes, faithful IX/IY
  semantics; exposes length, mnemonic, operands, `symbols_used`, and
  `branch_target`; symbol resolution decoupled behind an injected resolver.
- **SymbolTable** — bidirectional address↔name maps, typed symbols
  (FUNCTION/LABEL/JUMP_TARGET/BYTE_VARIABLE/WORD_VARIABLE/DATA_REGION),
  `FindContaining` range lookup, dependency-free JSON `.sym` load/save.
- **Execution coverage (L1)** — records each executed instruction's byte span
  (decode-on-first-execution, amortized ~free); per-address flags + coverage %.
- **Self-modifying-code detection (L2)** — write-to-executed-code raises an event
  `{addr, old, new, writer_pc, cycle}`; optional Break-on-SMC. The "before" byte
  is free from the hook (no instruction trapping).

## Debugger UI — `z80_debugger` (ImGui + GLFW + OpenGL via FetchContent)

Modular panels over a shared `UiContext`; each panel is a `Panel` subclass.
- **Control** — Step / Step Over / Run / Pause / Reset; status incl. coverage %
  and SMC count.
- **Registers** — all registers + alternates, editable when paused; flags, IM/IFF.
- **Disassembly** — Follow-PC, current-instruction highlight, breakpoint gutter
  (`@`, click to add/remove), label column + colour-coded symbols, hover
  tooltips, go-to (hex **or** symbol) with back/forward history, right-click
  "Go to target", coverage tint (green=executed, red=self-modified).
- **Memory** — hex/ASCII, jump-to-address, change highlight, per-byte hover
  tooltip (which symbol/region), coverage tint (green/magenta/red), right-click
  to label an address.
- **I/O Bus** — passive bus-transaction log (OUT/IN with the full port); never
  polls ports (a real `IN` can have side effects).
- **Self-Modifying Code** — event log (writer/target/`old→new`/cycle),
  click-to-jump, count, Break-on-SMC.
- Symbol editor popup (create/edit/remove; byte/word/label/function/jump-target)
  + File ▸ Save Symbols.

## Examples / data

- `examples/gcd.sym`, `examples/spectrum48k.sym` (full ZX system-variable table).
- Built-in demos: `--demo gcd` (default), `--demo smc` (self-modifying loop).
- CLI: `[prog.bin] [--org A] [--sym f] [--bp HEX] [--demo gcd|smc] [--run N]
  [--smoke] [--shot FILE]`.

## Tests (headless, run on every build)

`cpu_test`, `debug_memory_test`, `debug_session_test` (incl. coverage + SMC),
`disassembler_test` (golden + length sweep + branch targets), `symbol_table_test`
(round-trip + forgiving parse + FindContaining). All green; clean Release build,
no warnings.

## Verification caveat

Core/RE logic is unit-tested headlessly. UI panels are verified by build +
`--smoke`/`--shot` offscreen screenshots; interactive paths (clicks, hovers,
popups) are confirmed visually rather than automated.

---

## What's next

- **Designed, ready to implement:** the Spectrum ULA (screen via full-frame
  redraw → per-scanline timing, border, keyboard, EAR/MIC/speaker, 50 Hz
  interrupt). See [ARCHITECTURE.md](ARCHITECTURE.md) and
  [SPECTRUM_DESIGN.md](SPECTRUM_DESIGN.md). Foundations land first: multi-observer
  `ObservableMemory` → I/O compile-time policy → CPU interrupt injection.
- **Later (roadmap):** L3 annotation knowledge base (comments, data typing,
  xrefs) → L4 listing → L5 reassemblable export → L6 round-trip verify
  ([DEBUGGER_ROADMAP.md](DEBUGGER_ROADMAP.md)).
