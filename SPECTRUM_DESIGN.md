# ZX Spectrum Hardware (ULA) — Design

**Status:** DRAFT / in progress — design only, **not yet implemented**.
**Date:** 2026-06-06
**Relationship:** the Spectrum is the first *machine* capability in the platform
([ARCHITECTURE.md](ARCHITECTURE.md)); it runs over the same CPU as the
debugger ([DEBUGGER_DESIGN.md](DEBUGGER_DESIGN.md)) and is debuggable while
running. Current state: [STATUS.md](STATUS.md).

---

## 1. Goal & first milestone

Turn the digital twin into a runnable ZX Spectrum 48K — boot the ROM, show a
real display — while every debugger capability (breakpoints, coverage, SMC)
keeps working on the *running* machine.

**Milestone 1 (this design's scope):**
- CPU prerequisites (§3): **maskable-interrupt injection** and the **I/O
  compile-time policy** (which also delivers 16-bit port addressing).
- A **device abstraction** and a **Machine** running the CPU on the PAL frame
  clock (§5–6).
- **ULA video** by **full-frame redraw** (§7): bitmap + attributes → texture.
- **Border** (`OUT 0xFE`) and the **50 Hz frame interrupt**.

Enough to boot the 48K ROM to its copyright screen. **Keyboard, beeper audio,
tape, and cycle-accurate timing/contention are deferred** (§9).

## 2. Decisions made (with the user)

1. **One integrated app** — the debugger gains a Screen panel and a "run as
   Spectrum" mode over the same CPU; you debug a live Spectrum.
2. **I/O is a compile-time policy** (`CPUImpl<Memory, Io>`, ARCHITECTURE §6) —
   not a runtime hook. This gives zero-cost per-deployment peripherals (ULA,
   Kempston, GPIO, serial…) and *is* the 16-bit-port fix.
3. **Full-frame redraw, not dirty-tracking** — every scanline is cheap to draw
   in the time available, so we redraw the whole display each frame and skip the
   dirty bookkeeping. The path is structured to evolve into per-scanline-at-
   T-state rendering for cycle accuracy (§7).
4. **Model PAL/ULA timing closely** (§6), including beam-return (retrace) times,
   toward eventual cycle accuracy.
5. **Milestone 1 = prerequisites + screen + border + 50 Hz INT** (keyboard/sound
   next).

## 3. CPU prerequisites (hard blockers)

Verified gaps; additive changes to the core, each landed *first* with headless
unit tests, and **without regressing the `FastMemory+OpenBusIo` benchmark**
(ARCHITECTURE §8).

### 3.1 Maskable-interrupt injection
The CPU has `EI`/`DI`/`IM 0/1/2`/`RETI` but **no way to raise an interrupt**.

```cpp
// Request a maskable interrupt (the ULA calls this once per frame).
// If IFF1: push PC, clear IFF1/IFF2, and per interrupt mode:
//   IM 0: execute the bus instruction (Spectrum bus = 0xFF -> RST 38)
//   IM 1: PC = 0x0038
//   IM 2: vector = (I << 8) | bus; PC = mem[vector] | (mem[vector+1] << 8)
// HALT is woken (PC steps past it). Adds ~13 (IM0/1) / ~19 (IM2) T-states.
bool Interrupt(uint8_t bus = 0xFF);
void NonMaskableInterrupt();   // NMI -> 0x0066 (later; not needed for the ULA)
```

Honor: **`EI` defers** acceptance until after the following instruction;
**HALT wake**. Tests: IM1 + `IFF1` pushes PC and jumps to `0x0038`; ignored when
`IFF1=0`; HALT resumes on INT.

### 3.2 I/O compile-time policy (devices, not storage)
`IN A,(n)` / `IN r,(C)` currently drop the **high** address byte (needed by the
keyboard) *and* model ports as a stored array — which is a fiction (ARCHITECTURE
§6): real `OUT` is a transient pulse, `IN` reads a device's live state, and the
two can hit different hardware. I/O becomes the CPU's second **compile-time
policy**, an **event/query** seam:

```cpp
template <class Memory = FastMemory, class Io = OpenBusIo> class CPUImpl { … };
// Io contract: uint8_t In(uint16_t port); void Out(uint16_t port, uint8_t);
```

- IN/OUT compute the **full 16-bit port** (`(A<<8)|n`, or `BC`) and call
  `io_.In/Out` — adopting the policy *is* the 16-bit-addressing fix.
- **Default `OpenBusIo`** (honest "nothing attached": `Out` discarded, `In` →
  `0xFF`). `LatchedIo` (the old array) is an opt-in device for tests/simple
  peripherals. `SpectrumIo` routes `0xFE`→ULA (later Kempston, …) with real
  read≠write asymmetry. `ObservableIo<Inner>` (debug decorator) records bus
  transactions.
- Replace the raw `ReadPort/WritePort(uint8_t)` array API with `Io& GetIo()`
  device access (mirrors `GetMemory()`); `LatchedIo` offers explicit peek/poke
  for tests. **Back-compat (decision i):** the few port-poking tests/examples
  switch to `LatchedIo`.
- **Debugger consequence:** `DebugSession` is templated on the CPU config
  (ARCHITECTURE §7); disassembler/symbols are already config-agnostic. The I/O
  *panel* is rewritten as a passive **bus-transaction log** (it must never
  poll-read ports — real `IN` can have side effects).

This is a memory-policy-sized refactor; sequence it like that one (§11).

## 4. Memory observation (debugger capability)

`ObservableMemory` (the multi-observer evolution of `DebugMemory`,
ARCHITECTURE §5) powers the debugger's dirty/SMC/watch features. **Video does
not use it** — milestone-1 rendering reads screen RAM directly each frame (§7),
and the future cycle-accurate ULA reads memory *as the CPU executes*. So the ULA
does not subscribe as a memory observer; the observer list stays a debugger
concern. (A running Spectrum is still fully debuggable because it uses
`ObservableMemory`.)

## 5. Device abstraction & Machine

```
SpectrumCPU = CPUImpl<ObservableMemory, SpectrumIo>               // runnable
SpectrumDbg = CPUImpl<ObservableMemory, ObservableIo<SpectrumIo>> // in the debugger
Machine     = SpectrumCPU + devices(ULA, …) + PAL frame clock
Device:  port In/Out (via the Io policy)  ·  tick/frame hooks (INT, FLASH)  ·
         device state (render source, input sink)
```

- The **ULA** is the first device; `SpectrumIo` forwards its ports to it, and the
  Machine ticks it on the PAL clock (§6). The ULA holds the border colour, the
  (later) keyboard matrix, beeper state, and drives the frame interrupt.
- **Run loop:** the app's "run as Spectrum" mode drives the Machine through
  `DebugSession`, so breakpoints/coverage/SMC apply to the running machine. The
  Machine runs the CPU across a frame's T-states (honoring breakpoints — it can
  stop mid-frame), raises the INT at the frame boundary, then renders + polls
  input. Reconciling the T-state frame budget with `DebugSession`'s
  instruction-stepping is the main integration detail (likely a
  `RunFrame(tstate_budget)` reusing the inline breakpoint check).

