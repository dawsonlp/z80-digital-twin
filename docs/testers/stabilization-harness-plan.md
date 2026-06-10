# Stabilization Harness Plan

**Purpose:** turn the compatibility plan into repeatable regression signal. The
repo already has good unit coverage for isolated CPU/device behavior and a
headless exploratory probe. The missing layer is a small set of harnesses that
run external Spectrum programs under explicit oracles.

This plan is intentionally asset-light: ROMs, games, and many historical tests
are not committed. Harnesses must skip cleanly when a required external image is
absent, while still producing hard pass/fail results when the image is supplied.

## Current Baseline

Existing useful pieces:

- `tests/*_test.cpp`: deterministic unit tests for CPU behavior, timing
  classes, refresh register, ULA rendering, floating bus, tape parsing, beeper,
  boot, and debugger instrumentation.
- `examples/spectrum_probe.cpp`: headless machine runner with keyboard typing,
  tape playback, screen ASCII dump, coverage, dirty-RAM, SMC, PC-window, and
  freeze detection.
- `DebugSession`: the right instrumentation surface for compatibility tests
  because it runs the live `SpectrumMachine` CPU, not a proxy.

Main gap:

- `spectrum_probe` is exploratory CLI output, not a structured test harness. It
  does not consume a test manifest, does not encode expected outcomes, and does
  not write stable artifacts for comparison.

## Harnesses To Add

### 1. CPU Suite Runner

**Need:** run Spectrum-native CPU correctness suites from the compatibility plan:
ZEXDOC, ZEXALL, Patrik Rak `z80doc`/`z80full`, `z80flags`, `z80ccf`, and
`z80memptr`.

Detailed plan: [CPU Correctness Suites](cpu-correctness-suites.md).

**Why:** these isolate CPU defects from ULA/tape/game behavior. The highest-value
acceptance set is `ZEXDOC + z80ccf + z80memptr`.

**Approach:**

- Add a `spectrum_suite_runner` executable or a CTest-oriented test binary that
  uses `SpectrumMachine + DebugSession`.
- Inputs come from environment variables or a manifest, not from committed ROMs:
  `Z80_SPEC48_ROM`, `Z80_COMPAT_ASSETS`, and per-test tape/snapshot paths.
- Drive boot, optional `LOAD""`, tape play, and frame budget like
  `spectrum_probe`.
- Detect completion by screen text and/or known terminal PC/spin state.
- Produce a normalized result:
  `PASS`, `FAIL`, `TIMEOUT`, `FROZEN`, or `SKIP`.

**Oracles:**

- Primary: parse final screen text or OCR-lite bitmap signatures for expected
  `OK`, `PASS`, or known CRC report lines.
- Secondary: PC/range freeze detector to distinguish "test still running" from
  "wedged".
- Optional: per-suite known result address if a given suite exposes one.

**CTest behavior:** register each suite as a separate test. Missing assets skip,
not fail.

### 2. Compatibility Manifest Runner

**Need:** convert `COMPATIBILITY_TEST_PLAN.md` into executable cases without
hard-coding every game in C++.

**Approach:**

- Add a simple data file, for example `compat/manifest.json`, with:
  - test name and category,
  - ROM variable/path,
  - tape/snapshot path,
  - boot frames, run frames, report window,
  - key script,
  - expected state,
  - expected screen signature,
  - feature gates such as `floating_bus`, `contention`, `tzx_direct_recording`.
- Add a runner that executes one manifest case by name.
- CTest can register smoke cases individually, while developers can run the full
  manifest locally when they have assets.

**Oracles:**

- State oracle: no `FROZEN (tight loop in RAM)` after a given frame.
- Screen oracle: compare a low-resolution normalized display hash or ASCII
  signature.
- Progress oracle: coverage and non-screen RAM writes stay above a threshold
  while loading.
- Milestone oracle: PC reaches a known address or screen changes to a named
  state.

**Why this matters:** games/loaders need different key sequences and budgets.
Putting that in code will make the harness brittle and hard to expand.

### 3. Golden Screen / Frame Artifact Harness

**Need:** objective visual regression for boot screens, title screens, floating
bus demos, and later multicolour/raster tests.

**Approach:**

- Extend the headless runner to emit:
  - full-frame palette-index dumps,
  - PPM/PNG screenshots,
  - downsampled ASCII,
  - stable hashes of selected rectangles.
- Store generated artifacts under `build/compat-artifacts/`.
- Keep checked-in goldens only for synthetic/free fixtures. For copyrighted
  games, support user-local goldens under `compat/local-goldens/` or
  `$Z80_COMPAT_GOLDENS`.

