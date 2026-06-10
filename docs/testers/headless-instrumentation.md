# Headless Instrumentation

**Status:** living document. How to drive and observe a running machine from
code — no window, no GPU — and why the architecture makes that the *default*
rather than an afterthought.
**Date:** 2026-06-08

This is the "how do I explore anything without a UI" guide. The worked example
is a real bug: *Underwurlde* loaded a stub then froze in a tight loop. We
reproduce, diagnose, and confirm the fix entirely headlessly.

See also: [ARCHITECTURE.md](../developers/architecture.md) (the layering this builds on),
[SPECTRUM_DESIGN.md](../developers/spectrum-machine-design.md) (the ULA/timing), and
[DEBUGGER_DESIGN.md](../developers/debugger-design.md) (the session/coverage model).

---

## 1. Why headless is first-class here

The engine is one CPU core templated on its environment (memory + I/O policies),
and the capabilities are **UI-free static libraries**:

```
CORE        z80_cpu (src/)        — CPUImpl<Memory, Io>, fully inlined per config
CAPABILITY  z80_machine           — ZX Spectrum: ULA, video, tape, beeper (header-only)
CAPABILITY  z80_debugger_core     — DebugSession: stepping, breakpoints, coverage, SMC
FRONTEND    spectrum / z80_debugger — the ONLY layer that pulls in GLFW/ImGui
```

The crucial property: **`SpectrumMachine` and `DebugSession` are the same CPU
configuration** —
`CPUImpl<ObservableMemory, ObservableIo<CallbackIo>>`. So a `DebugSession` can
wrap the *live* machine's CPU and instrument it while the machine runs. The
window in `apps/spectrum/main.cpp` is just one consumer of `SpectrumMachine`;
everything it shows (screen, keyboard, tape) is reachable from a `.cpp` with no
display.

That is what `examples/spectrum_probe.cpp` is: a command-line harness that boots
a ROM, types on the keyboard, plays a tape, and reports what the CPU did.

---

## 2. The instrumented frame

`SpectrumMachine::run_frame()` advances one PAL frame using the machine's *raw*
stepper — fast, but opaque. To **observe** each frame, advance the same CPU
through the `DebugSession` instead. The two differ only in the stepper; the ULA
wiring (border timeline, screen writes, frame counter) is identical:

```cpp
void run_instrumented_frame(SpectrumMachine& machine, DebugSession& session) {
    machine.ula().begin_frame();          // drop last frame's write/border history
    machine.cpu().Interrupt(0xFF);        // assert 50 Hz /INT; wakes the ROM's HALT
    session.Run();
    session.RunForTStates(timing::kTPerFrame);   // 69,888 T — breakpoint/coverage-aware
    machine.ula().end_frame();            // resolve border per-line, advance FLASH
}
```

This is exactly the decomposition `machine.h` documents: *"production/fast = a
lambda over `RunUntilCycle`; debugging = a lambda over
`DebugSession::RunForTStates`."* `Interrupt(0xFF)` each frame both fires the
frame interrupt and wakes the CPU if the ROM is idling on `HALT` (which it does
between frames). The session then accumulates coverage, dirty-RAM, and SMC for
that frame, for free.

What the session exposes after each frame (`debugger/exec/debug_session.h`):

| Call | What it tells you |
|---|---|
| `CoveredBytes()` / `CoveragePercent()` | distinct bytes ever executed as code — *grows while new code paths run* |
| `DirtyAddresses()` / `ClearDirty()` | addresses written since the last clear — *grows while RAM is being filled* |
| `SmcCount()` / `SmcEvents()` | self-modifying-code writes (decompressors, decryptors) |
| `BlockedWrites()` | writes refused by ROM write-protect |
| `AddBreakpoint()` / `AddWatchpoint()` | stop at a PC or on a write to an address |
| `cpu().PC()`, `cpu().A()`, `cpu().HL()`, … | full register/flag state at any boundary |

---

## 3. Driving the keyboard matrix headlessly

