# Z80 Digital Twin

A high-performance Z80 emulator and ZX Spectrum 48K machine/debugging platform
written in modern C++23.

The project has four main surfaces:

- a fast, policy-based Z80 CPU core;
- a headless and windowed ZX Spectrum 48K machine;
- an ImGui debugger with disassembly, breakpoints, coverage, and
  self-modifying-code detection;
- the [`lsp-z80`](lsp-z80/) subproject for Pasmo-oriented Z80 language support
  and its Visual Studio Code client.

## Quick Start

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
./build/gcd_example 1071 462
```

For a headless/offline build that avoids GUI/audio dependencies:

```bash
cmake -S . -B build -DZ80_BUILD_UI=OFF
cmake --build build -j
```

The language server is an independent Python package within this repository:

```sh
cd lsp-z80
PYTHONPATH=src python3 -m unittest discover -s tests -v
```

The GUI debugger and Spectrum viewer use dependencies fetched by CMake on first
configure. ROMs and game tapes are copyrighted and are not included.

## Documentation

Start with [docs/README.md](docs/README.md).

- **Users:** [getting started](docs/users/getting-started.md),
  [Spectrum viewer](docs/users/spectrum-viewer.md),
  [debugger](docs/users/debugger.md), and
  [examples](docs/users/examples.md).
- **Developers:** [architecture](docs/developers/architecture.md),
  [Spectrum machine design](docs/developers/spectrum-machine-design.md),
  [debugger design](docs/developers/debugger-design.md), and
  [roadmap](docs/developers/roadmap.md).
- **Language tooling:** [`lsp-z80`](lsp-z80/README.md) and its
  [VS Code client](lsp-z80/editors/vscode/README.md).
- **Testers/stabilizers:** [testing](docs/testers/testing.md),
  [headless instrumentation](docs/testers/headless-instrumentation.md),
  [compatibility plan](docs/testers/compatibility-plan.md), and
  [stabilization harness plan](docs/testers/stabilization-harness-plan.md).

## Main Binaries

- `gcd_example`: runs a small Z80 GCD program.
- `gcd_stress_test`: throughput stress test.
- `performance_benchmark`: raw CPU benchmark.
- `spectrum_probe`: headless Spectrum instrumentation and tape-loading probe.
- `z80_disassemble`: reconstruct byte-preserving Pasmo source from a raw binary
  range; see [verification and limits](docs/testers/disassembly-verification.md).
- `z80_debugger`: ImGui debugger, optionally in Spectrum mode.
- `spectrum`: ZX Spectrum 48K viewer with keyboard, tape, screen, and beeper.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). The short rule: preserve CPU correctness,
keep machine-specific behavior out of the generic core, and add tests scaled to
the risk of the change.

## License

MIT License. See [LICENSE](LICENSE).