## 6. PAL TV / ULA timing model

Verified against the World of Spectrum 48K reference (numbers below) and Chris
Smith, *The ZX Spectrum ULA* (the canonical hardware reference; consult it for
the few sub-details flagged at implementation time).

**Clock tree (Smith):** a single **14 MHz** master clock divides down —
`÷2` → **7 MHz pixel (dot) clock** → `÷2` → **3.5 MHz CPU clock**. Equivalently
**1 T-state = 4 master cycles**, **1 pixel = 2 master cycles**, **2 pixels per
T-state**. Every timing below derives from this ladder rather than being asserted:
- **69,888 T-states/frame** (= 224 × 312) ⇒ **50.08 Hz** (3.5 MHz / 69,888).
- **224 T-states/scanline**; **312 scanlines/frame**.

**Scanline (224 T):** `128` display (256 px at **2 px/T-state**) · `24` right
border · `48` horizontal retrace (beam flyback/blank) · `24` left border.

**Frame (312 lines):** `64` top border (incl. vertical retrace/sync) · `192`
display · `56` bottom border.

**Anchors:**
- First display pixel at **T = 14,336** (= 64 lines × 224) after the interrupt.
- `/INT` is asserted at the frame boundary and held low ~**32 T-states**
  (issue/ULA-dependent — verify against Chris Smith before relying on the exact
  width). We deliver one INT per frame at the boundary.

