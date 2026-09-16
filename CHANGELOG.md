# Changelog

## Unreleased

### Added

- Independent Pasmo-oriented LSP and VS Code client.
- External Pasmo Spectrum build/run workflow with symbols, artifact identity,
  validated standalone RAM/stack launch and failed-build blocking.
- Byte-preserving `z80_disassemble` CLI and independent assembler/round-trip gates.
- Bounded instruction evidence, full address browsing and recent-history views.
- Address-owned latest observations independent of history eviction, byte-activity
  summaries, sticky execution/self-modification markers and architectural vector labels.
- Core CPU-only reset and clear-analysis operations; Clear analysis UI control.

### Changed

- CPU-owned bounded whole-instruction stepping and shared Spectrum instruction/frame
  lifecycle across viewer, debugger and probe.
- Same-value writes preserve evidence validity and do not count as value-changing SMC.
- Documentation now describes current types, workflows, retention and lifecycle;
  superseded designs/handoffs are retained as historical records.

### Validation and limits

- See [15 September verification](docs/testers/documentation-refresh-verification.md)
  for fresh build/test results and skipped external/native boundaries.
- Address-metadata acceptance remains partial. Analysis persistence, live reload,
  HALT/interrupt fidelity corrections and contention remain future work.
- No release/version bump is implied; the project version remains 1.0.3.


## v1.0.3 - 2026-06-12

### Changed

- Cleaned up prefixed-instruction T-state accounting so CB, ED, DDCB, and FDCB
  handlers add only the cycles remaining after already-fetched prefix bytes.
- Split I/O-visible `OUT` timing to match the existing `IN` timing model, so
  device callbacks observe the intended I/O M-cycle while preserving instruction
  totals.
- Updated the floating-bus timing design notes to match the current core timing
  contract.

### Verified

- Full CTest suite passes with local unit tests.
- ZEXALL passed through `cpu_suite_runner` using the local compatibility asset.

## v1.0.2 - 2026-06-10

### Fixed

- Added a headless CP/M `.COM` CPU-suite runner for ZEXDOC/ZEXALL compatibility tests.
- Fixed documented 8-bit ALU flag behavior, including `DAA`, `CPL`, `SCF`, `CCF`, `CP`, carry-in arithmetic, and logical operation half-carry handling.
- Fixed full Z80 flag fidelity for ZEXALL-covered instruction families:
  - 16-bit `ADD`, `ADC`, and `SBC` arithmetic.
  - 8-bit `INC` and `DEC`.
  - `BIT` register, `(HL)`, and indexed memory forms.
  - Block transfer and compare instructions: `LDI`, `LDD`, `CPI`, `CPD`, and repeat forms.
  - `NEG`, `RRD`, `RLD`, accumulator rotates, and CB-prefixed shift/rotate operations.

### Added

- Added focused CTest targets for CPU flag regression coverage:
  - `alu_flags_test`
  - `word_arithmetic_flags_test`
  - `inc_dec_flags_test`
  - `bit_flags_test`
  - `block_flags_test`
  - `result_flag_sources_test`
- Added external CPU suite CTest registrations:
  - `cpu_suite_zexdoc`
  - `cpu_suite_zexall`

### Verified

- Full test suite passes with compatibility assets:
  - `30/30` tests passed.
  - ZEXDOC passed.
  - ZEXALL passed.
