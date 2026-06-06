# ZX Spectrum Hardware (ULA) — Design

**Status:** DRAFT / in progress — design only, **not yet implemented**.
**Date:** 2026-06-05
**Relationship:** extends the debugger ([DEBUGGER_DESIGN.md](DEBUGGER_DESIGN.md))
and current state ([STATUS.md](STATUS.md)). This is the "attach simulated
hardware" initiative: turn the digital twin into a runnable ZX Spectrum 48K you
can also debug.

---

## 1. Goal & first milestone

Attach simulated hardware — the Spectrum **ULA** — so the machine boots its ROM
and shows a real display, while every debugger capability (breakpoints,
coverage, SMC) keeps working on the *running* machine.

**Milestone 1 (this design's scope):**
- CPU prerequisites: **maskable-interrupt injection** and **16-bit I/O
  addressing** (both are hard blockers — see §3).
- A **device abstraction** and a **Machine** that runs the CPU in frame quanta.
- **ULA video**: screen bitmap + attributes → texture in a Screen panel.
- **Border** (`OUT 0xFE`).
- **50 Hz frame interrupt**.

Enough to boot the 48K ROM to its copyright screen. **Keyboard, beeper audio,
and tape are deferred** to later milestones (§8).

## 2. Decisions made (with the user)

1. **One integrated app.** The existing debugger gains a Screen panel and a
   "run as Spectrum" mode over the *same* CPU/memory. You debug a live Spectrum.
2. **Multi-observer memory write hook.** Generalize the memory plug from a single
   write hook to a small observer list; the debugger *and* the ULA both
   subscribe. The ULA marks dirty screen cells on writes to `0x4000–0x5AFF` and
   re-decodes only those — "update the screen only when a screen/attr byte
   changes."
3. **Milestone 1 = CPU prereqs + screen + border + 50 Hz INT** (keyboard/sound
   next).

## 3. CPU prerequisites (hard blockers)

Verified gaps in the current core; both need additive CPU changes and land
*first*, each with headless unit tests.

### 3.1 Maskable-interrupt injection
Today the CPU has `EI`/`DI`/`IM 0/1/2`/`RETI` instructions but **no way to raise
an interrupt** from outside. Proposed:

```cpp
// Request a maskable interrupt (the ULA calls this once per frame).
// If IFF1 is set: accept it — push PC, clear IFF1/IFF2, and per interrupt mode:
//   IM 0: execute the bus instruction (Spectrum bus = 0xFF -> RST 38)
//   IM 1: PC = 0x0038
//   IM 2: vector = (I << 8) | bus; PC = mem[vector] | (mem[vector+1] << 8)
// If the CPU is HALTed, accepting the interrupt wakes it (PC steps past HALT).
// Adds the correct T-states (~13 IM0/1, ~19 IM2). Returns whether accepted.
bool Interrupt(uint8_t bus = 0xFF);

void NonMaskableInterrupt();   // NMI -> 0x0066 (later; not needed for ULA)
```

Correctness notes to honor: **`EI` defers** interrupt acceptance until after the
*following* instruction (so `EI : RET` can't be interrupted between them);
**HALT wake** on accepted INT. Tests: in IM1 with `IFF1`, `Interrupt()` pushes PC
and sets PC=`0x0038`; with `IFF1=0` it's ignored; HALT + INT resumes.

### 3.2 16-bit I/O addressing
`IN A,(n)` does `ReadPort(immediate)` and `IN r,(C)` does `ReadPort(C())` — the
**high** address byte (A or B) is dropped. The Spectrum keyboard *requires* it
(the high byte selects the half-row). Proposed:

- Internally compute the **full 16-bit port**: `IN A,(n)` → `(A << 8) | n`;
  `IN/OUT (C)` → `BC`; `OUT (n),A` → `(A << 8) | n`.
- Add an **installable I/O hook** consulted by IN/OUT with the 16-bit port; when
  none is installed, fall back to the existing 256-byte array (back-compatible).

```cpp
struct IoHook {                       // installed by the Machine; ULA implements it
    std::function<uint8_t(uint16_t port)> in;
    std::function<void(uint16_t port, uint8_t value)> out;
};
```

The raw `ReadPort(uint8_t)`/`WritePort(uint8_t)` stay as the simple poke API.
(An I/O *policy template*, mirroring the memory policy, is a possible future
refactor; an installable hook is enough and lower-churn now.)

## 4. Memory observation (multi-observer)

Generalize the plug's single `WriteHook` into a small **observer list**:

```cpp
// On DebugMemory (and any observing plug):
using WriteObserver = std::function<void(uint16_t addr, uint8_t old, uint8_t neu)>;
int  AddWriteObserver(WriteObserver);   // returns an id
void RemoveWriteObserver(int id);
```

- The **DebugSession** registers its existing dirty/SMC/watch handler.
- The **ULA** registers a handler that, for `0x4000–0x57FF` (bitmap) or
  `0x5800–0x5AFF` (attributes), marks the corresponding **8×8 cell** dirty.
- Cost: the write path iterates 1–2 observers; still far above Spectrum speed
  (only writes are hooked). The non-screen early-out in the ULA handler is a
  cheap range check.

## 5. Device abstraction & Machine

```
Machine = CPUImpl<ObservableMemory>  +  [Device...]  +  frame timing
Device:  OnOut(port,val) / OnIn(port)->val   (via the I/O hook)
         OnMemoryWrite(addr,old,new)         (via the write-observer list)
         OnFrame() / Tick(tstates)           (timing: INT, FLASH, audio)
         (device-specific: render, input)
ULA is the first Device (video, border, keyboard, ear/mic/speaker, 50Hz INT).
```

- **Machine** owns the CPU + memory + devices and the run loop. In the app, a
  "Run as Spectrum" mode drives it; the DebugSession still mediates so
  breakpoints/coverage/SMC apply to the running machine.
- **Frame quantum:** 48K = **69,888 T-states/frame** (224 T/line × 312 lines),
  **~50.08 Hz**. Each frame: deliver the INT at frame start, run the CPU until
  the T-state budget (honoring breakpoints — stop mid-frame if hit), then render
  + poll input. Real-time pacing: one Spectrum frame per ~20 ms wall-clock.

## 6. ULA video (milestone 1)

- **Layout:** bitmap `0x4000–0x57FF` (6144 B, the interleaved 256×192 ordering);
  attributes `0x5800–0x5AFF` (768 B, 32×24 cells). Attribute byte =
  `FLASH(1) BRIGHT(1) PAPER(3) INK(3)`; palette is 8 colours × bright.
- **FLASH:** every 16 frames, cells with the FLASH bit swap ink/paper (~0.32 s
  period). Drives a global re-decode of flashing cells only.
- **Dirty-cell flow:** write observer → mark 8×8 cell dirty → renderer
  re-decodes only dirty cells (plus flashing cells on the flash toggle) into an
  RGBA buffer → upload to a GL texture → **Screen panel** draws it; the
  **border** is drawn around it from the last `OUT 0xFE` (bits 0–2).
- **Decoder:** *pending the user's existing screen-byte decoder* — we'll adapt
  the renderer to its interface rather than reinvent the byte→pixel mapping.

## 7. I/O map (Spectrum 48K, relevant bits)

- `OUT (0xFE)`: bits 0–2 **border** colour; bit 3 **MIC**; bit 4 **speaker**
  (beeper).
- `IN (0xFE)`: bits 0–4 = the five keys of the half-row(s) selected by the high
  address byte (0 = pressed); bit 6 = **EAR** (tape in).
- The ULA responds to all even ports (A0 = 0); `0xFE` is the canonical address.

## 8. Deferred (later milestones)

- **Keyboard** — 8×5 key matrix; host-key → matrix mapping (incl. CAPS/SYMBOL
  SHIFT); `IN (0xFE)` returns the selected rows. (Milestone 2.)
- **Beeper audio** — `OUT 0xFE` bit 4 toggles → square wave from toggle
  timestamps; needs an audio backend (choice open: miniaudio / SDL / PortAudio,
  or WAV capture first). (Milestone 3.)
- **Tape (EAR in)** — load `.tap`/`.tzx` by feeding bit 6 of `IN 0xFE`.
- **Memory/IO contention timing** and **128K paging** — out of scope; would need
  a more timing-accurate core and a bank-aware memory model.

## 9. Open questions / pending

- **Screen decoder**: location + interface (user to provide). Renderer is
  designed around plugging it in.
- **Audio backend**: choose when we reach the beeper milestone.
- **Interrupt fine-detail**: EI-deferral and HALT-wake semantics in `Interrupt()`
  — get these right in unit tests.
- **Frame/run-loop integration**: exact reconciliation of the Machine's
  T-state-budgeted frame with DebugSession's instruction-budgeted `RunSlice`
  (likely a `RunFrame(tstate_budget)` that reuses the inline breakpoint check).

## 10. Next steps (ordered)

1. **CPU: `Interrupt()`** (IM0/1/2, IFF handling, HALT-wake, EI-deferral) + unit
   tests. *(headless)*
2. **CPU: 16-bit I/O** — combine the high byte in IN/OUT; installable I/O hook;
   keep raw `ReadPort`/`WritePort`. Unit tests (high byte visible; hook routing).
   *(headless)*
3. **Memory: multi-observer write hook** on the plug; migrate DebugSession to it;
   unit test that two observers both fire. *(headless)*
4. **Device + Machine scaffolding** — frame loop, INT per frame, "run as
   Spectrum" mode wired through DebugSession (breakpoints still apply).
5. **ULA video** — border + dirty-cell screen tracking + decode (user's decoder)
   → texture → **Screen panel** + FLASH. Verify by booting the 48K ROM to its
   copyright screen (screenshot).
6. **Milestone 2+**: keyboard → beeper → tape.

Steps 1–3 are pure logic and unit-tested (rot-proof, same as the rest of the
core); 4–5 are verified by build + screenshot (the ROM copyright screen is the
acceptance shot).

---

*Draft — to be refined as we go, starting with the screen decoder interface.*
