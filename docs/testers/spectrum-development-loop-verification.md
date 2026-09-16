# Spectrum development loop verification

**Verified:** 13–14 September 2026, macOS arm64.  
**Branch:** `feature/spectrum-dev-loop`, based on `98cf7fb`.  
**Scope:** standalone Spectrum 48K RAM programs, external Pasmo, installed VS
Code language client, debugger launch and visible edit/build/run loop.

## Result

The first development loop works: edit `main.asm`, assemble from a VS Code
task, launch a fresh Spectrum debugger, see the result, close the debugger,
edit and repeat. The test changed `color equ red` to `color equ green` and
observed the screen change from red to green. The example was restored to red.

Completion offered `green` as a workspace Pasmo symbol. F12 from `red` in
`main.asm` opened its definition in `palette.inc`. The installed extension's
bundle hash matched the newly packaged repository client exactly.

The debugger displayed imported `start` and `idle` labels, with PC at `$8012`
in the idle loop. Pause stopped execution; one Step advanced the idle `JP`
by 10 T-states; Run resumed execution. These observations establish the
example's execution controls, not general timing fidelity.

An intentional `missing_color` reference produced Pasmo's line-4 error and
task exit code 1, without launching a debugger. The valid source was restored
and rebuilt successfully. The same external CLI also built the restored source
and the resulting program passed the GUI smoke/render check.

## Tools and artifacts

- AppleClang 21.0.0.21000101, C++23 Debug build with GUI enabled.
- Python 3.14.6; language server 0.1.0 installed in `lsp-z80/.venv`.
- VS Code 1.137.0 arm64; installed `lsp-z80.lsp-z80` 0.1.0.
- Pasmo 0.5.5, built from the pinned official archive after SHA-256 validation.
- User-supplied 16384-byte Spectrum 48K ROM; no ROM committed.

| Variant | Binary size | SHA-256 |
|---|---|---|
| Red | 21 bytes | `0c59c662467c87951541a686b71d3079da6e0e5db04b5a38d7bb90a0480e9160` |
| Green | 21 bytes | `e90f69679d98376c43666ea6c4401ef0eaef7a9085d906aeca5ccc4a54d994d0` |

Origin and entry were `$8000`; SP was `$ff00`, with a 256-byte reserve.
Both variants' full attribute areas were checked headlessly against the chosen
color, using the real assembled binaries and CPU/ULA execution.

Local generated evidence is under `build/dev-loop-evidence/`: `ctest.txt`,
`cli-report.json`, `verify_cli.py` and `final-red.png`. These are local artifacts,
not committed fixtures. Example build records and listings remain under
`examples/spectrum-dev/build/`.

For native UI automation only, the compiled debugger was copied into a local
macOS `.app` wrapper so the UI tool could identify its window. The wrapper added
no program logic. The final CLI compatibility and screenshot checks used the
ordinary `build/z80_debugger` executable directly. A configured local workspace
at `build/spectrum-dev.code-workspace` points to the ordinary executable.

## Automated results

- **33 CTest checks passed; 2 skipped.** The skipped tests were ZEXDOC and
  ZEXALL, whose external assets were unavailable. The ROM-backed Spectrum
  boot/debug tests ran with `Z80_SPEC48_ROM` explicitly set.
- Both Pasmo disassembly gates ran and passed, including independent assembler
  encodings and byte-preserving disassembly/reassembly.
- The new workflow test passed eight cases, including two-color execution,
  failed builds, stale configuration/includes, include-path shadowing,
  INCBIN changes, tampered binaries, invalid origin/entry/stack, missing
  tools/ROM, malformed symbols and subprocess paths containing spaces.
- The headless launch test checked invalid-load non-mutation, write protection,
  exact byte loading, PC/SP/interrupt state, clean host-load evidence and
  execution controls.
- **18 Python language-server tests passed.** VSIX type/manifest checks and
  packaging passed; real editing behavior was verified in the installed client.
- Ten final CLI cases passed: missing stack, overflowing origin, ROM overlap,
  malformed symbols and missing ROM failed as expected; GCD, SMC, generic
  binary, ROM-only and standalone Spectrum program smoke runs succeeded.

## Limits

This is a fresh-process, standalone launch. BASIC/ROM-dependent initialization,
tape packaging, live reload, source-line debugging, instruction histories and
runtime-informed reconstruction remain deferred. Reset still cold-boots the
ROM. The example avoids HALT and long prefix chains; known runtime concerns in
those areas remain outside this evidence. Windows/Linux were not verified.

Build evidence uses checked hashes and a two-assembly dependency discovery
process, not an atomic filesystem snapshot. Run one build per project at a
time. The current debugger retains only one imported name per address and the
adapter reports omitted aliases/constants rather than inventing semantics.
