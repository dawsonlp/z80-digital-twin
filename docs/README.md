# Documentation

**Audience:** users, developers, and testers.
**Purpose:** route readers to the smallest document that answers their question.
**Last reviewed:** 2026-06-09.

## Users

- [Getting Started](users/getting-started.md): build, run tests, and launch the
  main binaries.
- [Spectrum Viewer](users/spectrum-viewer.md): run the ZX Spectrum 48K viewer,
  load tapes, use keyboard mapping, and take screenshots.
- [Debugger](users/debugger.md): use `z80_debugger` for binaries and Spectrum
  sessions.
- [Examples](users/examples.md): GCD examples, stress tests, and benchmarks.
- [Troubleshooting](users/troubleshooting.md): common build/runtime failures.

## Developers

- [Architecture](developers/architecture.md): core engine, machine layer,
  debugger layer, and policy model.
- [Spectrum Machine Design](developers/spectrum-machine-design.md): ULA,
  timing, keyboard, tape, sound, and ROM behavior.
- [Debugger Design](developers/debugger-design.md): debug session, UI panels,
  symbols, breakpoints, and SMC detection.
- [Reverse-Engineering Roadmap](developers/reverse-engineering-roadmap.md):
  coverage, annotations, export, and round-trip verification.
- [Roadmap](developers/roadmap.md): current implementation priorities.
- [Decisions](developers/decisions.md): durable architectural decisions.
- [Contributing](developers/contributing.md): workflow and expectations.

## Testers And Stabilizers

- [Testing](testers/testing.md): CTest, unit tests, optional ROM-dependent tests.
- [CPU Correctness Suites](testers/cpu-correctness-suites.md): plan for running
  external Z80 exercisers headlessly.
- [Headless Instrumentation](testers/headless-instrumentation.md): probe recipes
  and `DebugSession`-driven diagnosis.
- [Compatibility Plan](testers/compatibility-plan.md): external software that
  exposes subtle CPU, ULA, loader, and timing faults.
- [Stabilization Harness Plan](testers/stabilization-harness-plan.md): harnesses
  still needed to make compatibility testing repeatable.
- [Fault Diagnosis](testers/fault-diagnosis.md): symptoms mapped to likely
  subsystems and tests.
- [Test Assets](testers/test-assets.md): local asset layout and copyright policy.

## Reference And Archive

- [Status](reference/status.md): dated project status snapshot.
- [Performance](reference/performance.md): how to measure performance now.
- [Archive](archive/): superseded TODOs, early test results, historical status,
  performance analysis, and migration notes.
