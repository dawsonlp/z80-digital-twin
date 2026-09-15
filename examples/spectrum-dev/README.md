# Write and run a Spectrum program

Open this folder in VS Code. In `main.asm`, change `color equ red` to
`color equ green`, save, and choose **Terminal → Run Task → Spectrum: Build and
Run**. A fresh Spectrum debugger opens and the program fills its screen and
border with the selected color. Close that window before the next run.

**Spectrum: Build** assembles without launching. **Build and Run** uses the same
external Python CLI, waits for successful assembly and validation, then launches
the resulting binary. Failed assembly never launches an older build.

## One-time setup

From the repository root, build the debugger with GUI support:

```sh
cmake -S . -B build -DZ80_BUILD_UI=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
python3 -m venv lsp-z80/.venv
lsp-z80/.venv/bin/python -m pip install -e lsp-z80
```

The language server requires Python 3.14+. The build/run CLI requires Python
3.9+. Install the [VS Code client](../../lsp-z80/editors/vscode/README.md) from
its VSIX; the example's settings point to the repository-local virtual
environment above. On Windows, set `lsp-z80.server.path` to that environment's
`Scripts/lsp-z80.exe`. This workflow has been exercised on macOS, not Windows.

Install external Pasmo **0.5.5**, following the pinned archive and checksum in
the [verification guide](../../docs/testers/disassembly-verification.md).
Provide your own exact 16 KB Spectrum 48K ROM. No ROM is bundled here.

Supply tool locations in the environment inherited by VS Code and its tasks:

```sh
export Z80_PASMO="/absolute/path/to/pasmo"
export Z80_DEBUGGER="/absolute/path/to/z80-digital-twin/build/z80_debugger"
export Z80_SPEC48_ROM="/absolute/path/to/spec48.rom"
code examples/spectrum-dev
```

An already-running VS Code instance may retain its original environment.
Alternatively set these three variables in VS Code's user setting
`terminal.integrated.env.osx` (or the corresponding Linux/Windows setting).
Recreate existing task terminals after changing their environment. Keep
machine-specific paths out of the committed example settings.

If opening the example within a multi-folder `.code-workspace`, place the
language associations and server path in that workspace's settings as well;
the supported simplest setup is opening this example folder directly.

## Terminal commands

From this folder, with the environment above:

```sh
python3 ../../tools/spectrum_dev.py build
python3 ../../tools/spectrum_dev.py run
python3 ../../tools/spectrum_dev.py build-run
python3 ../../tools/spectrum_dev.py build-run --paused
```

Explicit `--pasmo`, `--debugger` and `--rom` options override the environment.
`--project /path/to/spectrum-project.json` selects a different project directory.
The editor tasks currently target this repository checkout; they are not a
standalone installed SDK.

## Program and launch contract

`spectrum-project.json` selects source, include paths, Pasmo `NAME=value`
defines, origin, entry symbol/address, stack pointer and initial stack reserve.
The default program starts at `$8000`, with SP `$ff00` and a 256-byte reserve
below SP. The assembler's actual emitted range must match the configured origin.
Program bytes and stack reserve must fit in RAM without overlapping, and entry
must lie inside the program. The stack reserve validates startup placement;
it does not prevent a program from overflowing its stack later.

The machine starts with reset CPU registers and interrupts disabled, protected
ROM, zero-initialized RAM outside the loaded program, and no BASIC startup.
This example uses direct ULA/screen writes and an infinite `jp idle` loop. It
does not call ROM routines, enable interrupts or use HALT.

Use **Pause**, **Step**, **Step Over** and **Run** in the debugger. Imported
`start` and `idle` labels appear in disassembly. Windows can be moved/resized;
the Spectrum screen initially occupies the memory panel's central area.
**Reset** still cold-boots the Spectrum ROM and clears the program: use a fresh
build/run to restart your program. Source-line debugging and live reload are
later work.

## Artifacts and failures

Each build attempt gets a directory under `build/`. Successful attempts contain:

- `program.bin`: raw assembled bytes.
- `program.pasmo.sym`: original assembler symbols, including constants.
- `program.debug.sym`: debugger JSON labels, distinguished from Pasmo's format.
- `program.build.json`: input/tool/configuration hashes, emitted artifacts and
  resolved launch state.
- `assembler.stdout.txt` and `assembler.stderr.txt`: listing and diagnostics.

`build/last-build.json` identifies the latest successful artifact set, or marks
the latest attempt as failed/incomplete. Publication replaces that pointer only
after all artifacts have passed checks. Attempts remain inspectable; remove the
example's generated `build/` directory when you no longer need their evidence.
Use one build at a time for a project.

The CLI assembles twice: first to discover inputs through Pasmo's own verbose
and debug output, then to build with checked input hashes and stable outputs.
This covers nested includes and executed INCBIN inputs, including include-path
shadowing. A later `run` rejects changed configuration, inputs or artifacts.
These checks detect ordinary stale builds; they are not a filesystem snapshot
or protection against concurrent adversarial file replacement.

Only listing-confirmed labels within the binary are imported. Constants,
out-of-image symbols and extra aliases at an already-named address are reported
as omitted, and remain available in Pasmo's original symbol output. The entry
name wins when several labels share its address. No function/data semantics
are inferred from names.

Assembly failures show Pasmo's source/line diagnostics in the task terminal.
The Build task also includes a problem matcher. Missing tools, an invalid ROM,
bad placement, an unresolved entry or stale artifacts produce nonzero exits.

## Verification

```sh
cmake -S ../.. -B ../../build -DZ80_PASMO_EXECUTABLE="$Z80_PASMO"
cmake --build ../../build -j
ctest --test-dir ../../build -R 'spectrum_program_test|spectrum_dev_workflow' --output-on-failure
```

The workflow test invokes real Pasmo and runs two color variants through the
CPU/ULA, in directories containing spaces. Other cases verify failed/stale
builds, include search changes, INCBIN dependencies and invalid launch inputs.
Missing Pasmo produces an explicit skip, not a verified pass.
