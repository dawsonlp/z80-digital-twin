# Documentation Reorganization Plan

**Goal:** build a coherent `docs/` tree with explicit readerships:

- **End users:** want to build, run, load ROMs/tapes, use the debugger/viewer,
  and understand what the project does.
- **Developers:** want architecture, design constraints, roadmap context, and
  implementation priorities.
- **Testers/stabilizers:** want repeatable procedures, compatibility assets,
  diagnostics, known gaps, and fault-localization workflows.

The current Markdown set contains valuable material, but several files mix
audiences, some are historical snapshots, and some time-sensitive claims are now
stale or duplicated. The reorganization should preserve evidence and decisions
while making the main paths shorter.

## Proposed Tree

```text
docs/
  README.md                         # documentation index and audience router

  users/
    getting-started.md              # build, binaries, first runs
    spectrum-viewer.md              # ROM/tape loading, keyboard, sound, shots
    debugger.md                     # using the ImGui debugger
    examples.md                     # gcd, stress test, performance benchmark
    troubleshooting.md              # common build/runtime problems

  developers/
    architecture.md                 # stable system architecture
    spectrum-machine-design.md      # ZX Spectrum machine/ULA design
    debugger-design.md              # debugger architecture
    reverse-engineering-roadmap.md  # debugger/reassembler roadmap
    roadmap.md                      # current implementation roadmap
    decisions.md                    # durable ADR-style decisions
    contributing.md                 # contributor workflow and standards

  testers/
    testing.md                      # how to run unit/integration tests
    headless-instrumentation.md     # probe and DebugSession recipes
    compatibility-plan.md           # external programs and feature gates
    stabilization-harness-plan.md   # harnesses to add
    fault-diagnosis.md              # symptom -> likely subsystem -> tools
    test-assets.md                  # local asset layout, env vars, copyright policy

  reference/
    performance.md                  # measured performance and benchmark method
    status.md                       # generated or manually refreshed status snapshot

  archive/
    early-todo.md
    early-test-results.md
```

Keep the root `README.md`, but make it a short project landing page plus links
into `docs/`. The root should not carry full architecture, old performance
claims, full program manuals, and tester recipes at once.

## Navigation Model

`docs/README.md` should answer:

- "I want to run it" -> `users/getting-started.md`
- "I want to use the Spectrum emulator" -> `users/spectrum-viewer.md`
- "I want to debug/reverse-engineer code" -> `users/debugger.md`
- "I want to contribute implementation work" -> `developers/architecture.md`
- "I want to stabilize correctness" -> `testers/testing.md` and
  `testers/compatibility-plan.md`
- "I want the historical rationale" -> `developers/decisions.md` and `archive/`

Each document should begin with:

- audience,
- purpose,
- last-reviewed date,
- source of truth if the document is derived from code/tests,
- links to next likely documents.

## Per-File Disposition

