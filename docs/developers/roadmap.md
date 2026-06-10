# Roadmap

**Audience:** developers choosing implementation work.
**Purpose:** keep current priorities separate from historical TODOs.
**Last reviewed:** 2026-06-10.

## Recently completed

- CPU correctness now has a headless suite runner and manifest-backed external
  tests. `ZEXDOC` and `ZEXALL` pass when the local compatibility assets are
  present.
- The main CPU flag classes exercised by ZEX are covered by focused unit tests,
  so future regressions should fail in small tests before the full exercisers.
- The documentation tree now separates user, developer, tester, reference, and
  archive material.

## Now

### Development workbench

- Add a source-to-machine loop: edit Z80 assembly, invoke a configured assembler,
  load the produced bytes directly into the running machine, import generated
  labels/symbols, and optionally set PC/SP/register state before running.
- Add binary insert support: load raw bytes into a chosen address range, record
  provenance, expose the changed range in memory/disassembly, and optionally set
  CPU state from a sidecar or UI form.
- Add session state save/load for debugger work: CPU registers, RAM image or
  patches, breakpoints, symbols, annotations, selected machine model, tape
  position, and relevant UI/debugger settings.
- Treat assembler choice and paths as configuration, not hard-coded behavior.
  Pick one default dialect first, then keep room for others.

### Reverse engineering

- Promote the existing coverage and self-modifying-code tracking into a durable
  project model: execution starts, write events, user labels, comments, data
  regions, and generated symbols.
- Add flow tracking beyond isolated branch targets: basic blocks, call edges,
  jump edges, returns, hot paths, and observed entry points.
- Extend symbol discovery: auto-label branch targets, detect likely functions,
  identify pointer tables and strings where evidence supports it, and preserve
  user overrides.
- Build the RAM-to-source path: capture a memory image, classify code/data from
  execution evidence, render a documented listing, export reassemblable assembly,
  reassemble it, and compare bytes back to the captured image.

### User interface and formats

- Add settings/configuration UI for machine model, ROM path, compatibility asset
  paths, assembler toolchain, keyboard/tape/audio/video options, timing knobs,
  and debugger behavior.
- Make load/save flows complete and visible: ROM, TAP/TZX, raw binary inserts,
  symbols, project/session files, screenshots, and CPU/machine state.
- Add tape streaming controls that expose playback state directly: insert/eject,
  play/pause, rewind/seek where the format supports it, current block/pulse
  diagnostics, and save/restore of tape position in sessions.
- Keep command-line equivalents for important UI flows so testers can reproduce
  failures headlessly.

## Next

- Maintain the CPU-suite harness and add adapters/assets for `z80ccf`,
  `z80memptr`, and Spectrum-native CPU edge suites as they become available.
- Expand synthetic TZX block coverage, especially currently linearized flow
  blocks.
- Add TZX jump/call/return flow semantics where needed by real loaders.
- Implement contended memory timing.
- Add deterministic contention unit tests before promoting multicolour/raster
  software to compatibility acceptance.
- Calibrate floating-bus timing against `fbustest`.
- Add golden screen/artifact capture for free or local-only fixtures.

## Later

- Add richer project packaging for reverse-engineering sessions, including
  assembler outputs, source files, symbol maps, RAM captures, and verification
  reports.
- Support additional assembler dialects after one end-to-end path is reliable.
- Add snapshot import/export formats once the internal CPU/machine-state model is
  stable.
- Add headless beeper/audio regression metrics.

Historical TODO material is archived in [../archive/early-todo.md](../archive/early-todo.md).
