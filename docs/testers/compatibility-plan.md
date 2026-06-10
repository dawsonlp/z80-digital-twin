# Compatibility & Timing Test Plan

**Status:** living document. A battery of real ZX Spectrum software that
historically broke emulators — exotic loaders, cycle-exact ULA timing,
undocumented CPU behaviour — used to find where the twin diverges from hardware.
**Date:** 2026-06-10

"Most games work" is where the interesting 5% begins. This plan lists the
programs that *don't* work unless the emulator is right about something subtle,
says **why** each is hard, what the twin does **today**, and how to drive it
headlessly with [`spectrum_probe`](headless-instrumentation.md).

See also: [SPECTRUM_DESIGN.md](../developers/spectrum-machine-design.md) (ULA/timing model),
[HEADLESS_INSTRUMENTATION.md](headless-instrumentation.md) (the probe + recipes).

> **You supply the images.** ROMs and game tapes are copyrighted and not in the
> repo. The CPU test suites (§A) are freely available external assets and the
> best starting point; the repo contains the harness and asset manifest, not the
> third-party binaries.

---

## Feature prerequisites (what gates what)

Several tests below can't pass until a ULA feature exists. Current state, from
[machine/spectrum/ula.h](../../machine/spectrum/ula.h):

| Feature | Status | Gates |
|---|---|---|
| **Floating bus** (IN from unmapped/odd port returns the byte the ULA is fetching) | ✅ implemented ([ula.h](../../machine/spectrum/ula.h) `floating_bus`/`beam_fetch_at`; `floating_bus_test`); `kFloatingBusReadT` pending `fbustest` calibration | §C Arkanoid, Sidewize, Cobra, Short Circuit |
| **`R` refresh register** (low 7 bits increment per M1; read by `LD A,R`) | ✅ implemented (`Step()` per-M1 + interrupt-ack) | §F Speedlock R-keyed decryptors (Arkanoid) |
| **Contended memory** (CPU stalled on 0x4000–0x7FFF during the display window) | ❌ not modelled (frame runs a flat T-state budget) | §B contention tests, §D multicolour |
| **`MEMPTR`/WZ** internal register | ❓ unverified by dedicated MEMPTR suite; WZ exists and is used in several addressing paths | §A `z80memptr` |
| **Undocumented flag bits 3/5** (`F3`/`F5`) across core CPU operations | ✅ ZEXALL green through `cpu_suite_runner`; includes `SCF`/`CCF`, ALU, BIT, INC/DEC, block ops, rotates/shifts | §A ZEXALL; §A `z80ccf` remains a useful independent regression |
| **TZX `0x15` direct recording** (sampled audio) | ❌ not parsed | §F speech loaders |
| **TZX `0x23` jump / `0x26` call** flow blocks | ⚠️ skipped, not executed ([tape.h](../../machine/spectrum/tape.h)) | §F multi-stage loaders |

The two highest-value features are **floating bus** and **contended memory** —
between them they unlock §B, §C, and §D.

---

## A. CPU correctness suites — run these first (priority 1)

Synthetic, self-checking binaries: they isolate CPU bugs from ULA/timing bugs
and print a pass/fail CRC. Drive ZEXDOC/ZEXALL through the CP/M `.COM` harness;
use Spectrum-machine execution for Spectrum-native suites.

| Program | What it stresses | Current status |
|---|---|---|
| **ZEXDOC** | every *documented* instruction's result + flags vs CRC | ✅ passing via `cpu_suite_runner --case zexdoc` |
| **ZEXALL** | as ZEXDOC, **including** undocumented flag bits 3 & 5 | ✅ passing via `cpu_suite_runner --case zexall` |
| **z80doc / z80full** (Patrik Rak) | documented / full instruction behaviour | not yet harnessed; useful independent Spectrum-native signal |
| **z80flags** | flag edge cases | not yet harnessed |
| **z80ccf** | undocumented **YF/XF** from `SCF`/`CCF` (depends on previous A; CPU-revision-dependent) | not yet harnessed; lower risk now that ZEXALL passes |
| **z80memptr** | the **MEMPTR/WZ** internal register leaking into `BIT n,(HL)` / indexed flags | not yet harnessed; still the main CPU-side unknown |

