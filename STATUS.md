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
    bus transactions; `CallbackIo` — generic bridge forwarding `IN`/`OUT` to
    installed handlers (keeps machine-specific port decode out of the core).
    `IN`/`OUT` carry the **full 16-bit port** (A or BC on the high byte).
- **Direction** ([ARCHITECTURE.md](ARCHITECTURE.md)): the CPU foundations
  (policies + interrupt) and the Machine layer (frame clock + `timing.h`) are in
  place. Next is the ULA (`SpectrumIo` ports + keyboard/border + video screen) on
  these foundations — see [SPECTRUM_DESIGN.md](SPECTRUM_DESIGN.md).

## Debugger core (UI-free, unit-tested) — `z80_debugger_core`

- **DebugSession** — owns the execution loop: full-instruction stepping (across
  prefixes), Step-Over (temporary return breakpoint), bounded `RunSlice` with
  inline breakpoints, `RunForTStates` (T-state-budgeted run — the breakpoint-aware
  frame primitive), write-watchpoints + dirty tracking via an ObservableMemory
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

## Machine layer (UI-free, unit-tested) — `z80_machine`

Header-only; templated on the CPU config and decoupled from the debugger via a
stepper callback, so it depends only on `z80_cpu` (a sibling capability to the
debugger; the app composes them).
- **`timing.h`** — the ZX Spectrum 48K PAL time base: the ULA clock tree
  (14 MHz `/2` → 7 MHz pixel `/2` → 3.5 MHz CPU) and the frame/scanline geometry
  derived from it (224 T/line, 312 lines, 69,888 T/frame, display start T=14,336),
  plus `to_master()`/`to_pixels()` for the sub-T-state path.
- **`Device`** — peripheral interface with a per-frame `OnFrame()` lifecycle hook
  (FLASH toggle, audio flush, …); port/memory interaction stays in the I/O and
  memory policies.
- **`Machine<Cpu>`** — drives the CPU in fixed T-state frame quanta: each frame
  asserts the frame interrupt (`CPU::Interrupt`), advances one frame's worth of
  T-states via the caller's stepper (raw speed, or `RunForTStates` for
  breakpoint-aware debugging), carries the per-frame overrun, and ticks devices.
- **`screen.h`** — Spectrum display decode (ported to C++23 `constexpr`/`span`
  from the author's C decoder): `decode_attribute`, per-byte and per-line
  (32 bytes → 256 palette indices) pixel expansion with the FLASH ink/paper swap
  (`flash_phase()`), and the 16-colour `kPalette`/`to_rgb`.
- **`video.h`** — frame assembly: border + 256×192 display into one palette-index
  frame (default 320×256), via `render_scanline`/`render_frame`. The interleaved
  display-memory layout collapses into a per-line base-address calc
  (`bitmap_address`/`attribute_address`). Everything is read through a
  `FrameSource` seam ("what did the beam see here?"), so render fidelity lives in
  the source — the renderer never changed when the beam-accurate source (below)
  replaced the final-memory one.
- **`ula.h` / `keyboard.h` / `spectrum_machine.h`** — the running ZX Spectrum 48K.
  The **ULA** latches the border on `OUT 0xFE` into a frame-T-state-stamped timeline
  (resolved to a per-line colour each frame), reads the **keyboard** on `IN` (the
  8×5 matrix, active-low half-rows selected by the port's high byte) + EAR,
  advances the FLASH phase, and is the renderer's `FrameSource`. `keyboard.h` holds
  the hardware matrix layout (key→row/bit); the viewer maps host keys to it.
  **Beam-accurate screen:** the ULA observes display-file writes (0x4000–0x5AFF)
  with their frame T-state via the `ObservableMemory` write hook, and
  `screen_byte(addr, line)` reconstructs each byte as of the moment the beam
  fetched it for that scanline — so per-scanline attribute/bitmap changes
  (multicolour, raster splits) render correctly (per-scanline; mid-line is the
  remaining refinement). Untouched bytes read straight from RAM. **`SpectrumMachine`** wires
  `CPUImpl<ObservableMemory, CallbackIo>` + ULA + the frame clock: `run_frame()` runs a
  PAL frame (50 Hz INT) and renders to palette indices / RGBA. Boots `spec48.rom`
  to the copyright screen (verified headless by `spectrum_boot_test`).
- **`spectrum` viewer** (`apps/spectrum/`, built with the UI) — boots a ROM and
  shows the live screen (border + display, 3×) in a GLFW/ImGui window, with the
  host **keyboard** wired to the matrix (letters/digits/ENTER/SPACE, Shift→CAPS,
  Ctrl→SYM SHIFT, Backspace→DELETE). Paced to real Spectrum speed via a 50.08 Hz
  fixed timestep (decoupled from the display's vsync; `--turbo` runs uncapped;
  fps shown in the title). `--shot FILE` renders headless to a PPM.
  `spectrum spec48.rom`.

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

`cpu_test`, `observable_memory_test`, `io_policy_test`, `interrupt_test` (IM 0/1/2,
masking, HALT wake, EI deferral), `timing_test` (clock tree + geometry),
`machine_test` (frame budget, device ticks, interrupt-per-frame), `screen_decode_test`
(attribute/byte/line, FLASH swap, palette — incl. compile-time `static_assert`s), `video_test`
(address mapping, border fill, display decode through the FrameSource seam),
`keyboard_test` (matrix layout + active-low half-row IN decode),
`raster_test` (beam-accurate per-scanline display reconstruction),
`spectrum_boot_test` (boots the 48K ROM headlessly and checks the screen drew;
SKIPs when `spec48.rom` is absent), `debug_session_test`
(incl. coverage + SMC + `RunForTStates`), `disassembler_test` (golden + length
sweep + branch targets), `symbol_table_test` (round-trip + forgiving parse +
FindContaining). All green; clean Release build, no warnings.

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
