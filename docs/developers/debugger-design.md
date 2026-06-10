# Z80 Digital Twin Debugger UI — Design Document

**Version:** 2.0
**Date:** June 5, 2026
**Status:** Implemented — the v2.0 design is realized (see [STATUS.md](../reference/status.md)).
Platform/policy model: [Architecture](architecture.md). The debugger/machine
memory plug is now `ObservableMemory`.

> **v2.0 rewrite note.** This document replaces a v1.0 draft whose central
> premise — a CPU-fed EventBus driving runtime-loadable plugins with
> virtualized memory — was mismatched against the actual CPU and against this
> project's defining property (raw execution speed). v2.0 grounds the design in
> the real [z80_cpu.h](../../src/z80_cpu.h) API, makes the **debugger own the
> execution loop**, and scopes the first version to a **core debugger** with
> the plugin system explicitly deferred.

---

## 1. Executive Summary

A **cross-platform ImGui debugger** for the Z80 Digital Twin. The debugger
**owns the execution loop**: it drives the CPU one step at a time, checks
breakpoints inline, and reads CPU state directly each frame to render registers,
disassembly, memory, and I/O. Memory is a **pluggable compile-time policy**: the
production/benchmark build keeps the current fast `std::array` plug (codegen
unchanged, 2 GHz-equivalent performance intact), while the debugger plugs in a
`ObservableMemory` that exposes exact write events via a hook. No event bus, and the
production hot path carries no indirection.

**Key properties**
- **Platforms:** macOS, Linux, Windows (single C++23 codebase).
- **Execution model:** Debugger-driven loop; CPU is a passive steppable engine.
- **Memory model:** Pluggable policy — `CPUImpl<FastMemory>` for production
  (unchanged), `CPUImpl<ObservableMemory>` for the debugger (hooked writes).
- **Observation model:** Registers/memory/I/O read directly each frame; memory
  writes observed via the `ObservableMemory` hook. No EventBus.
- **Scope (v1):** Core debugger. Plugin extensibility is a documented future
  phase (§14), not built now.

---

## 2. Goals and Non-Goals

### Goals (v1 — core debugger)
- Drive the CPU with **full-instruction stepping** and step-over.
- Free-run with **inline breakpoint** checking and a responsive UI.
- Display all Z80 registers, flags, PC/SP, and the alternate set accurately.
- Disassembly view centered on PC, with breakpoint and symbol annotations.
- Memory hex/ASCII viewer with jump-to-address and region coloring.
- I/O port activity view.
- Symbol table loaded from a `.sym` (JSON) file, integrated into disasm/memory.
- 60 FPS UI on a 1080p window.

### Non-Goals (deferred or out of scope)
- **Plugin system, EventBus, runtime-loadable libraries** — deferred to §14.
- **I/O device virtualization** (plugins simulating hardware) — deferred.
- **Per-T-state (sub-instruction) stepping** — requires CPU re-architecture; see
  §4.3.
- Conditional breakpoints and read-watchpoints — future (§8.3). (Memory
  write-watchpoints are feasible via the hook; scoping call — §8.2.)
- GDB remote protocol, time-travel/recording, multi-threaded CPU.
- Re-implementing the Z80 — we use the existing `z80::CPU`.

---

## 3. Constraints Imposed by the Existing CPU

The debugger is built **on top of** [z80_cpu.h](../../src/z80_cpu.h) as it exists. The
following facts drive every design decision below; ignoring them is how the
previous draft went wrong.

