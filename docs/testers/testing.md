# Testing

**Audience:** testers and developers validating behavior.
**Last reviewed:** 2026-09-15.
**Source of truth:** [CMakeLists.txt](../../CMakeLists.txt), test sources and the
independent language-tooling package configuration.

## C++ and assembly workflow

Use a separate headless build so GUI settings in an existing build are preserved:

```sh
cmake -S . -B /tmp/z80-headless -DZ80_BUILD_UI=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/z80-headless -j
ctest --test-dir /tmp/z80-headless --output-on-failure
```

For GUI changes, configure with `-DZ80_BUILD_UI=ON`, build `z80_debugger` and
`spectrum`, and verify the relevant native interaction separately. The GUI fetches
dependencies at configure time; headless targets do not need them.

## Registered areas

| Area | Targets / CTest names |
|---|---|
| CPU semantics and flags | `cpu_test`, `rotate_flags_test`, `alu_flags_test`, `word_arithmetic_flags_test`, `register_aliasing_test`, `daa_test`, `inc_dec_flags_test`, `bit_flags_test`, `block_flags_test`, `result_flag_sources_test`, `refresh_register_test`, `interrupt_test` |
| Timing and scheduling | `instruction_timing_test`, `timing_test`, `machine_test` |
| Memory / I/O | `observable_memory_test`, `io_policy_test` |
| Spectrum devices | `screen_decode_test`, `video_test`, `keyboard_test`, `raster_test`, `floating_bus_test`, `tape_test`, `beeper_test` |
| Debugger and shared runtime | `debug_session_test`, `instruction_history_test`, `disassembler_test`, `symbol_table_test`, `spectrum_debug_test` |
| Program launch and ROM boot | `spectrum_program_test`, `spectrum_boot_test` |
| External Pasmo gates | `spectrum_dev_workflow`, `disassembly_assembler`, `disassembly_section` |
| Optional external CPU suites | `cpu_suite_zexdoc`, `cpu_suite_zexall` |

`instruction_history_test` includes byte-access attribution, mutation, prefix
continuation, reset/clear, history eviction and address-listing checks.
`spectrum_program_test` covers placement/readback, policy parity and shared
instruction/frame behavior with synthetic input. `spectrum_dev_workflow` uses
`tests/fixtures/spectrum-dev/`, not the user's editable example program.

## Dependencies and skips

CMake discovers Python 3.9+ and external Pasmo. Supply a pinned executable with
`-DZ80_PASMO_EXECUTABLE=/absolute/path/to/pasmo` if necessary. With Python absent,
the three Python workflow/export gates are not registered. With Pasmo absent,
those tests return skip code 77; the disassembly gates reject unvalidated Pasmo
versions. See [Disassembly Verification](disassembly-verification.md).

Set `Z80_SPEC48_ROM` for ROM-dependent tests/probes. The tests also search local
fallback paths. ROM-absent paths can print SKIP and exit successfully, so CTest
may display Passed without exercising the ROM-specific assertions. Inspect the
output and report whether a ROM was supplied; do not equate a green test count
with ROM coverage. Synthetic portions of `spectrum_debug_test` still run.

Set `Z80_COMPAT_ASSETS` to the root containing `cpu/zexdoc.com` and
`cpu/zexall.com`. Missing cases return 77 and appear as Skipped in CTest.
`cpu_suite_runner` uses built-in cases; `compat/cpu-suites.json` is descriptive,
not a parsed runtime manifest. See [CPU suites](cpu-correctness-suites.md).

## Language server and VS Code client

The LSP requires Python 3.14+ and is independent of CMake:

```sh
cd lsp-z80
PYTHONPATH=src python3 -m unittest discover -s tests -v
```

Use an interpreter meeting that version requirement, or the repository-local
virtual environment created by the [example setup](../../examples/spectrum-dev/README.md).
For the VS Code client:

```sh
cd lsp-z80/editors/vscode
npm ci
npm run compile
LSP_Z80_SERVER_PATH=/absolute/path/to/lsp-z80 npm test
```

Compilation checks TypeScript and the extension manifest. `npm test` is a
separate real Extension Host integration test; compilation alone does not prove
that boundary. See [client README](../../lsp-z80/editors/vscode/README.md).

## Evidence and limits

Record the revision/dirty baseline, build type, compiler, supplied assets,
executed tests, skips and failure logs. Pasmo artifacts live under
`build/disassembly-artifacts/` when using the `build` directory. The
[latest documentation verification](documentation-refresh-verification.md)
records an actual run without replacing older dated acceptance evidence.

Unit tests do not establish broad Spectrum compatibility, full bus fidelity,
complete metadata acceptance or native usability. `performance_benchmark` is
not a CTest threshold gate; use a separate Release build and
[performance guidance](../reference/performance.md). Additional procedures:
[headless instrumentation](headless-instrumentation.md),
[compatibility](compatibility-plan.md), [fault diagnosis](fault-diagnosis.md),
[asset policy](test-assets.md), and [stabilization plan](stabilization-harness-plan.md).
