# Documentation refresh verification

**Date:** 15 September 2026.
**Scope:** reconcile documentation with the current implementation and verify the
pre-existing address-metadata changes being committed alongside it.
**Source baseline:** `feature/spectrum-dev-loop`, commit `b94fa00` (address metadata),
built and tested before committing with the same source contents. Its parent is
checkpoint `a3dd462`. Project version remains 1.0.3; no release
or exhaustive metadata acceptance is claimed.

## Environment and build

macOS arm64, Apple Clang 21.0.0 (`clang-2100.3.34.2`), C++23, Debug configuration.
The existing `build` directory has GUI targets enabled. The initially selected
Xcode tools stopped at an unaccepted license; using the installed Command Line
Tools per command completed the build without changing global configuration:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools cmake --build build -j 8
```

A separate headless configure/build also succeeded:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools cmake -S . \
  -B /tmp/z80-doc-headless -DZ80_BUILD_UI=OFF -DCMAKE_BUILD_TYPE=Debug \
  -DZ80_PASMO_EXECUTABLE="$PWD/build/tools/pasmo-0.5.5/pasmo"
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
  cmake --build /tmp/z80-doc-headless -j 8
```

Pasmo is a pre-existing local 0.5.5 executable, not a bundled artifact. The ROM
was supplied locally at `roms/spec48.rom`, 16,384 bytes, SHA-256:

```text
d55daa439b673b0e3f5897f99ac37ecb45f974d1862b4dadb85dec34af99cb42
```

## Results

| Check | Result | Boundary |
|---|---|---|
| GUI-enabled Debug build | Passed | Includes rebuilt debugger and viewer. |
| Separate headless Debug build | Passed | No GUI dependency configuration needed. |
| CTest in each build | 34 passed, 2 skipped, 36 registered | ROM supplied; ZEXDOC/ZEXALL assets unavailable. |
| Pasmo gates | Passed within both CTest runs | Workflow fixture, independent encoding cases and byte-round-trip cases. |
| Python LSP unit tests | 18 passed | Repository-local Python 3.14 environment. |
| VS Code `npm run compile` | Passed | TypeScript, manifest and bundle; not an Extension Host run. |
| Current CLI help | Passed | Debugger, spectrum development script, disassembler; CPU runner cases also listed. |
| Full local ROM export/reassembly | Exact byte match | Raw 16 KiB range at origin zero, Pasmo 0.5.5; not semantic source recovery. |
| Spectrum debugger smoke | Exit 0, screenshot inspected | Five emulated frames then one instruction; five UI frames rendered. |
| Standalone fixture build/GUI launch | Exit 0 | 21-byte Pasmo output, symbols, explicit entry/stack, 20 instructions and smoke render. |
| Documentation paths / whitespace | Passed | Local Markdown file targets and `git diff --check`; external URLs were not revalidated. |

CTest commands:

```sh
Z80_SPEC48_ROM="$PWD/roms/spec48.rom" ctest --test-dir build --output-on-failure
Z80_SPEC48_ROM="$PWD/roms/spec48.rom" \
  ctest --test-dir /tmp/z80-doc-headless --output-on-failure
```

The instruction-history test reports 25,690,112 bytes of fixed byte-activity
storage and exercises the long-loop retention case. That number excludes the
other analysis structures and allocator overhead. It is not a total-memory or
execution-overhead measurement.

The LSP check used:

```sh
lsp-z80/.venv/bin/python -m unittest discover -s lsp-z80/tests -v
```

`npm run compile` ran in `lsp-z80/editors/vscode` with existing dependencies.
The separate real Extension Host test was not rerun in this refresh.

## Export and GUI boundaries

The raw ROM check ran `z80_disassemble --org 0`, assembled its output with
`pasmo --bin`, and compared the complete output with `cmp`. Both SHA-256 values
matched the ROM hash above. ROM bytes and reconstructed source remain local.

The rebuilt debugger smoke command was:

```sh
./build/z80_debugger --spectrum roms/spec48.rom --run 5 --steps 1 \
  --smoke --shot /tmp/z80-doc-spectrum.ppm
```

It required execution outside the filesystem/process sandbox to connect to the
macOS window server. The screenshot shows PAUSED state, registers, a Spectrum
screen, memory-address disassembly with RO/X/O markers, and the Restart program
and Clear analysis controls. It does not establish completion of ROM boot or
interaction correctness. The default panel arrangement overlaps some panels;
this smoke check is not a layout/usability acceptance.

A copy of `tests/fixtures/spectrum-dev` under `/tmp` was built through
`tools/spectrum_dev.py build`. The emitted 21-byte binary has SHA-256:

```text
0c59c662467c87951541a686b71d3079da6e0e5db04b5a38d7bb90a0480e9160
```

The real debugger accepted that binary with its generated `.debug.sym`, protected
ROM, origin/entry `$8000`, SP `$ff00`, a 256-byte reserve, `--steps 20`, and
`--smoke`. The editable example source was not changed.

Native UI automation timed out before fresh interactive checks could be
established. Scrolling, tooltip expansion, resizing and clicking clear/restart
are therefore not newly verified here. Their source contracts and focused
headless tests were inspected; earlier native evidence remains separately dated
in the [address checklist](../developers/address-metadata-checklist.md).

## Retained evidence and open acceptance

Local logs and the inspected screenshot are under
`build/documentation-verification-2026-09-15/` (ignored, not portable evidence).
This committed record captures commands, results, hashes and limits. No external
ROMs, reconstructed ROM source, game tapes or generated binaries are committed.

No new Release performance comparison, exhaustive access-accounting/overflow
audit, full allocator measurement, persistence acceptance or HALT/interrupt
fidelity correction was performed. Those remain in the
[address checklist](../developers/address-metadata-checklist.md) and
[current roadmap](../developers/roadmap.md).
