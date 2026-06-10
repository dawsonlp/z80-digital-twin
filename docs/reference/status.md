# Status

**Audience:** users and developers checking current capability.
**Purpose:** summarize what exists now without repeating the full design docs.
**Last reviewed:** 2026-06-09.

## Working Now

- Z80 CPU core with documented and prefixed instruction support, T-state
  accounting, interrupts, refresh register behavior, and focused unit coverage.
- Compile-time memory and I/O policies:
  `FastMemory`, `ObservableMemory`, `OpenBusIo`, `LatchedIo`,
  `ObservableIo`, and `CallbackIo`.
- Debugger core and ImGui UI with stepping, breakpoints, symbols, disassembly,
  coverage, self-modifying-code detection, blocked ROM-write reporting, and
  Spectrum mode.
- ZX Spectrum 48K machine layer with PAL frame timing, ULA screen rendering,
  keyboard matrix, border, floating bus, tape signal playback, beeper resampling,
  ROM write protection, headless probe, and windowed viewer.
- Headless CTest coverage for CPU behavior, timing, memory/I/O policies,
  Spectrum video/keyboard/tape/beeper/floating bus, and debugger core.

## Known Gaps

- Contended memory timing is not modeled yet.
- Some TZX flow-control blocks are parsed linearly rather than executed as full
  control flow.
- External compatibility suites and games are not yet wrapped in structured
  regression harnesses.
- Audio regression is unit-tested at the resampler level, not yet with
  full-program beeper fingerprints.

## Current Priorities

See [Roadmap](../developers/roadmap.md) and
[Stabilization Harness Plan](../testers/stabilization-harness-plan.md).

Historical snapshot: [status-2026-06-06.md](../archive/status-2026-06-06.md).
