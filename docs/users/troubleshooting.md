# Troubleshooting

**Audience:** users and contributors.
**Purpose:** resolve common build and runtime failures.
**Last reviewed:** 2026-09-15.

## CMake Fetch Fails

The GUI targets fetch GLFW, ImGui-related dependencies, and audio/UI support on
first configure. If you are offline or only need headless tests:

```bash
cmake -S . -B build -DZ80_BUILD_UI=OFF
```

## ROM Not Found

Spectrum tools do not include ROMs. Pass a ROM path explicitly or set:

```bash
export Z80_SPEC48_ROM=/path/to/spec48.rom
```

ROM-dependent tests skip when no ROM is available.

## Game Or Tape Not Loading

Use the headless probe first:

```bash
./build/spectrum_probe spec48.rom --tape game.tzx --load --frames 12000 --window 1000 --screen
```

If RAM writes stop, PC range is tiny, and the report says `FROZEN`, see
[Fault Diagnosis](../testers/fault-diagnosis.md).

## Build Directory Problems

Use an out-of-source build. If an old build tree is confused by changed options,
create a separate build directory such as `build-debug` or `build-headless`.

## Compiler Errors

Check compiler and standard-library support: the code requires C++23. macOS
Apple Clang is the exercised setup; current CMake flags do not establish MSVC
support. See [Getting Started](getting-started.md).

## macOS Xcode license blocks a build

An unaccepted Xcode license can prevent the selected build tools from starting.
Review it through the normal Xcode setup. If Command Line Tools are already
installed and usable, a per-command selection avoids changing the global setting:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools cmake --build build -j
```

This was sufficient for the September documentation verification. A build cache
that pins another SDK may need a separate configure with the chosen toolchain.

## Source workflow cannot find Pasmo, debugger or ROM

Set `Z80_PASMO`, `Z80_DEBUGGER` and `Z80_SPEC48_ROM` as described in the
[Spectrum example](../../examples/spectrum-dev/README.md), or pass CLI paths.
An already-running VS Code instance and existing task terminal can retain old
environment values. Recreate the task terminal after updating its environment.
The LSP needs Python 3.14+, even though the build/run script only needs 3.9+.

## Build succeeds but the apparent old program still runs

The source workflow starts a fresh debugger process. Close the previous window,
check the generated build manifest and binary hash, and rerun the task. Check
source control flow before concluding the build is stale. Assembly failure
blocks launching an older artifact. **Restart program** inside the Spectrum
UI cold-boots ROM; use the build/run task to reload the standalone RAM binary.

## Address evidence and history differ

The address view keeps the latest observation per start. The history queue drops
older events after 8192 entries. Changed bytes invalidate observation even if
restored; execution and self-modification facts persist until analysis clears.
See [Debugger](debugger.md) for the marker and lifecycle rules.
