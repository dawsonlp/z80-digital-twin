# Testing

**Audience:** testers and developers validating behavior.
**Purpose:** describe the current in-repo test suite and how to run it.
**Last reviewed:** 2026-06-09.
**Source of truth:** `CMakeLists.txt`.

## Run Everything

```bash
cmake -S . -B build -DZ80_BUILD_UI=OFF
cmake --build build -j
ctest --test-dir build
```

The test binaries are registered with CTest. GUI targets are not required for
the headless test suite.

## Current Test Areas

- CPU semantics: `cpu_test`, `rotate_flags_test`, `daa_test`,
  `refresh_register_test`, `interrupt_test`.
- Instruction timing: `instruction_timing_test`.
- Memory and I/O policies: `observable_memory_test`, `io_policy_test`.
- Machine timing and frame loop: `timing_test`, `machine_test`.
- Spectrum video, raster, keyboard, floating bus: `screen_decode_test`,
  `video_test`, `raster_test`, `keyboard_test`, `floating_bus_test`.
- Tape and beeper: `tape_test`, `beeper_test`.
- Debugger core: `debug_session_test`, `disassembler_test`,
  `symbol_table_test`, `spectrum_debug_test`.
- ROM boot smoke: `spectrum_boot_test`.

`spectrum_boot_test` skips cleanly when no 48K ROM is available.

## Optional ROM/Tape Inputs

Set `Z80_SPEC48_ROM` to run ROM-dependent tests and probes without passing the
ROM path each time. Compatibility software and copyrighted assets stay local;
see [Test Assets](test-assets.md).

Set `Z80_COMPAT_ASSETS` to run external CPU suites through
`cpu_suite_runner`. Without it, `cpu_suite_zexdoc` and `cpu_suite_zexall` skip
cleanly.

## What The Unit Suite Does Not Prove

The unit suite verifies many isolated CPU, ULA, tape, and debugger behaviors.
It does not yet prove broad Spectrum compatibility. For that, use:

- [CPU Correctness Suites](cpu-correctness-suites.md)
- [Compatibility Plan](compatibility-plan.md)
- [Stabilization Harness Plan](stabilization-harness-plan.md)
- [Headless Instrumentation](headless-instrumentation.md)
