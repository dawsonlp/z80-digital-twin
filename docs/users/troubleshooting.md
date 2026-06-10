# Troubleshooting

**Audience:** users and contributors.
**Purpose:** resolve common build and runtime failures.
**Last reviewed:** 2026-06-09.

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

Check compiler support first: the code requires C++23. Use Clang 16+, GCC 13+,
or MSVC 2022+.