### 3.1 `Step()` is per-opcode-byte, not per-instruction
[`CPU::Step()`](../../src/z80_cpu.cpp#L70) consumes **one opcode byte**. For prefixed
instructions it only latches a prefix state and returns:

```
CB 47 (BIT 0,A)   → Step #1: current_state = CB_PREFIX, PC++, +4 T-states
                    Step #2: executes BIT, current_state = NORMAL
DD CB d 06        → Step #1: DD_PREFIX
                    Step #2: DD_CB_PREFIX
                    Step #3: reads displacement + cb opcode internally, executes
```

A single user-facing "Step" must therefore advance until the instruction is
**complete**, i.e. until `current_state == NORMAL`. The CPU does not expose this
today (see §4.2).

### 3.2 No event/observer mechanism exists — so memory becomes a pluggable policy
Memory is a private `std::array<uint8_t, 65536>`; instruction handlers write it
via raw `memory[addr] = ...` in **189 places**
([z80_cpu.cpp:589](../../src/z80_cpu.cpp#L589),
[:852](../../src/z80_cpu.cpp#L852), [:1013](../../src/z80_cpu.cpp#L1013), …). Crucially,
**every one of those is a plain `memory[...]` subscript** — there are no
`.data()`, `&memory`, iterator, `memcpy`, or compound-assignment uses (verified
by grep). So the only contract the CPU requires of "memory" is `operator[]` for
read and write.

That lets us make memory a **compile-time policy**: the CPU is templated on a
`Memory` type (§5). Production plugs the existing fast array; the debugger plugs
a hooked implementation. Because the binding is at instantiation time, **every
plug is fully inlined and the current run is byte-for-byte unchanged** — there is
no runtime indirection and no per-access branch on the production path. See §5
for the templating and §3.2.1 for the debug plug.

#### 3.2.1 The `Memory` policy contract and the observable plug
Any plug must satisfy a minimal contract — read and write by address. This is an
illustrative sketch of the policy shape, not the full current header:

```cpp
// Policy concept (informal): readable/writable by uint16_t address.
struct FastMemory {                                    // production plug == today
    std::array<uint8_t, 65536> data;
    uint8_t  operator[](uint16_t a) const { return data[a]; }
    uint8_t& operator[](uint16_t a)       { return data[a]; }   // direct, inlinable
};

class ObservableMemory {                                    // debugger plug
    std::array<uint8_t, 65536> data;
    WriteHook hook;                                    // void(uint16_t, uint8_t old, uint8_t neu)
public:
    uint8_t operator[](uint16_t a) const { return data[a]; }
    struct Ref {                                       // write-intercepting proxy
        ObservableMemory& m; uint16_t a;
        operator uint8_t() const { return m.data[a]; }
        Ref& operator=(uint8_t v) {
            uint8_t old = m.data[a]; m.data[a] = v;
            if (m.hook) m.hook(a, old, v);             // exact write event
            return *this;
        }
        Ref& operator=(const Ref& r) { return *this = uint8_t(r); }
    };
    Ref operator[](uint16_t a) { return Ref{*this, a}; }
};
```

`ObservableMemory` yields **exact** memory-write events (address, old, new) — used for
change-highlighting (§7.2) and memory write-watchpoints (§8.2). Its speed is
irrelevant: the debugger drives the CPU at interactive rates (and a tuned plug
comfortably sustains 4 MHz realtime with breakpoints). The production
`FastMemory` plug carries none of this — it is the current array.

### 3.3 I/O *does* funnel through accessors
Unlike memory, IN/OUT instructions go through
[`ReadPort`/`WritePort`](../../src/z80_cpu.h#L157) and the block-I/O ops
([z80_cpu.cpp:2393](../../src/z80_cpu.cpp#L2393),
[:2457](../../src/z80_cpu.cpp#L2457)). This is the one clean tap point and is what a
*future* I/O-virtualization layer (§14) would use. For v1 the I/O panel simply
reads `io_ports[]` each frame via `ReadPort`.

### 3.4 State is exposed via non-const reference accessors
Registers are reached through `uint16_t& BC()`, `uint8_t& A()`, etc.
([z80_cpu.h:98-121](../../src/z80_cpu.h#L98-L121)) — there are **no const overloads**.
The debugger owns the `CPU` and holds it by non-const reference, so reading
state works; we simply will not pretend panels operate on a `const CPU&`.
`Reset()`, `IsHalted()`, `GetCycleCount()`, `ReadMemory()`, `ReadPort()` are
already available and sufficient for read-out.

### 3.5 `RunUntilCycle` runs to completion
[`RunUntilCycle`](../../src/z80_cpu.cpp#L64) loops `Step()` until a target cycle or
HALT, synchronously. Calling it directly from the render thread would freeze the
UI for the duration. The debugger uses **bounded run-slices** instead (§4.1).

---

## 4. Execution Model — Debugger Owns the Loop

This is the central decision. The CPU never runs free under the debugger; the
**debugger advances it** and decides when to stop and render.

### 4.1 The frame loop

```
EVERY FRAME (target ~16 ms):
  1. Poll ImGui input.
  2. Dispatch debug commands:
       Step      → StepOneInstruction()
       Step Over → run until PC == return_addr or breakpoint (see §4.4)
       Run       → RunSlice(budget)   // bounded; see below
       Pause     → mode = PAUSED
       Reset     → cpu.Reset()
  3. Render core panels by reading CPU state directly.
  4. ImGui::Render(); swap buffers (vsync).
```

**`RunSlice(budget)`** — the heart of responsive free-run:

```
mode = RUNNING
remaining = budget                 // e.g. a T-state or instruction count tuned
                                   // to keep the frame ≤ ~10 ms
while (remaining-- > 0 && !cpu.IsHalted() && mode == RUNNING) {
    if (breakpoints.contains(cpu.PC())) {   // inline check, O(1)
        mode = PAUSED; break;
    }
    StepOneInstruction();
}
```

The breakpoint check is a hash-set lookup on PC **inside** the loop — never via
a per-frame event fan-out. At an instruction budget of a few hundred thousand
per slice, a tight loop still runs a large program in a handful of frames while
the UI stays interactive and breakpoints fire promptly.

### 4.2 `StepOneInstruction()` — completing a full instruction
Because `Step()` is per-byte (§3.1), the unit step is:

```
do {
    cpu.Step();
} while (!cpu.InstructionComplete());   // see required CPU change §4.2
```

This requires the CPU to tell us when it is at an instruction boundary. The
minimal change is a one-line read accessor (§5). Until then, instruction
stepping cannot be implemented correctly — this is a hard dependency.

### 4.3 Granularity: instruction-level (agreed)
The debugger steps at **instruction granularity** — the natural unit for the
current CPU, where `Step()` executes a whole instruction's worth of T-states
atomically. Each step *reports* its T-state delta (`GetCycleCount()`
before/after) so cycle cost is still visible. Sub-instruction (per-T-state)
stepping would require re-architecting the CPU into a cycle-stepped state
machine; it is explicitly out of scope and noted in §14 as a CPU-core project,
not a debugger feature.

### 4.4 Step Over
"Step Over" must skip subroutine bodies entered by `CALL`. Approach: peek the
opcode at PC; if it is a `CALL`/`RST` variant, record `return_addr = PC +
instr_len`, set a **temporary internal breakpoint** there, and `RunSlice` until
hit; otherwise behave as Step. This reuses the disassembler's length decoding
(§6) and the breakpoint machinery (§8).

---

## 5. Required CPU Changes (Minimal, Explicit)

Two kinds of change: small **additive accessors**, and one **structural** change
— making memory a template policy. Neither alters the production codegen.

### 5.1 Additive accessors (inline, trivial)
| Change | Why |
|--------|-----|
| `bool InstructionComplete() const { return current_state == NORMAL; }` | Correct full-instruction stepping (§4.2) |
| `uint8_t InterruptMode() const` | Display IM 0/1/2 in registers panel |
| `bool IFF1() const` / `IFF2() const` const overloads (optional) | Read-only flag display |

Everything else the debugger reads — register refs, `ReadMemory`, `ReadPort`,
`GetCycleCount`, `IsHalted`, `Reset`, `LoadProgram`, `Step` — already exists.

### 5.2 Memory as a template policy
The CPU class is parameterized on its `Memory` plug, and the existing name is
preserved by an alias so **all current code, examples, and the benchmark compile
and run unchanged**:

```cpp
template <class Memory = FastMemory>
class CPUImpl {
    Memory memory;                 // the plug (§3.2.1)
    std::array<uint8_t, 256> io_ports;
    // ... all existing state and the InstructionHandler table, now
    //     std::array<void (CPUImpl::*)(), 256> ...
};

using CPU      = CPUImpl<FastMemory>;    // production / benchmark — identical codegen
using DebugCPU = CPUImpl<ObservableMemory>;   // debugger — hooked writes
```

**Refactor shape (mechanical, repo-wide):**
- The 3000-line [z80_cpu.cpp](../../src/z80_cpu.cpp) keeps its structure; each method
  definition gains a `template <class Memory>` prefix and `CPUImpl<Memory>::`
  qualifier.
- Keep definitions in the `.cpp` via **explicit instantiation** at the bottom
  (`template class CPUImpl<FastMemory>; template class CPUImpl<ObservableMemory>;`) so
  compile times and the file layout stay close to today — no need to move
  everything into headers.
- The instruction dispatch tables become member-pointer arrays of
  `CPUImpl<Memory>`; otherwise unchanged.

**Why this satisfies "don't affect the current run":** `CPUImpl<FastMemory>`
inlines `memory[a]` to the same direct `std::array` access as today. The
template is resolved at compile time, so there is no virtual dispatch and no
per-access branch anywhere on the production path. The numbers in
[design_decisions.md](../archive/early-design-decisions.md) hold by construction.

> **Note (I/O):** `io_ports` is left as a plain array in v1 — I/O already funnels
> through `ReadPort`/`WritePort` (§3.3), so a future I/O policy can be added the
> same way without touching call sites. Not templated now to keep the v1
> refactor focused on memory.

---

## 6. Disassembler

A **standalone, stateless** byte decoder. It decodes prefixes from the bytes at
the target address — it does **not** read the live CPU `current_state` (the
previous draft contradicted itself here; disassembly of arbitrary addresses must
be independent of what the CPU is mid-fetching).

**API**
```
struct Instruction {
    uint16_t address;
    uint8_t  length;                 // 1..4
    std::string mnemonic;            // "LD", "ADD", ...
    std::string operands;            // "A, 0x42" (symbol-substituted if known)
    std::string text;                // full rendered line
};

Instruction Decode(std::span<const uint8_t> mem, uint16_t address,
                   const SymbolTable& symbols) const;
```

- Handles all prefix combinations: CB, DD, ED, FD, DD CB, FD CB.
- Reads bytes via the passed span (`cpu.ReadMemory` wrapped by the panel), so it
  decodes ROM, RAM, anything.
- Substitutes symbol names into operands when the target address resolves.

**Maintenance note.** The CPU already encodes the full opcode→handler mapping
with named functions (`LD_BC_nn`, etc.). The disassembler necessarily restates
opcode knowledge in table form; the two must be kept consistent. Mitigation:
a unit test that disassembles every opcode and checks length/mnemonic against a
fixed golden table, plus spot-checks against a reference disassembler.

---

## 7. State Observation and Consistency

### 7.1 Single source of truth
CPU registers, memory, and I/O live solely in the CPU. Panels derive everything
from it each frame. Memory-write notifications come from the `ObservableMemory` plug's
hook (§3.2.1), not from polling.

### 7.2 Memory change detection (via the ObservableMemory hook)
The debugger installs a write hook on the `ObservableMemory` plug. Each `memory[a] =
v` fires `hook(a, old, v)`, which the debug session records into a per-frame
"recently written" set; the memory viewer highlights those cells and clears the
set after rendering. This gives **exact** change detection (no false negatives,
no scanning) at no cost on the production path — that path uses `FastMemory`,
which has no hook. The same hook feeds memory write-watchpoints (§8.2).

---

## 8. Breakpoint System

### 8.1 v1: address breakpoints
```
struct Breakpoint { uint16_t address; bool enabled; uint64_t hit_count; bool temporary; };
```
Stored in a hash set keyed by address for O(1) lookup in the run loop (§4.1).
`temporary` supports Step-Over's internal breakpoint (§4.4).

**Operations:** `Add/Remove/Toggle/Clear/List`. Hits are surfaced by the loop
setting `mode = PAUSED` and recording the hit address — not via an event bus.

### 8.2 Memory write-watchpoints (v1-feasible via the hook)
Because the `ObservableMemory` plug delivers exact write events (§3.2.1), watchpoints
are cheap to add: the hook checks the written address against a watch set and
pauses the run when matched. Whether this lands in v1 or just-after is a scoping
call, but the mechanism is already present — it is no longer blocked on
infrastructure.

### 8.3 Future
Conditional breakpoints (`A == 0x42`, `HL > 0x5000`) and read-watchpoints —
out of scope for v1 (read interception would extend the `Memory` policy's read
path).

---

## 9. Symbol Table

```
struct Symbol { uint16_t address; std::string name; SymbolType type; std::string description; uint16_t size; };
enum class SymbolType { JUMP_TARGET, VARIABLE, DATA_REGION, FUNCTION };
```

- Bidirectional hash maps: address→symbol and name→address.
- JSON `.sym` file (schema unchanged from v1 draft — that part was fine).
- API: `DefineLabel`, `Lookup(addr)`, `Resolve(name)`, `LoadFromFile`,
  `SaveToFile`, `List`.
- Missing/invalid symbol file is **non-fatal** — load what parses, warn on rest.

---

## 10. UI Architecture

### 10.1 Layout
ImGui docking; main dockspace with core windows; layout persisted to
`imgui.ini`. GLFW + OpenGL 3.3 backend (portable across all three OSes; avoids
the Metal/legacy-GL branching the v1 draft introduced unnecessarily).

### 10.2 Core panels
- **Registers** — AF, BC, DE, HL and primes, IX, IY, PC, SP, IR, WZ; individual
  flag bits (S Z H P/V N C); hex with decimal/binary toggle; IM and IFF state.
  Read directly each frame.
- **Disassembly** — window centered on PC; columns Address | Bytes | Mnemonic |
  Operands; current PC highlighted; breakpoint dots; inline symbol labels;
  click address to set/clear breakpoint. Renders only visible lines.
- **Memory** — 16-byte rows, hex + ASCII; jump-to-address; region coloring
  (ROM/RAM/stack); changed-cell highlight via §7.2.
- **I/O Ports** — 256 ports, value (hex/dec); reads `ReadPort` each frame.
- **Control** — Step / Step Over / Run / Pause / Reset; status bar with cycle
  count, halted flag, PC, and measured host throughput.

---

## 11. Build and Platform Support

- **CMake 3.20+**, **C++23** (matches the core; GCC 13+/Clang 16+/MSVC 2022+).
- **Dependencies:** ImGui (vendored), GLFW3, OpenGL 3.3. No libsndfile or audio
  deps in v1 (those belonged to deferred plugins).
- **Targets:**
  - `z80_cpu` — the core, now templated on the `Memory` policy (§5.2) with
    `using CPU = CPUImpl<FastMemory>` preserving existing usage; explicit
    instantiation keeps it a normal compiled unit.
  - `z80_debugger` — the debugger executable; instantiates `CPUImpl<ObservableMemory>`
    and links the core directly (no shared-library boundary in v1).
  - `z80_debugger_tests` — disassembler golden tests, breakpoint/step logic, and
    a parity test that `FastMemory` and `ObservableMemory` runs produce identical CPU
    state for the same program.

The debugger links the CPU core as a normal static dependency. The "core as
shared library + plugin .so/.dylib/.dll" packaging from the v1 draft is deferred
with the plugin system (§14).

---

## 12. Performance

- **UI:** 60 FPS at 1080p. ImGui immediate mode is ample; only visible disasm
  lines and the visible memory window are rendered/diffed.
- **Execution:** Free-run uses bounded slices (§4.1). The inline breakpoint
  check is a single hash-set lookup per instruction. The debug build's per-write
  hook (`ObservableMemory`) is fine at interactive/4 MHz-realtime rates; the
  production build (`FastMemory`) carries no hook and matches the original
  benchmark by construction (§5.2).
- **Tuning knob:** the per-frame instruction budget trades responsiveness
  against throughput; default chosen to keep a frame under ~10 ms on the dev
  machine, adjustable at runtime.

---

## 13. Error Handling and Diagnostics

- **Symbols:** malformed entries skipped with a warning; missing file is
  non-fatal.
- **Breakpoints on odd/invalid addresses:** allowed (any address is a valid PC
  target); no special handling needed.
- **Logging:** simple leveled logger (ERROR/WARN/INFO) to stderr; optional file
  sink. No plugin-crash isolation needed in v1 (no plugins).

---

## 14. Future Extensions (Explicitly Deferred)

These were the v1 draft's main body; they are real future work but must not gate
the core debugger.

- **Plugin system + EventBus.** Plugins (bitmap viewer, audio, keyboard
  injection, profilers) would consume CPU events. The infrastructure is already
  the right shape: the `ObservableMemory` plug's write hook (§3.2.1) is the natural
  event source, and a future `Memory` plug can fan out to subscribers instead of
  a single hook. No hot-path instrumentation of the production build is ever
  needed — plugins live entirely in the `CPUImpl<ObservableMemory>` world.
- **I/O device virtualization.** Add an `IO` policy alongside the `Memory`
  policy (§5.2 note), or route `ReadPort`/`WritePort` (§3.3) to handlers —
  feasible and isolated, unneeded until a plugin hosts a device.
- **Runtime-loadable plugins** (.so/.dylib/.dll) and shared-library packaging.
- **Per-T-state stepping** — CPU-core re-architecture (§4.3).
- **Conditional breakpoints and read-watchpoints** — extend the breakpoint
  evaluator / `Memory` read path (§8.3).
- **Time-travel / recording, GDB protocol, multi-threaded CPU.**

---

## 15. File Structure

```
src/
├── z80_cpu.h          # now CPUImpl<Memory>; `using CPU = CPUImpl<FastMemory>`
├── z80_cpu.cpp        # templated defs + explicit instantiation (§5.2)
└── memory/
    ├── fast_memory.h  # production plug (the current std::array)
    └── memory/observable_memory.h # hooked memory plug for debugger/machine use

debugger/
├── disasm/        disassembler.{h,cpp}, opcode tables
├── symbols/       symbol_table.{h,cpp}
├── breakpoints/   breakpoint_manager.{h,cpp}
├── exec/          debug_session.{h,cpp}   # owns the loop, run-slice, stepping
├── ui/
│   ├── debugger_ui.{h,cpp}                # frame loop, command dispatch
│   ├── panels/    registers, disasm, memory, io, control
│   └── theme.{h,cpp}
└── main.cpp
```

`exec/debug_session` is the new core abstraction the v1 draft lacked: it holds
the `DebugCPU&`, installs the `ObservableMemory` write hook, owns the breakpoint set
and run mode, and exposes `StepOneInstruction / StepOver / RunSlice / Pause /
Reset`. UI calls into it; panels read CPU state through it.

---

## 16. Success Criteria

### Core debugger (v1)
- [ ] Builds and runs on macOS, Linux, Windows from one source.
- [ ] **Full-instruction** step advances PC correctly across prefixed
      instructions (CB/DD/ED/FD/DD CB/FD CB) — verified against known programs.
- [ ] Step Over skips CALL bodies and stops at the return.
- [ ] Run is responsive (UI stays interactive) and stops promptly on breakpoints
      and HALT.
- [ ] Registers panel shows all registers, flags, IM/IFF accurately.
- [ ] Disassembly centers on PC with breakpoint + symbol annotations.
- [ ] Memory viewer: hex/ASCII, jump-to-address, changed-cell highlight.
- [ ] Symbol table loads from `.sym`; bad entries are non-fatal.
- [ ] **Benchmark unchanged:** `CPUImpl<FastMemory>` free-run performance equals
      pre-refactor numbers (compile-time policy, no production indirection).
- [ ] **Plug parity:** `FastMemory` and `ObservableMemory` runs produce identical CPU
      state for the same program (validates the templating refactor).

---

## 17. Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|-----------|
| Disassembler diverges from CPU's real decoding | Medium | Golden-table unit test over all opcodes; reference cross-check |
| Run-slice budget mis-tuned (laggy UI or slow run) | Low | Runtime-adjustable budget; measure frame time |
| Per-T-state stepping later requested | Low | Out of scope by agreement; instruction-level stepping with T-state reporting is the v1 contract (§4.3) |
| Required CPU accessors rejected/forgotten | High | §5.1 is a hard, minimal, additive dependency — land it first |
| Templating the CPU introduces regressions or bloats compile time | Medium | Explicit instantiation (not header-only); plug-parity test (§16); land the refactor as its own step with the existing test suite green before any debugger code |
| ImGui API churn | Medium | Pin a specific ImGui version (vendored) |

---

**End of Design Document (v2.0)**
