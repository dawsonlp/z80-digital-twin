# Changelog

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