The 48K keyboard is an **8×5 matrix** read through the ULA's even ports, not a
character buffer (`machine/spectrum/keyboard.h`). The high byte of the port
address selects a half-row (active **low**); data bits D0–D4 read that row, and a
**0 means pressed**. So "press A" is "pull half-row 1, bit 0 low":

```cpp
ula.key_down(half_row, bit);   // clear the bit  (pressed)
ula.key_up(half_row, bit);     // set the bit    (released)
ula.release_all_keys();        // all rows back to 0x1F
```

The matrix is a **level**, not an event — the ROM's keyboard scan (run from the
`0x0038` interrupt handler, i.e. once per frame) samples whatever is held *right
now*. So "typing" a key means holding the row(s) low for a few frames, then
releasing for a few, so the scan sees a clean press→release:

```cpp
void press_chord(machine, session, keys, hold_frames, gap_frames) {
    ula.release_all_keys();
    for (k : keys) ula.key_down(k.half_row, k.bit);
    repeat hold_frames:  run_instrumented_frame(...);   // ROM samples the press
    ula.release_all_keys();
    repeat gap_frames:   run_instrumented_frame(...);    // and the release
}
```

A *chord* is the set of keys held together. Two cases matter for loading a game:

- **Shifts are just more keys.** `"` is **SYMBOL SHIFT + P** → hold
  `{kSymbolShift, key_for_ascii('P')}` together. `DELETE` is **CAPS SHIFT + 0**.
- **Keyword entry.** At power-on the cursor is in `K` (keyword) mode, where the
  **J key alone emits the whole `LOAD` token** — you do not spell "L-O-A-D".

So `LOAD""` + ENTER is four chords: `J`, then `SYM+P`, `SYM+P`, `ENTER`. The probe
encodes this as the script `L""\n` (where `L`→J-keyword, `"`→SYM+P, `\n`→ENTER).

> Mapping note: GLFW's printable key tokens equal uppercase ASCII, so the
> viewer's `poll_keyboard` and the probe's script share the same `keyboard.h`
> ASCII table — the matrix layout has exactly one home.

---

## 4. Seeing the screen as text

`render_indices()` fills a frame of palette indices (border + 256×192 display).
To read it headlessly, take the most common index as "paper" and print the
display area downsampled to one character per 4×8 cell — `#` for any non-paper
(ink) pixel. Enough to read text and recognise a title screen:

```
  |         #######      ##    ################                    |   <- Underwurlde
  |         #######                                                |      title screen,
  |           ##         ##    ################  ################  |      loaded headlessly
```

(`apps/spectrum/main.cpp --shot FILE` writes a full-colour PPM instead, for
pixel-exact verification. Same `render_indices()`, different sink.)

---

## 5. The probe tool

`spectrum_probe` (built by default; needs your own `spec48.rom` + a tape image):

```
spectrum_probe [rom.rom] [options]
  --tape FILE     Load a .tap/.tzx image (auto-detected).
  --load          Type LOAD"" + ENTER, then play the tape.
  --type "KEYS"   Type a key-script (L=LOAD token, "=SYM+P, _/space=SPACE, \n=ENTER).
  --play          Start the tape without typing LOAD.
  --boot N        Frames to boot before typing      (default 100).
  --frames N      Frames to run and instrument       (default 2500).
  --window N      Emit a report row every N frames   (default 100).
  --screen        Dump the screen as ASCII at the end.
```

The report’s columns are chosen to separate **loading**, **running**, and
**frozen**:

| Column | Reading |
|---|---|
| `+code` | new bytes executed this window (`ΔCoveredBytes`) |
| `RAMwr` | distinct RAM writes **outside** the display file this window |
| `PC-range` | min–max PC sampled at frame boundaries (and its span) |
| `hotpage` | most-sampled 256-byte page of PC |
| `border` | current border colour (cycles during a tape load = loading stripes) |
| `state` | verdict: `running` / `IDLE/spin (ROM)` / `FROZEN (tight loop in RAM)` |

