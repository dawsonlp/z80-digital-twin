# Compatibility & Timing Test Plan

**Status:** living document. A battery of real ZX Spectrum software that
historically broke emulators — exotic loaders, cycle-exact ULA timing,
undocumented CPU behaviour — used to find where the twin diverges from hardware.
**Date:** 2026-06-08

"Most games work" is where the interesting 5% begins. This plan lists the
programs that *don't* work unless the emulator is right about something subtle,
says **why** each is hard, what the twin does **today**, and how to drive it
headlessly with [`spectrum_probe`](HEADLESS_INSTRUMENTATION.md).

See also: [SPECTRUM_DESIGN.md](SPECTRUM_DESIGN.md) (ULA/timing model),
[HEADLESS_INSTRUMENTATION.md](HEADLESS_INSTRUMENTATION.md) (the probe + recipes).

> **You supply the images.** ROMs and game tapes are copyrighted and not in the
> repo. The CPU test suites (§A) are freely distributable and the best starting
> point.

---

## Feature prerequisites (what gates what)

Several tests below can't pass until a ULA feature exists. Current state, from
[machine/spectrum/ula.h](machine/spectrum/ula.h):

| Feature | Status | Gates |
|---|---|---|
| **Floating bus** (IN from unmapped/odd port returns the byte the ULA is fetching) | ❌ floats to `0xFF` ([ula.h](machine/spectrum/ula.h#L82-L90)) | §C Arkanoid, Sidewize, Cobra, Short Circuit |
| **Contended memory** (CPU stalled on 0x4000–0x7FFF during the display window) | ❌ not modelled (frame runs a flat T-state budget) | §B contention tests, §D multicolour |
| **`MEMPTR`/WZ** internal register | ❓ unverified | §A `z80memptr` |
| **Undocumented `SCF`/`CCF` flags** (YF/XF from prior A) | ❓ unverified | §A `z80ccf` |
| **TZX `0x15` direct recording** (sampled audio) | ❌ not parsed | §F speech loaders |
| **TZX `0x23` jump / `0x26` call** flow blocks | ⚠️ skipped, not executed ([tape.h](machine/spectrum/tape.h)) | §F multi-stage loaders |

The two highest-value features are **floating bus** and **contended memory** —
between them they unlock §B, §C, and §D.

---

## A. CPU correctness suites — run these first (priority 1)

Synthetic, self-checking binaries: they isolate CPU bugs from ULA/timing bugs
and print a pass/fail CRC. Freely distributable. Drive headless: load, start,
and watch the screen / PC settle.

| Program | What it stresses | Notes |
|---|---|---|
| **ZEXDOC** | every *documented* instruction's result + flags vs CRC | run before ZEXALL |
| **ZEXALL** | as ZEXDOC, **including** undocumented flag bits 3 & 5 | the broad net |
| **z80doc / z80full** (Patrik Rak) | documented / full instruction behaviour | faster, Spectrum-native |
| **z80flags** | flag edge cases | |
| **z80ccf** | undocumented **YF/XF** from `SCF`/`CCF` (depends on previous A; CPU-revision-dependent) | classic failure point |
| **z80memptr** | the **MEMPTR/WZ** internal register leaking into `BIT n,(HL)` flags | almost nobody gets this without implementing WZ |

**Acceptance:** ZEXDOC + z80ccf + z80memptr all green. These three reveal more
real bugs than any game.

---

## B. ULA / timing torture (priority 2 — needs floating bus + contention)

| Program | What it stresses |
|---|---|
| **Floating-bus test** (`fbustest` and similar) | IN from an unmapped port returns the current display/attribute fetch byte, not `0xFF` |
| **Contended-memory tests** | the per-T-state stall pattern (6,5,4,3,2,1,0,0) on display-window RAM access |
| **Snow-effect tests** | refresh corruption when `I` points into contended RAM (real-hardware-only; usually skipped) |

---

## C. Games that famously broke emulators (priority 2)

| Game | Why it's hard |
|---|---|
| **Arkanoid** (Imagine/Ocean) | *The* canonical floating-bus game — hangs/misbehaves early without it. If you test one thing, test this. |
| **Sidewize** | floating-bus dependent for raster sync |
| **Cobra** (Ocean) | floating bus + tight timing |
| **Short Circuit** (Ocean) | floating bus |
| **Aquaplane** (Quicksilva) | the rolling-sea effect needs accurate timing; wrong timing tears it |
| **Fairlight / Fairlight II** (The Edge) | careful interrupt/timing; an early stress case |

**Acceptance:** Arkanoid boots to attract mode and plays. Pair with `fbustest`
and `z80memptr` as a tight three-program acceptance set for the timing features.

---

## D. Multicolour / raster engines (priority 3 — needs cycle-exact contention)

Per-scanline attribute rewrites (8×1 colour) and border-time CPU effects, in
lock-step with the beam. The strictest timing tests that exist.

| Program | What it stresses |
|---|---|
| **Nirvana / Nirvana+** engine + games (*Gandalf*, *Sword of Ianna*, *Wonderful Dizzy*) | cycle-accurate contended timing + interrupt position for 8×1 multicolour |
| **Shock! Megademo**, **Reset Megademo**, demoscene "border-buster" productions | graphics drawn in the border — only correct if /INT timing and T-states-per-line are exact |

---

## E. Beeper music engines (priority 3 — needs audio; timing errors become audible)

Pitch/tempo are a direct readout of CPU timing accuracy — an excellent
regression signal once the beeper path is wired into a test.

| Program | What it stresses |
|---|---|
| **Tim Follin** scores: *Agent X II*, *Chronos*, *LED Storm* | multi-channel beeper; small timing error detunes them audibly |
| **Octode / QChan / Phaser1** (Shiru, modern) | engines designed to expose timing wobble |
| **Fairlight / Cobra** digitized speech | software-timed sample playback |

---

## F. Exotic loaders / protection (priority 1 — testable today)

This is the category the TZX loop-block fix opened up. Drive with
`spectrum_probe --load`; the freeze detector reports load-vs-hang directly.

| Loader / family | Examples | What it stresses |
|---|---|---|
| **Ultimate Play the Game** | *Underwurlde* ✅, *Knight Lore*, *Sabre Wulf*, *Atic Atac*, *Lunar Jetman* | TZX loop (0x24/0x25) + tone/pulse/pure-data turbo signal |
| **Speedlock** (many revisions) | Ocean / US Gold titles | timing-sensitive turbo + anti-tamper; revisions are mutually incompatible |
| **Alkatraz** | US Gold titles | heavy self-modifying / decrypting code — good `SetBreakOnSmc` test |
| **Bleepload** (Firebird) | Firebird titles | multi-speed, anti-piracy, border noise during load |
| **Paul Owens / Microsphere** | various | multi-stage loads, embedded timing checks (exercise 0x23/0x26 flow blocks) |

**Acceptance:** the four Ultimate titles above all reach their title screen
(same loader family as the fixed *Underwurlde*).

---

## How to run a test headlessly

```bash
# Loader / protection (§F): does it load or freeze?
./build/spectrum_probe spec48.rom --tape GAME.tzx --load --frames 12000 --screen
#   -> watch RAMwr stay high while loading; a "FROZEN (tight loop in RAM)" row = hang.

# CPU suite (§A): start it, then watch the screen settle to a PASS/FAIL report.
./build/spectrum_probe spec48.rom --tape zexdoc.tap --load --frames 200000 --window 5000 --screen

# Want register/flag detail at a point? Set a breakpoint in your own harness:
#   DebugSession session(machine.cpu());
#   session.AddBreakpoint(ADDR); session.Run();
# See HEADLESS_INSTRUMENTATION.md §7 for verified recipes.
```

As features land (floating bus, contention), promote the gated tests from
"blocked" to "expected green" and add a row to the acceptance sets above.

---

## Priority summary

1. **Now:** §A CPU suites · §F loaders (testable with the current build).
2. **Next feature — floating bus:** unlocks §C (Arkanoid et al.) + §B floating-bus test.
3. **Next feature — contended memory:** unlocks §B contention + §D multicolour.
4. **With audio in a harness:** §E beeper-timing regression signal.
