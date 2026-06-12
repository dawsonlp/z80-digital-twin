# Floating Bus — Design

**Status:** **implemented** (2026-06-08), verified by `floating_bus_test` and the
direct probe; `kFloatingBusReadT` left at 0 pending `fbustest` calibration. The
first ULA-timing feature toward §C of the
[compatibility plan](../testers/compatibility-plan.md) — Arkanoid, Sidewize, Cobra,
Short Circuit, Aquaplane — plus the `fbustest` suite (§B). See §7 for the
post-implementation findings (notably: Arkanoid has a *separate*, earlier blocker).
**Date:** 2026-06-08

See also: [SPECTRUM_DESIGN.md](spectrum-machine-design.md) §6 (PAL/ULA timing model),
[machine/spectrum/ula.h](../../machine/spectrum/ula.h) (where this lands),
[src/io/callback_io.h](../../src/io/callback_io.h) (the I/O seam),
[HEADLESS_INSTRUMENTATION.md](../testers/headless-instrumentation.md) (how we verify it).

---

## 1. What the floating bus is, and who needs it

On a real 48K the data bus is not driven to a defined level when the CPU reads a
port that **no device decodes**. While the ULA is actively fetching the display,
the byte it just pulled from the display file is still on the shared bus, so an
`IN` from an undecoded port reads back **the ULA's last display fetch** — not a
clean `0xFF`. Outside the active fetch (border, retrace, the top/bottom border
lines) nothing is being fetched and the bus idles to `0xFF`.

That makes the floating-bus byte a **direct readout of where the raster beam is**,
and a range of software abuses it as a free raster clock. This feature is **not
Arkanoid-specific** — the design target is the whole set, which splits into two
correctness demands:

| Program | What it reads the floating bus for | Demands |
|---|---|---|
| **`fbustest`** (§B) | asserts the exact byte at known beam positions | the **exact** fetch pattern *and* read offset — the strict oracle |
| **Arkanoid** (§C) | beam sync for its draw loop | window + idle correct; value tracks the beam |
| **Sidewize, Short Circuit** (§C) | raster sync | same; edge-detects transitions |
| **Cobra** (§C) | floating bus **+** tight timing | value correct *and* stable frame-to-frame |
| **Aquaplane** (§C) | rolling-sea raster effect | beam position correct across the display |