**Beam-return times** are simply the non-display T-states and are already inside
the 69,888 budget: horizontal retrace (48 T/line) returns the beam to the next
line's left edge; the bottom-border + top-border lines (56 + 64) plus their sync
cover the vertical flyback to the top. No pixels are produced during them (border
colour is shown, or blanking during sync).

**Interlace — "first vs second scan":** the 48K is **non-interlaced**. It emits
exactly **312 lines per field with no half-line offset**, so every field is
identical — there is **no difference between successive scans** from the TV's
view. (Broadcast PAL is 625 lines as two interlaced fields of 312.5 lines; the
Spectrum deliberately omits the half-line, which is why it shows a stable
non-interlaced picture.) Our model uses a single repeating 312-line frame.

### 6.1 Time base — the master clock is the ruler, not the step

The 14 MHz master clock is the single source of truth for time, but we **do not
step the CPU at 14 MHz** — that would quadruple the core's innermost loop and
break the mass-twin performance invariant (ARCHITECTURE §8) for no benefit.
Instead:

- The **CPU stays a T-state counter** (`t_cycle`); that delta is the bridge.
- The **Machine/ULA own the master-derived timeline**; the ×4 / ×2 ratios are
  conversions used where needed, not a per-cycle obligation on the core.
- A single **timing module** (`timing.h`, added with the Machine) encodes the
  ladder once — master/cpu/pixel rates, `T_PER_LINE`, `LINES`, `T_PER_FRAME`,
  `DISPLAY_START_T`, `master_per_T = 4`, `pixels_per_T = 2` — so there are no
  scattered magic numbers, plus `to_master()` / `to_pixels()` for the rare
  sub-T-state path.

**Granularity each feature needs** (model only as deep as required):

| Feature | Granularity |
|---|---|
| Frame/scanline timing, 50 Hz INT | T-state (have it) |
| Memory contention (6,5,4,3,2,1,0,0) | T-state, *aligned* to `DISPLAY_START_T` — the delays are whole T-states; the master clock only fixes the alignment |
| Raster effects (mid-frame border/attribute, multicolour) | T-state (per-scanline / per-byte position) |
| Floating-bus "snow", mid-byte border change | pixel/master — the only true sub-T-state cases |

So: work in **T-states** (CPU-aligned, and integer-exact since the divisors are
powers of two), keep the clock ladder authoritative, and subdivide to
pixel/master **inside the ULA, only when a dot-precise effect demands it**.

## 7. ULA video rendering

- **Layout:** bitmap `0x4000–0x57FF` (6144 B, the interleaved 256×192 order);
  attributes `0x5800–0x5AFF` (768 B, 32×24 cells, `FLASH BRIGHT PAPER₃ INK₃`);
  16-colour palette (8 × bright).
- **Milestone 1 — full-frame redraw.** At the frame boundary, decode all 192
  display lines from current screen RAM into an RGBA buffer, fill the border,
  upload one GL texture, draw it in the **Screen panel**. ~49,152 px is trivial
  at 50 Hz, so no dirty-tracking is warranted.
