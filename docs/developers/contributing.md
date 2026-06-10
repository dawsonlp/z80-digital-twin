# Contributing

**Audience:** developers contributing code or docs.
**Purpose:** describe the current workflow and quality bar.
**Last reviewed:** 2026-06-09.

## Build And Test

```bash
cmake -S . -B build -DZ80_BUILD_UI=OFF
cmake --build build -j
ctest --test-dir build
```

Use the GUI build when changing `z80_debugger` or `spectrum`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Engineering Rules

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

Time-sensitive docs should include a `Last reviewed` line.

## Pull Request Checklist

- Build passes.
- Relevant CTest targets pass.
- Optional ROM/tape cases are documented if they were used.
- Performance-sensitive changes include a benchmark note when appropriate.
- Public behavior changes update docs.