Today our ULA returns `0xFF` for every undecoded port
([ula.h:82-90](../../machine/spectrum/ula.h#L82-L90)), so every one of these is blocked;
Arkanoid's sync loop never sees its expected value and pins in a tiny loop — the
hang the probe reproduces. **We design to `fbustest` (exact), not to Arkanoid
(forgiving)** — getting the strict oracle green makes the §C games correct as a
side effect, not the other way round.

---

## 2. Layering — and why we do *not* add a separate I/O implementation

The natural question is whether floating bus warrants its own `Io` policy. It
does not, and adding one would regress the architecture. The seam already sits at
the right layer:

```
CPU core   CPUImpl<Mem, Io>           — never knows about ports/devices
I/O policy CallbackIo                 — generic bridge: forwards IN/OUT to handlers;
                                         no handler installed => open bus, IN -> 0xFF
DEVICE     Ula::read_port (via OnIn)  — Spectrum-only; decodes ULA ports HERE
```

[callback_io.h](../../src/io/callback_io.h) is explicit about this: it exists to keep
"machine-specific port decoding (e.g. the ZX Spectrum ULA … in the machine layer)
out of the CPU core," and its `kFloating = 0xFF` is the **generic open-bus
default** for a machine with no devices.

**The floating bus is a behaviour of the Spectrum's ULA/bus, not of the Z80's I/O
mechanism.** So it belongs in the `Ula` handler — which is already Spectrum-only
code under [machine/spectrum/](../../machine/spectrum/). Consequences:

- **Zero blast radius for non-Spectrum machines.** Any other `Machine<Cpu>` uses
  `CPUImpl` with a different `Io` policy (or simply installs no `OnIn` handler)
  and keeps the plain `0xFF` open bus. It never compiles, links, or executes a
  line of floating-bus code. The feature is opt-in by virtue of *which device is
  wired in*, not by a flag.
- **Putting it in a generic `Io` policy would be wrong** — it would leak Spectrum
  display timing (scanline geometry, fetch pattern) into machine-agnostic core
  code. The one thing that looks shared, the `0xFF` constant, is correctly the
  generic default; the Spectrum's undecoded-read is a genuine *device* override.
- **`ObservableIo` is unaffected** — it forwards `In()` to the inner device
  unchanged and logs the result, so the floating-bus value shows up correctly in
  the debugger's I/O panel for free.

So: **no new I/O class.** Exactly one branch changes — the odd-port early-out in
`Ula::read_port`:

```cpp
// machine/spectrum/ula.h, read_port()
if ((port & 1) != 0) return 0xFF;          // <-- becomes: return floating_bus();
```

On a bare 48K every device is on an even port (ULA, A0=0), so "odd port" *is*
"undecoded" — the correct and only place floating bus belongs. (When a Kempston
joystick or AY is added later, their decodes are checked *before* falling through
to floating bus, in the same handler.) `read_port` stays `const`: the path only
reads the clock and RAM, mutating nothing. No change to `spectrum_machine.h` or
any dependency. The **one** core touch is the I/O-instruction timing split in §5
— a generic correctness fix to the Z80 core (not Spectrum-specific), required so
`clock_()` reports the true bus-sample T-state.

---

## 3. One beam-fetch authority (so floating bus and contention can't diverge)

To "get it right" for the whole set — and to avoid three different timing models
(rendering, floating bus, future contention) drifting apart — the ULA gains **one
private function that answers "what is the ULA doing at frame T-state `t`?"** and
floating bus is its first consumer:

```cpp
struct BeamFetch {
    bool     active;    // ULA actively fetching the display at t?
    uint16_t address;   // the display-file byte being fetched (valid iff active)
};
[[nodiscard]] BeamFetch beam_fetch_at(uint32_t t) const;
```

This is the single source of truth for the `t → (line, within-line slot,
address)` map. Floating bus reads `address` from live RAM; **contended memory
(separate, larger feature) will reuse the same `active`/timing map** to compute
its 6,5,4,3,2,1,0,0 stall. Rendering keeps its existing per-line resolution
([ula.h `screen_byte`](../../machine/spectrum/ula.h#L143)) — but all three share the
`timing.h` constants as the *only* place geometry is defined, so there is one set
of numbers to calibrate, not three.

---

## 4. The fetch model — T-state → byte on the bus

From [Spectrum Machine Design](spectrum-machine-design.md) §6, one 224-T scanline is:

```
| 128 T display (256 px) | 24 T right border | 48 T retrace | 24 T left border |
  t%224:  0 .......... 127  128 ........ 151   152 .... 199   200 ....... 223
```

and `kDisplayStartT = 64 * 224 = 14336` is the first display pixel of display
line 0; display lines 0..191 occupy absolute scanlines 64..255.

**Step 1 — active fetch?** With `t = frame_tstate()`:

```
line         = t / kTPerLine                 // 0..311
within_line  = t % kTPerLine                 // 0..223
display_line = line - 64                      // 0..191 on screen
active       = display_line in [0,192) && within_line in [0,128)
```

If `!active` → bus idle → `0xFF`.

**Step 2 — which byte.** Across the 128-T display span the ULA fetches 32 cells,
two cells at a time, in a repeating **8-T pattern** (4 fetched bytes + 4 idle —
8 T = 16 px = 2 char cells, a cell being 4 T):

| `within_line % 8` | bus carries |
|---|---|
| 0 | bitmap byte, cell `2k` |
| 1 | attribute byte, cell `2k` |
| 2 | bitmap byte, cell `2k+1` |
| 3 | attribute byte, cell `2k+1` |
| 4–7 | idle → `0xFF` |

where `k = within_line / 8` (0..15) and columns are 0..31. Addresses come from the
existing interleaved helpers ([video.h:47-56](../../machine/spectrum/video.h#L47-L56)).

**Value source: live RAM (`read_(addr)`), deliberately — not** the per-line
beam-accurate `screen_byte()`. Floating bus is a present-instant probe: the byte
the chip clocks onto the bus is whatever is in display memory *right now* at the
position the beam has reached. `read_` is faithful to hardware and needs no extra
state. (`screen_byte` exists to reconstruct a *past* frame's per-line writes for
rendering — a different question.)

```cpp
[[nodiscard]] uint8_t floating_bus() const {
    if (!clock_ || !read_) return CallbackIo::kFloating;     // 0xFF, shared default
    const BeamFetch f = beam_fetch_at(frame_tstate() + kFloatingBusReadT);
    return f.active ? read_(f.address) : CallbackIo::kFloating;
}
```

---

## 5. The hard part: when in the `IN` is the bus sampled? (core finding)

The floating-bus value depends on the **exact T-state the port read samples the
bus**. So the question is what `clock_()` reports at the instant `Ula::read_port`
runs. Tracing the core ([z80_cpu.cpp](../../src/z80_cpu.cpp)) settles it:

The CPU must not use an instruction-atomic timing model for I/O-visible events.
The relevant fetch cycles are charged before `io.In()`/`io.Out()`, so a device
sampling `clock_()` sees the I/O M-cycle rather than the start of the instruction.
Examples:

```cpp
void IN_A_n() {                              // 0xDB
    uint8_t port = memory[PC()++];
    t_cycle += 7;                            // M1 + operand
    A() = io.In((A() << 8) | port);          // <-- handler runs at I/O M-cycle
    t_cycle += 4;
}
void IN_B_C() {                              // ED 40 (prefix charged +4 in prior Step)
    t_cycle += 4;                            // second M1
    B() = io.In(BC());                       // <-- handler runs at I/O M-cycle
    ... flags ...
    t_cycle += 4;
}
```

INI/IND/INIR/INDR follow the same rule with their block-I/O opcode M1 charged
before the port read. At `read_port` time `clock_()` reports the start of the
I/O M-cycle:

| Form | total T | fetch before I/O M-cycle | `t_cycle` at `io.In()` | ⇒ offset to I/O M-cycle |
|---|---|---|---|---|
| `IN A,(n)` (0xDB) | 11 | M1(4)+operand(3) = 7 | start + 7 | **0** |
| `IN r,(C)` (ED) | 12 | M1(4)+M1(4) = 8 | start + 8 | **0** |

**Conclusion: the core exposes a consistent I/O M-cycle timestamp, so the ULA
needs only the within-M3 latch constant `kFloatingBusReadT` for floating-bus
sampling.**

### Decision: charge the pre-I/O fetch cycles *before* `io.In()` (committed)

We commit to the exact, core-hook approach (over a ULA-side constant tuned to one
`IN` form) — the goal is correctness across the whole §B/§C set, and this removes
the per-form ambiguity entirely. Split each `IN`/`INI`-family handler so the fetch
portion is added before the I/O read and the I/O M-cycle after — **keeping every
instruction's total identical**:

```cpp
void IN_A_n() {
    uint8_t port = memory[PC()++];
    t_cycle += 7;                            // M1 + operand: advance to the I/O M-cycle
    A() = io.In((A() << 8) | port);          // clock_() now == I/O M-cycle start, for ALL forms
    t_cycle += 4;                            // M3 (total still 11)
}
```

For the ED forms, the ED prefix M1 is charged by `Step()` when the prefix byte is
fetched. The handler adds the second-M1 (`+4`) before `io.In()`/`io.Out()` and
the remaining I/O-cycle time after, so `clock_()` reads `start + 8` (the I/O
M-cycle). After this, **one** within-M3 latch constant `kFloatingBusReadT` (same
for every form, ~+2..3) is the only thing left to calibrate against `fbustest`.

- **Surface:** `IN A,(n)`, `OUT (n),A`, ED `IN/OUT` register-port handlers,
  and `INI/IND/OUTI/OUTD` families — purely reordering existing cycle adds, no
  logic change.
- **Safety net:** [tests/instruction_timing_test.cpp](../../tests/instruction_timing_test.cpp)
  asserts per-instruction totals; since totals are preserved, it green-lights the
  reorder and guards against a slip.
- **Bonus:** the same split for the `OUT` family later gives contention and
  beeper-edge timing a correct sub-instruction T-state — the §3 beam map then has
  an exact clock on both bus directions.

### The other calibration item

3. **The 8-T pattern phase** (§4): whether slot 0 is bitmap-cell-`2k` or shifted
   is revision-sensitive; `fbustest` pins it. Locked independently by the §6 unit
   tests so a phase re-tune can't break the pattern shape.

Both constants live as named `constexpr` in one place in `ula.h`, commented here.

**Scope boundary — this is not contention.** Floating bus changes a *read value*
only; it does **not** stall the CPU. Contended memory (the larger §B/§D feature)
shares the §3 beam map but is out of scope here.

---

## 6. Worked walkthrough: one `IN`, step by step

A concrete trace of the path once built. **Scenario:** Arkanoid's raster-sync loop
runs `IN A,($FF)` (`A`=0xFF ⇒ port `0xFFFF`, odd/undecoded). The frame started at
absolute cycle `1,000,000`; this instruction begins at **frame-relative T-state
36,807**. Assume the calibrated `kFloatingBusReadT = +2`.

1. **Fetch & dispatch.** `Step()` reads opcode `0xDB`, advances PC, dispatches to
   `IN_A_n()`. No cycles charged yet — `t_cycle` is at the instruction start.
2. **Pre-I/O timing advance (the §5 core hook).** `IN_A_n()` reads operand
   `n=0xFF`, then charges the fetch portion **before** the read:
   `t_cycle += 7;` (M1 4 + operand 3). `t_cycle` is now frame-relative **36,814** —
   the true start of the I/O M-cycle — then `A() = io.In(0xFFFF)`.
3. **Call chain.** `io.In` → `ObservableIo::In` → `CallbackIo::In` →
   `ula_.read_port(0xFFFF)`. (`ObservableIo` logs the result on the way back.)
4. **Port decode.** `read_port` sees `(0xFFFF & 1) != 0` → undecoded → calls
   `floating_bus()` instead of returning `0xFF`.
5. **What T-state?** `floating_bus()` computes `frame_tstate()` =
   `1,036,814 − 1,000,000` = **36,814**, adds `kFloatingBusReadT (+2)` → **t = 36,816**.
6. **Is the beam fetching, and what?** `beam_fetch_at(36,816)`:
   `line = 36,816/224 = 164`; `within = 36,816 − 164·224 = 80`;
   `display_line = 164 − 64 = 100` (∈ [0,192) ✓), `within 80 < 128` ✓ → **active**;
   `slot = 80 % 8 = 0` → bitmap, `k=10` → `cell = 20`;
   `address = bitmap_address(100, 20)` = `0x4000 + 0xC80 + 20` = **`0x4C94`**.
7. **Read the live byte.** `read_(0x4C94)` returns the byte Arkanoid currently has
   there — say **`0x3C`**. That is the byte physically on the bus this instant.
8. **Propagate back.** `floating_bus()` → `read_port` → `CallbackIo` →
   `ObservableIo` (logs) → `io.In` returns `0x3C` ⇒ **`A() = 0x3C`**.
9. **Finish the instruction.** `t_cycle += 4;` (the I/O M-cycle). Total = 7 + 4 =
   **11**, identical to before — `instruction_timing_test.cpp` stays green.
10. **The game reacts.** Arkanoid sees `A ≠ 0xFF`, concludes the beam reached the
    display, and proceeds — the sync it was hanging on.

**The contrast that makes it a raster clock.** Run the *same* instruction during
the top border (start ≈ 5,000 ⇒ `t ≈ 5,002`): `line = 22 < 64`, so `beam_fetch_at`
returns **not active** → `floating_bus()` returns **`0xFF`**. Same code, different
T-state → different value. Today [ula.h:83](../../machine/spectrum/ula.h#L83) returns
`0xFF` *unconditionally*, so the loop never breaks and the CPU pins in the 78-byte
loop the probe showed.

---

## 7. Acceptance & verification — results

**Unit test — PASS.** [tests/floating_bus_test.cpp](../../tests/floating_bus_test.cpp)
(registered in CTest) drives the clock to known T-states with sentinels in the
display file and asserts: display slots 0–3 read bitmap/attr/bitmap/attr of the
right cells, idle slots 4–7 and the border/retrace/top/bottom all float to
`0xFF`, the last column (31) is reached late in the line, and even ports still
read the keyboard matrix. This locks the §4 pattern independently of the timing
offsets. All 21 project tests pass, including `instruction_timing_test` — the
core I/O timing split preserved every instruction total.

**Regression guard — PASS.** Jetpac, Manic Miner, and Underwurlde still load and
run (PC in RAM, coverage and RAM-writes growing) after the change.

**`fbustest` — pending.** Not in the repo (copyrighted-adjacent; supply
separately). It's the oracle that calibrates `kFloatingBusReadT` (left at 0) and
pins the 8-T phase to exact per-position bytes.

### Finding: this Arkanoid.tzx is a Speedlock loader (§F), gated *before* §C

Re-running the probe after implementation shows Arkanoid behaving **identically**
to before, parked in the ROM `WAIT-KEY1` key-wait (`15DE`–`162C`, even port `0xFE`)
— a path the floating bus never touches. Tracing the load (throwaway harness; see
git history of this commit's investigation) showed why:

- **`Arkanoid.tzx` is a Speedlock 2 release.** Its TZX block groups are literally
  named *"Speedlock 2 Block 1/2"* — `0x12`/`0x13`/`0x14` (tone/pulse/pure-data)
  turbo blocks. So it belongs to §F (Speedlock), and that gate comes *before* the
  §C floating-bus gate. Our TZX parser handles every block type present (no dropped
  block).
- **Load sequence:** the ROM loads two BASIC stages (3× `LD-BYTES`, both to `5CCB`),
  then a relocated machine-code loader at `~0xFC00` runs ~14,000 instructions over
  ~3 frames, then takes an error exit to BASIC and the ROM falls into the key-wait.
  The ~45 KB of game data (millions of T-states) never loads.
- **It is NOT the tape reader, and NOT T-state timing** (measured, not assumed).
  Instrumenting every `IN` by where it executes: the ROM read the tape port
  **1,255,552** times during the BASIC loads, but **RAM-resident code read a port
  exactly 0 times.** The Speedlock loader **aborts before it ever touches the
  tape** — so the free-running-tape model (my earlier suspect) is exonerated, and
  so is the tape-sample/EAR timing (it's never reached). The ~14k high-RAM
  instructions are pure computation (self-decrypt / setup / a check) that decides
  to bail.
- **Root cause — FOUND and FIXED: the `R` (memory-refresh) register wasn't
  incrementing.** Single-stepping the decrypt to the bail branch revealed a
  Speedlock decryptor keyed on `R`:
  ```
  62A3  ED 5F  LD A,R      ; key = refresh register
  62A5  AE     XOR (HL)    ; decrypt byte
  62A6  77     LD (HL),A
  62A7  ED A0  LDI         ; advance, loop while BC != 0
  ```
  Every iteration's `LD A,R` returned the **same** value (`0xBA`) — our core only
  ever set `R` at reset / `LD R,A`, never on the per-M1 increment real hardware
  does. So the key was constant, the bytes decrypted to garbage, execution ran
  into a NOP-slide and hit `RST 0x00` (soft reset → BASIC → key-wait). Using `R`
  as a decrypt key is a classic Speedlock anti-emulation trick.
  - **Fix** (`src/z80_cpu.cpp`): increment `R`'s low 7 bits (bit 7 preserved) on
    every M1 opcode fetch in `Step()` — once per Step, skipping the DDCB/FDCB
    states whose Step reads operands, not an M1 — plus once on interrupt-acknowledge.
  - **Result:** Arkanoid now decrypts correctly, the turbo loader reads the tape
    (border loading-stripes, ~10–20k RAM writes/window, 221k SMC writes), and it
    **renders the Arkanoid title/loading screen** and bulk-loads game data — versus
    a blank key-wait before. All 21 tests still pass.

**It reaches its interactive title screen** — not a stall. What looked "frozen"
is the title/control-select menu idling: after load, execution settles into a
large repeating loop (`~0x8040–0xF476`) that polls the keyboard (~240 `0xFE`
reads / 2000 frames) and holds a title tone via the `~0xF446` beeper/delay loop
(`OUT (0xFE)` + register countdowns, IFF1=0, **no port-read wait** — 0
floating-bus reads). Driving keypresses in headlessly confirms it: keys change
the screen and shift the PC region, i.e. the menu is alive and responding. So
Arkanoid loads end-to-end to a working menu; the probe's "FROZEN" was a
false-positive on the tight tone loop.

So the `R`-register fix takes Arkanoid from dead-on-load all the way to its
interactive title screen. The floating bus (correct, proven by
`floating_bus_test`) will matter for in-game raster sync once a start sequence is
driven through to gameplay — the natural next milestone, no longer a blocker.

---

## 8. Summary

- **No separate I/O implementation.** The `CallbackIo` → `OnIn` → `Ula::read_port`
  seam already isolates this; the floating bus is a Spectrum *device* behaviour,
  lives in `ula.h`, and is invisible to non-Spectrum machines by construction.
- **Change surface:** (a) in `ula.h` — one return statement in `read_port`, a
  private `floating_bus()` + `beam_fetch_at()` helper, and two calibration
  constants, reusing the existing clock/RAM-reader/address-helpers/`timing.h`
  geometry; (b) in the Z80 core — the I/O-instruction timing split across ~13
  `IN`-family handlers (§5), a generic correctness fix guarded by
  `instruction_timing_test.cpp`. `read_port` stays `const`.
- **Built right, not Arkanoid-shaped:** designed to the strict `fbustest` oracle,
  with one beam-fetch authority the §C games and future contention all share.
- **Risk** is confined to two `fbustest`-pinned timing offsets; the §4 pattern is
  locked by unit tests independently of them.