**Current acceptance:** ZEXDOC and ZEXALL are green with the external CP/M
harness. That establishes broad instruction and flag correctness, including the
full `F3/F5` surface checked by ZEXALL.

**Next CPU acceptance:** add independent `z80memptr` and `z80ccf` assets once
we have Spectrum-native or raw adapters. `z80memptr` is higher value now because
ZEXALL does not fully close the MEMPTR/WZ question.

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
| **Arkanoid** (Imagine/Ocean) | *The* canonical floating-bus game — hangs/misbehaves early without it. If you test one thing, test this. **Note:** the `Arkanoid.tzx` in the repo is a **Speedlock 2** release, so it gates on §F (Speedlock) *before* the floating bus matters — after the `R`-register fix its loader decrypts and runs to the interactive title screen; driving into gameplay (where the floating bus applies) is the next step (see [FLOATING_BUS_DESIGN.md](../developers/floating-bus-design.md) §7). Use a non-Speedlock §C title to exercise the floating bus alone. |
| **Sidewize** | floating-bus dependent for raster sync |
| **Cobra** (Ocean) | floating bus + tight timing |
| **Short Circuit** (Ocean) | floating bus |
| **Aquaplane** (Quicksilva) | the rolling-sea effect needs accurate timing; wrong timing tears it |
| **Fairlight / Fairlight II** (The Edge) | careful interrupt/timing; an early stress case |

**Acceptance:** Arkanoid boots to attract mode and plays. Pair with `fbustest`
and `z80memptr` as a tight three-program acceptance set for the timing features.
CPU-suite correctness is no longer the gating uncertainty for this path; the
remaining risk is Spectrum timing, floating-bus calibration, contention, and
loader behavior.

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
| **Speedlock** (many revisions) | Ocean / US Gold titles; **Arkanoid** (Speedlock 2) | timing-sensitive turbo + anti-tamper; revisions are mutually incompatible. **Status:** root-caused & fixed one anti-tamper trick — the decryptor is keyed on the **`R` refresh register** (`LD A,R`/`XOR (HL)`), which our core wasn't incrementing per-M1, so the key was constant → garbage decrypt → `RST 0`. Fixed (R now increments on every M1 + interrupt-ack). Arkanoid now decrypts, loads end-to-end, and reaches its **interactive title/control-select screen** (polls the keyboard, responds to keypresses; the `~0xF470` `OUT (0xFE)` loop is just a held title tone, not a hang). Driving a start sequence into gameplay — where the floating bus does its raster sync — is the next milestone. |
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

# CPU suite (§A): CP/M .COM harness. Assets live outside the repo.
Z80_COMPAT_ASSETS=/path/to/compat-assets ./build/cpu_suite_runner --case zexdoc
Z80_COMPAT_ASSETS=/path/to/compat-assets ./build/cpu_suite_runner --case zexall

# Or through CTest:
Z80_COMPAT_ASSETS=/path/to/compat-assets ctest --test-dir build -R cpu_suite --output-on-failure

# Want register/flag detail at a point? Set a breakpoint in your own harness:
#   DebugSession session(machine.cpu());
#   session.AddBreakpoint(ADDR); session.Run();
# See HEADLESS_INSTRUMENTATION.md §7 for verified recipes.
```

As features land (floating bus, contention), promote the gated tests from
"blocked" to "expected green" and add a row to the acceptance sets above.

---

## Priority summary

1. **Now:** §F loaders and Spectrum-native CPU edge suites (`z80memptr`, `z80ccf`) once assets/adapters are available.
2. **Next feature — floating bus calibration:** unlocks §C (Arkanoid et al.) + §B floating-bus test.
3. **Next feature — contended memory:** unlocks §B contention + §D multicolour.
4. **With audio in a harness:** §E beeper-timing regression signal.