The verdict heuristic: **no new code + essentially no RAM writes + a tiny PC
range** = a spin. If the PC sits in ROM it's the BASIC editor idling; in RAM with
a tape "playing" it's a wedged loader.

---

## 6. Worked example: the Underwurlde freeze

**Symptom.** The game loads a short stub, a turbo loader takes over, then it
freezes in a tight loop in RAM. Most games (Jetpac, Manic Miner) work.

**Reproduce + diagnose** — with the buggy tape parser:

```
$ spectrum_probe spec48.rom --tape underwurlde.tzx --load --frames 6000 --window 2000
Tape: underwurlde.tzx (2 blocks, 28220 pulses, 17s)     <- only 2 blocks parsed!
frame   +code   RAMwr  PC-range        hotpage  border  state
 2000    2292    1616  03D5-F25F (61066)  F200     7     running
 4000       0       2  F230-F25F (  47)  F200     7     FROZEN (tight loop in RAM)
 6000       0       2  F230-F25F (  47)  F200     7     FROZEN (tight loop in RAM)
```

The signature is unambiguous: after the stub loads (frame ~2000), the PC pins to
a **47-byte loop in RAM** (`F230–F25F`), executes **no new code**, writes **no
RAM**, and the border stops cycling — the stub is polling the EAR line for a
turbo signal that never arrives. And the tape only parsed to **2 blocks / 17 s**.

The root cause was in `machine/spectrum/tape.h`: the `.tzx` parser had no case
for block **0x24 (Loop Start)** — which Underwurlde uses to encode its pilot
tone — so it bailed via the `default:` branch and dropped the entire turbo
signal. (See the fix commit; loop blocks are now unrolled.)

**Confirm the fix** — same command, fixed parser:

```
Tape: underwurlde.tzx (8 blocks, 720153 pulses, 196s)   <- full tape now
frame   +code   RAMwr  PC-range        hotpage  border  state
 2000    2613    1871  03D5-F351 (61308)  F200     6     running
 4000      19    5438  F230-F34F ( 287)  F200     6     running   <- turbo loader
 6000       0   10345  F230-F320 ( 240)  F200     6     running      decoding to RAM
 8000       0   10582  F230-F320 ( 240)  F200     1     running
10000     788   38930  67FC-F31E (35618)  F200     0     running   <- hand-off to game
12000       0      24  90C6-AE16 (7504)  9800     0     running   <- game main loop
```

Now the turbo loop runs but `RAMwr` stays at ~10k writes/window (it's *loading*,
not spinning), the hand-off at frame 10000 lights up coverage and RAM, and
`--screen` shows the Underwurlde title screen. Loaded end-to-end, no display.

---

## 7. Recipes

**Boot to BASIC and screenshot the copyright line**
```
spectrum_probe spec48.rom --boot 150 --frames 0 --screen
```

**Type an arbitrary key sequence** (e.g. `PRINT` keyword = the `P` key, ENTER)
```
spectrum_probe spec48.rom --type "P\n" --screen
```

**Break at a PC and inspect** — in your own harness:
```cpp
DebugSession session(machine.cpu());
session.AddBreakpoint(0x0556);                 // ROM LD-BYTES entry (48K)
session.Run();
while (session.State() == RunState::Running) run_instrumented_frame(machine, session);
printf("hit LD-BYTES: A=%02X DE=%04X\n", machine.cpu().A(), machine.cpu().DE());
```

**Watch a RAM address get written** — `session.AddWatchpoint(addr)`, then the
next `RunForTStates` stops with `StopReason::Watchpoint` and
`session.LastWatchpointHit()`.

**Catch self-modifying / decrypting loaders** — `session.SetBreakOnSmc(true)`, or
just read `SmcCount()` per window (Underwurlde shows 411 SMC writes — its loader
rewrites itself).

The pattern generalises beyond the Spectrum: any `Machine<Cpu>` config can be
driven frame-by-frame through a `DebugSession` and observed the same way.