| Current file | Current role | Problem | Destination |
|---|---|---|---|
| `README.md` | Landing page, build guide, program guide, architecture, testing, performance, future work | Too many audiences; duplicates `STATUS.md`, `TESTING.md`, examples, and design docs | Keep root as concise landing page. Move detailed sections into `docs/users/getting-started.md`, `docs/users/*.md`, `docs/reference/performance.md`, and `docs/README.md` |
| `ARCHITECTURE.md` | Stable platform architecture and policy model | Mostly good; should be treated as developer reference | Move to `docs/developers/architecture.md`; update links |
| `SPECTRUM_DESIGN.md` | ZX Spectrum machine/ULA design | Valuable but mixes completed work, pending questions, and ordered next steps | Move to `docs/developers/spectrum-machine-design.md`; split live roadmap items into `docs/developers/roadmap.md` |
| `DEBUGGER_DESIGN.md` | Debugger architecture/design | Valuable developer reference; long but coherent | Move to `docs/developers/debugger-design.md`; add current implementation status note |
| `DEBUGGER_ROADMAP.md` | Reverse-engineering lab vision | Good roadmap but not the whole project roadmap | Move to `docs/developers/reverse-engineering-roadmap.md` |
| `STATUS.md` | Time-sensitive snapshot | Dated 2026-06-06 and already contradicts later Spectrum/tape/floating-bus work in places | Move to `docs/reference/status.md`; mark as snapshot or replace with a refreshed, short current status |
| `TODO.md` | Early development TODO and assembler notes | Partially superseded; contains possibly stale marketplace/tool claims | Move still-relevant assembler/binary-loader material into `docs/developers/roadmap.md`; archive the rest as `docs/archive/early-todo.md` |
| `TESTING.md` | Early test results | Stale: says 10 suites and old build commands, while CMake now registers many tests | Archive as `docs/archive/early-test-results.md`; create new `docs/testers/testing.md` from current CTest/CMake reality |
| `COMPATIBILITY_TEST_PLAN.md` | External compatibility battery and feature gates | Good tester doc; should live with harness/stabilization docs | Move to `docs/testers/compatibility-plan.md` |
| `STABILIZATION_HARNESS_PLAN.md` | Proposed harnesses for compatibility stabilization | Good tester/developer bridge | Move to `docs/testers/stabilization-harness-plan.md` |
| `HEADLESS_INSTRUMENTATION.md` | Probe recipes and DebugSession instrumentation | Strong tester/operator doc | Move to `docs/testers/headless-instrumentation.md`; cross-link from users/debugger |
| `FLOATING_BUS_DESIGN.md` | Deep design/debugging record for floating bus | Useful as developer decision record plus tester calibration context | Split: concise decision summary into `docs/developers/decisions.md`; keep full record as `docs/developers/floating-bus-design.md` or `docs/testers/floating-bus-calibration.md` |
| `PERFORMANCE_ANALYSIS.md` | Performance claims and early insights | Duplicates README/examples/design decisions; some language is promotional | Move calibrated, reproducible benchmark data into `docs/reference/performance.md`; archive speculative sections if retained |
| `design_decisions.md` | Early design/performance/assembly notes | Mixed ADRs, performance claims, and speculation | Convert durable decisions into ADR-style entries in `docs/developers/decisions.md`; move obsolete parts to archive |
| `CONTRIBUTING.md` | Contributor guide | Build/test commands are stale (`make`, `./cpu_test` from root); tone and requirements need alignment with current CMake/CTest | Move to `docs/developers/contributing.md`; leave root `CONTRIBUTING.md` as a short pointer or keep updated root copy for GitHub convention |
| `examples/README.md` | Examples guide plus performance/educational pitch | Duplicates README and performance docs | Move factual usage into `docs/users/examples.md`; reduce `examples/README.md` to a local index or pointer |

## New Documents To Create

### `docs/testers/fault-diagnosis.md`

Purpose: reduce time from symptom to subsystem.

Initial sections:

- CPU suite fails -> CPU flags, undocumented behavior, MEMPTR/WZ, refresh `R`.
- Tape loads header then freezes -> TZX parser/pulse train/EAR timing.
- Border stripes stop -> loader waiting for signal or wrong block flow.
- Game reaches title but fails in play -> floating bus, contention, keyboard,
  or interrupt timing depending on title.
- Multicolour/raster tearing -> contention and beam timing.
- Audio pitch/tempo wrong -> CPU timing or beeper edge resampling.
- ROM writes appear as SMC -> check blocked-write vs committed write.

Each row should identify:

- observable signal,
- likely subsystem,
- command/harness to run,
- useful artifact,
- next disconfirming test.

### `docs/testers/test-assets.md`

Purpose: make local-only copyrighted/free test assets predictable.

Define:

- environment variables: `Z80_SPEC48_ROM`, `Z80_COMPAT_ASSETS`,
  `Z80_COMPAT_GOLDENS`;
