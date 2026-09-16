# Contributing

**Audience:** developers contributing code or docs.
**Purpose:** describe the current workflow and quality bar.
**Last reviewed:** 2026-09-15.

## Build And Test

```bash
cmake -S . -B build -DZ80_BUILD_UI=OFF
cmake --build build -j
ctest --test-dir build
```

Use the GUI build when changing `z80_debugger` or `spectrum`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DZ80_BUILD_UI=ON
cmake --build build -j
```

## Engineering Rules

Follow the [C++ project conventions](cpp-standards.md), including the agreed
register-union exception.

- Keep machine-specific behavior out of the generic CPU core unless the behavior
  is genuinely Z80 behavior.
- Prefer the existing policy/device/debugger boundaries.
- Add focused tests for CPU flags, timing, memory/I/O behavior, and machine
  behavior when touched.
- Keep external ROM/tape assets out of the repository.
- Avoid broad rewrites bundled with behavioral fixes.

## Documentation

Docs are split by audience. Put operational instructions under `docs/users/`,
architecture and roadmap material under `docs/developers/`, and stabilization
procedures under `docs/testers/`.

Time-sensitive docs should include a `Last reviewed` line. Current behavior
belongs in status, user guides and architecture; proposals and dated verification
must be labeled separately. Preserve superseded rationale in the archive and
check relative links when moving it. Use [Testing](../testers/testing.md) for
the separate C++, Pasmo, LSP and VS Code validation boundaries.

## Pull Request Checklist

- Build passes.
- Relevant CTest targets pass.
- Optional ROM/tape cases are documented if they were used.
- Performance-sensitive changes include a benchmark note when appropriate.
- Public behavior changes update docs.