- **Future — per-scanline at its T-state.** As the CPU executes, the ULA draws
  each scanline from memory at the moment the beam reaches it. Raster effects,
  mid-frame attribute/border changes, and memory contention then emerge for free.
  The scanline is the unit that carries us there; full-frame redraw is the
  stepping stone, and the decoder's per-line entry (`unpack_line`) fits both.
- **Border:** per-frame solid now → per-scanline colour later (`OUT 0xFE` can
  change it mid-frame).
- **FLASH:** swap ink/paper every 16 frames (~0.32 s); with full redraw this is
  just a phase flag passed to the decoder.
- **Decoder:** reuse the user's existing decoder (analysis done separately);
  port it read-only, scanline-oriented, with a flash-phase argument.

## 8. I/O map (Spectrum 48K, relevant bits) — implemented by `SpectrumIo`

`SpectrumIo` is a faithful device: **write and read of `0xFE` touch different
state**, and unconnected bits / unmapped ports read as open bus (`0xFF`).

- `OUT (0xFE)`: latches bits 0–2 **border**, bit 3 **MIC**, bit 4 **speaker**
  (beeper). Other bits are not connected — written and forgotten.
- `IN (0xFE)`: returns bits 0–4 = the five keys of the half-row(s) selected by the
  **high** address byte (0 = pressed); bit 6 = **EAR** (tape in). It does **not**
  return anything you `OUT`.
- The ULA responds to all even ports (A0 = 0); `0xFE` is canonical.

## 9. Deferred (later milestones)

- **Keyboard** (M2) — 8×5 matrix; host-key mapping (incl. CAPS/SYMBOL SHIFT).
- **Beeper audio** (M3) — `OUT 0xFE` bit 4 → square wave from toggle timestamps;
  audio backend choice open (miniaudio / SDL / PortAudio, or WAV first).
- **Tape (EAR in)** — load `.tap`/`.tzx` via bit 6 of `IN 0xFE`.
- **Cycle-accurate timing + memory/IO contention** — per-scanline rendering (§7)
  plus ULA contention of CPU access to `0x4000–0x7FFF` during display fetch.
- **128K paging** — bank-aware memory model.

## 10. Open questions / pending

- **Screen decoder** — final port/interface (logic reviewed; awaiting the file).
- **`/INT` hold width** and exact within-line phase / contention pattern — verify
  against Chris Smith's ULA book when timing accuracy is implemented.
- **Audio backend** — choose at the beeper milestone.

## 11. Next steps (ordered)

1. **`ObservableMemory`** — make the write hook multi-observer; migrate
   `DebugSession`; unit test (two observers both fire). *(headless)*
2. **I/O policy** — add the `Io` template param (default `OpenBusIo`); provide
   `LatchedIo` (old array, opt-in) and `ObservableIo<Inner>` (debug decorator);
   add `SpectrumIo` stub; replace `ReadPort/WritePort` with `GetIo()`; retarget
   `DebugSession` (decision a); redo explicit instantiations; rewrite the I/O
   panel as a passive transaction log. Tests: 16-bit port reaches the device,
   `OpenBusIo` reads `0xFF`, `LatchedIo` round-trips, **benchmark unchanged**.
   *(logic headless; panel by build/screenshot)*
3. **`CPU::Interrupt()`** — IM0/1/2, IFF, HALT-wake, EI-deferral + tests.
   *(headless)*
4. **Device + Machine** — PAL frame clock (§6), INT per frame, "run as Spectrum"
   via `DebugSession` (breakpoints still apply).
5. **ULA video** — `SpectrumIo` border + full-frame decode (user's decoder) →
   **Screen panel** + FLASH. Acceptance: boot the 48K ROM to its copyright
   screen (screenshot).
6. **Milestone 2+** — keyboard → beeper → tape → cycle-accurate per-scanline
   timing + contention.

Steps 1–3 are pure logic, unit-tested (rot-proof); 4–5 verified by build +
screenshot (the ROM copyright screen is the acceptance shot).

---

*Draft — refined collaboratively; next refinement point is the screen-decoder
interface.*