**Oracles:**

- Use exact hashes for deterministic synthetic fixtures.
- Use rectangle hashes or tolerant palette-index comparisons for title screens
  where flash phase or border timing can vary.

### 4. TZX Block Coverage Harness

**Need:** stabilize exotic loader support without relying only on whole-game
  behavior.

**Approach:**

- Add synthetic TZX unit tests for every parsed block type and every currently
  skipped control-flow block:
  - already covered: `0x10`, `0x24`, `0x25`, metadata skipping.
  - add direct tests for `0x11`, `0x12`, `0x13`, `0x14`, `0x20`, `0x23`,
    `0x26`, `0x27`, `0x2A`, `0x2B`.
- Split behavior into two expectations:
  - parser accounting: consumed/skipped blocks, total pulses, total T-states.
  - playback semantics: EAR level at selected T-states.

**Reality check:** current `0x23` jump and `0x26` call are skipped linearly, so
the harness should initially mark branch semantics as expected-fail or
feature-gated. That makes the known gap visible without blocking unrelated work.

### 5. Contention Timing Harness

**Need:** contended memory is not modeled yet, but it gates the most important
timing tests after floating bus.

**Approach after feature implementation:**

- Add deterministic unit tests around the contention function before using real
  software:
  - address in `0x4000..0x7FFF` during active display window stalls by
    `6,5,4,3,2,1,0,0` depending on T-state phase.
  - non-contended RAM, ROM, and border/retrace periods do not stall.
  - M1, memory read/write, stack, and I/O timing apply stalls at the correct
    sub-operation boundary.
- Add small synthetic programs that perform timed writes to display memory and
  assert final cycle counts.
- Only then promote multicolour/raster demos from "blocked" to pass/fail.

### 6. Floating Bus Calibration Harness

**Need:** the repo has `floating_bus_test`, but `COMPATIBILITY_TEST_PLAN.md`
notes `kFloatingBusReadT` still needs calibration against `fbustest`.

**Approach:**

- Add an external `fbustest` manifest case.
- Capture the expected pass/fail text or screen signature.
- Add a small calibration mode that sweeps `kFloatingBusReadT`-equivalent offset
  if made configurable, then reports the offset range that passes.

**Outcome:** keep the existing deterministic unit test as the pattern lock, and
use `fbustest` as the hardware-calibration oracle.

### 7. Beeper / Audio Regression Harness

**Need:** current beeper tests verify the resampler, not full software-timed
  music output.

**Approach:**

- Add a headless audio capture mode that records beeper PCM over N frames.
- For synthetic fixtures, compare frequency and edge-count metrics exactly.
- For real music engines, store local spectral fingerprints:
  - duration,
  - zero-crossing bands,
  - dominant frequency bands over windows,
  - RMS envelope.

**Why not raw WAV hashes:** tiny timing shifts can produce phase differences
  while still sounding correct. Use metrics first; reserve exact hashes for
  synthetic tests.

## Suggested Implementation Order

1. Refactor `spectrum_probe` logic into a reusable headless test helper so CLI
   and tests share frame driving, typing, screen dumping, and activity windows.
2. Add the manifest runner with skip-on-missing-assets behavior.
3. Add CPU suite cases first: `ZEXDOC`, `z80ccf`, `z80memptr`.
4. Add TZX block coverage for skipped/partial block types.
5. Add `fbustest` as an external calibration case.
6. Implement contention, then add contention unit tests before enabling
   multicolour/raster software cases.
7. Add audio capture metrics after the beeper path is wired through the
   headless machine runner.

## Minimum Acceptance Set

The first stabilization milestone should be:

- CTest remains green with no external assets installed.
- With `Z80_SPEC48_ROM` and CPU-suite assets installed:
  - `ZEXDOC` passes,
  - `z80ccf` passes or reports the exact CPU-revision assumption,
  - `z80memptr` passes or fails with a focused WZ/MEMPTR diagnostic.
- With loader assets installed:
  - four Ultimate loader titles reach title screen or a known stable milestone,
  - Arkanoid Speedlock reaches the interactive title/control-select screen.
- Every run writes structured logs and optional artifacts under `build/`.

## Non-Goals

- Do not commit copyrighted ROMs, tapes, or game-derived goldens.
- Do not treat "loads once manually" as a pass condition.
- Do not use whole-game behavior to diagnose CPU semantics until CPU suites have
  passed; otherwise the failure space is too large.