- expected local layout for CPU suites, tapes, snapshots, and goldens;
- skip semantics when assets are absent;
- copyright policy: do not commit ROMs, tapes, or game-derived goldens;
- how to add a new manifest case.

### `docs/users/troubleshooting.md`

Purpose: answer practical failures without forcing users through design docs.

Include:

- UI dependencies and `-DZ80_BUILD_UI=OFF`;
- first-configure network requirement for GUI dependencies;
- missing ROM/tape behavior;
- out-of-source build requirement;
- common CMake/compiler version problems;
- where artifacts/screenshots/logs are written.

### `docs/developers/roadmap.md`

Purpose: one current implementation roadmap.

It should be short and reality-linked:

- now: external compatibility harnesses and CPU suites;
- next: contention model and calibration tests;
- later: TZX branch/call flow blocks, audio regression harness, RE L3+.

Avoid preserving old completed tasks as "next."

## Migration Phases

### Phase 1: Add Structure Without Moving Content

- Create `docs/README.md`.
- Add the target tree with stub files for the three audiences.
- Add "This content is moving to..." banners to root docs that will migrate.
- Keep all existing links working.

This phase has low risk and gives contributors a destination map.

### Phase 2: Move Stable Documents

Move mostly coherent docs first:

- `ARCHITECTURE.md` -> `docs/developers/architecture.md`
- `DEBUGGER_DESIGN.md` -> `docs/developers/debugger-design.md`
- `DEBUGGER_ROADMAP.md` -> `docs/developers/reverse-engineering-roadmap.md`
- `COMPATIBILITY_TEST_PLAN.md` -> `docs/testers/compatibility-plan.md`
- `HEADLESS_INSTRUMENTATION.md` -> `docs/testers/headless-instrumentation.md`
- `STABILIZATION_HARNESS_PLAN.md` -> `docs/testers/stabilization-harness-plan.md`

Leave root redirect stubs temporarily if external links matter.

### Phase 3: Rewrite Mixed/Stale Documents

- Replace `TESTING.md` with a current `docs/testers/testing.md`.
- Replace `STATUS.md` with a short current status or mark it explicitly as a
  dated snapshot.
- Collapse duplicated performance claims into `docs/reference/performance.md`.
- Convert `TODO.md` and `design_decisions.md` into roadmap/decision/archive
  content.
- Trim `README.md` to landing page + quick start + docs index.
- Refresh `CONTRIBUTING.md` commands and testing expectations.

This phase should involve content edits, not only file moves.

### Phase 4: Enforce Coherence

- Add a link checker or at least a simple CI script for Markdown links.
- Add a documentation freshness rule:
  - status/roadmap docs must carry `Last reviewed`;
  - generated facts such as test counts should either be generated or omitted;
  - docs must name their audience.
- Add a `docs/testers/compat/` manifest schema once the compatibility runner
  exists.

## Source-Of-Truth Rules

- Build commands live in one user-facing getting-started doc and are linked
  elsewhere.
- Test inventory lives in `docs/testers/testing.md`; design docs should not list
  every current test unless necessary.
- Current project state lives in one status/roadmap pair.
- Deep design records can be long, but their current decision must be summarized
  at the top.
- Compatibility expectations belong to tester docs, not the root README.
- Copyright-sensitive assets are always local-only and documented through
  `test-assets.md`.

## Recommended First Commit

The first implementation commit should only create the structure and move the
least controversial docs:

1. Add `docs/README.md`.
2. Move `ARCHITECTURE.md`, `DEBUGGER_DESIGN.md`, `DEBUGGER_ROADMAP.md`,
   `HEADLESS_INSTRUMENTATION.md`, `COMPATIBILITY_TEST_PLAN.md`, and
   `STABILIZATION_HARNESS_PLAN.md`.
3. Update root `README.md` links.
4. Add root redirect stubs for moved files if preserving links matters.

Do not rewrite stale docs in the same commit. That would mix mechanical moves
with judgment calls and make review harder.
