# Main integration verification — 16 September 2026

Scope: integrate `feature/spectrum-dev-loop` (including `dev`) into main before
starting deterministic execution analysis. Source baseline tested: `a5e3c3d`
plus the durable symbol/semantic export working-tree changes committed as `0e6d445` immediately
after these checks. Subsequent integration edits are documentation only.

## Fresh validation

Host: macOS, Apple clang 21.0.0, configured C++23.

| Boundary | Result |
|---|---|
| Debug/UI build (`build`) | Passed; no compiler warning/error matches in build log |
| Debug/UI CTest | 38 passed, 2 optional ZEX tests skipped (40 registered) |
| Fresh Release/headless build (`/private/tmp/z80-merge-release`) | Passed; no compiler warning/error matches in build log |
| Release/headless CTest | 37 passed, 2 optional ZEX tests skipped (39 registered) |
| Pasmo workflow and semantic export fixtures | Executed in both CTest configurations with local Pasmo 0.5.5 |
| Full local Spectrum ROM semantic export | New project with RESET, PRINT_A and CALL_JUMP labels; reassembled all 16384 bytes identically |
| Native debugger smoke | Five frames rendered successfully; screenshot produced |
| LSP Python suite | 18 tests passed |
| VS Code extension build | Type/manifest checks and compilation passed |
| Real VS Code Extension Host | One integration test passed; server activation/language features exercised |
| Working-tree whitespace | `git diff --check` passed |

ROM supplied explicitly through `Z80_SPEC48_ROM` for both CTest runs:
SHA-256 `d55daa439b673b0e3f5897f99ac37ecb45f974d1862b4dadb85dec34af99cb42`.
No ROM, tape or reconstructed ROM source is added to Git.

Local diagnostic logs are `/private/tmp/z80-merge-{debug,release}-build.log`,
`/private/tmp/z80-merge-{debug,release}-tests.log`,
`/private/tmp/z80-merge-lsp-tests.log`, `/private/tmp/z80-merge-extension.log`,
`/private/tmp/z80-merge-extension-tests.log`, and `/private/tmp/z80-merge-native.log`.
The native screenshot is `/private/tmp/z80-merge-native.ppm`; ROM round-trip
artifacts are `/private/tmp/z80-merge-rom-xcwaca91/`. These temporary paths are
local evidence locations, not durable distributed artifacts.

## Explicit follow-ups, not implied acceptance

The interactive native save/reopen/rename walkthrough remains unverified; the
rendering smoke does not establish that interaction. Metadata auditing/resource
acceptance and HALT/interrupt-signal fidelity remain as documented in their
existing checklists. Automatic runtime evidence capture is not implemented.
Storage uses the documented POSIX boundary and has not gained a portability
guarantee from these tests. No broad Spectrum compatibility claim is made.

These known limitations remain follow-up work rather than blockers to integrating
the tested baseline. No deterministic-analysis implementation is included.
