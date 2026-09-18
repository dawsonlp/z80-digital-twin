# Current development handoff

## Active work: live disassembly and patching, 17 September 2026

Branch `feature/live-disassembly-patching` starts at `c4be116`; the first
checkpoint/patch increment is recorded on this branch. The user approved implementation after reviewing the
[design](docs/developers/live-disassembly-patching-design.md). Track progress in
the [checklist](docs/developers/live-disassembly-patching-checklist.md).

The first increment adds explicit CPU/memory/Spectrum snapshots, a UI-free
paused patch controller, and a native **Live code update** panel. It supports
changed-size binaries, selected PC/HL/word edits, retaining all other machine
state, restoration, stale-review rejection and a recovery lock after failed
rollback. The checkpoint is in-process only. No symbol relocation is automatic.
The [worked example](examples/live-patching/README.md) and real-Pasmo/core tests
exercise a moved return continuation. Full Debug/UI build and 47 CTest tests passed; two optional ZEX suites skipped.
Native rendering is verified; button-level interaction is pending because the
computer-use service timed out after unlock.

Next work: complete native panel acceptance, then session attachment/coherent
export and stable symbol/continuation correspondence. Preserve the single
execution owner and optional user-approved state adjustments. Do not narrow the
product to fixed-layout patches. Previous analysis findings below remain useful;
the earlier product sequencing is superseded by this requested developer loop.

## Prior analysis baseline

**Reviewed:** 16 September 2026, after the evening analysis work.
**Active branch:** `feature/deterministic-execution-analysis`.
**Implementation checkpoint:** `3ff0629` (committed and pushed).
**Integrated main base:** `253986272f4aab15c757f64d3a23ad991cd2d1fb` via PR #1.
Analysis changes after that base remain on the feature branch, not merged to main.
This document is the single active continuation guide. Historical records live in
[the archive](docs/archive/README.md); their old priorities and state are not current.

## Goal and decisions to preserve

Produce assembly that explains the program: purpose, data representations,
algorithms, caller context, entry requirements and observable consequences.
Instruction paraphrases and mechanically correct graphs are supporting material,
not the finished understanding. The next product deliverable should be one
understood source section, not simply another whole-ROM listing.

- Collect facts with deterministic algorithms and controlled experiments first.
  Human or later agent interpretation can propose semantic meaning, but must
  remain distinguishable from observed behavior and testable against it.
- Preserve immutable observations and versioned findings. Completeness belongs
  to each analysis finding, not a global probability that a function is correct.
- Normal CALL use never closes the set of entries, callers or exits. Shared
  blocks, interior entries, stack dispatch and multiple uses of one RET coexist.
- Roll up findings after every stage. Keep their evidence and limitations while
  choosing concise explanations for source; do not overwrite deliberate names.
- Count distinct calling sites separately from occurrences. Static possibilities,
  executed transfers and derived routine relationships remain separate views.
- Byte-equivalent reassembly and semantic understanding are separate acceptance
  claims. Keep external Pasmo/CLI boundaries and the independently usable LSP.

## What was completed tonight

The previous symbol/runtime baseline was integrated through PR #1 before this
feature branch began. Stable symbol identity, aliases, enrichment, image-bound
persistence and semantic Pasmo export already exist. Interactive native save/reopen
acceptance remains open; this evening's headless tests do not close it.

| Capability | Current result |
| --- | --- |
| Concrete transfer classification | Taken/untaken CALL, RST, jumps and returns from supplied context, with unresolved outcomes kept explicit. |
| Continuation and value tracing | Exact saved-byte lineage, register/memory origins, bounded arithmetic and stack-pointer provenance. |
| Constructed transfers | Popped-continuation jumps, PUSH/RET dispatch and continuation-preserving indirect transfers. |
| Stack reconstruction | Supported caller-skipping exits, stack restoration/replacement, substituted continuations and prepared/consumed continuations. |
| Repeatable resolution | Separate raw findings and stage-by-stage comments, dependencies and alternative site usages. |
| Instruction coverage | BIT, CP, SCF/CCF and logical operations preserve unrelated lineage; unmodeled flags and unknown operands remain explicit. |
| Transfer graph and metrics | Capture-local typed edges, reverse indexes, distinct caller sites/addresses, occurrence counts, conditional outcomes and continuation variants. JSON plus readable text. |

Key recent commits: `e09849c` stack reconstruction; `06eb434` ROM experiments;
`99f5087` selective instruction-effect coverage; `3ff0629` graph/caller metrics.

Sixteen controlled ROM-entry experiments recorded 914 instructions and reproduced
49 artifacts byte-for-byte in two runs. They recover both UNSTACK paths, preserve
channel-selection continuation context, identify USR's prepared continuation and
retain two distinct uses of the calculator RET at `$33A1`. Graph checks distinguish
CALL and fall-through arrivals at `$162C` without counting them as equivalent calls.

These are CPU-only laboratory entries with boot-derived RAM and explicit setup,
not authentic BASIC invocations or resumed machine snapshots. Reference routine
names are not algorithmically discovered meanings. Table provenance can remain
partial even when the observed numeric destination is certain.

## Where we are now

`z80_analyze transfers` reads supplied captures, emits report v7 and supports
`--through effects|continuations|values|constructed|stack` and `--format json|text`.
Capture writers use v2; readers retain v1 compatibility. Graph tactic is
`z80-transfer-graph/1`; value tactic is `z80-address-values/3`.

The symbol/assembly pipeline and execution-analysis pipeline both work, but their
integration is incomplete. Generated analysis comments and caller summaries are
in reports; they are not automatically attached to exported assembly. There is
no routine-level call graph yet, no cross-capture occurrence deduplication, and
no automatic persistent runtime capture. Reopening evidence is not resuming CPU,
Spectrum device, interrupt or tape state.

## Next roadmap, in order

1. **Make channel selection/dispatch the first semantic source slice.** Follow
   real callers as well as existing isolated experiments. Explain selectors,
   input/output pointer fields, shared entries, continuation requirements and
   observable behavior. Form predictions and test an unshown case. Keep imported
   terminology and semantic hypotheses attributable.
2. **Finish instruction coverage needed by that slice.** Continue Stage 4a:
   remaining indexed/immediate byte memory effects, shifts/rotates and BEEPER
   address derivation. Establish explicit I/O-effect assumptions before retaining
   lineage through OUT. Check undocumented/prefixed forms against the actual
   executor; old display investigations record executor/hardware differences.
3. **Connect resolved findings to image-bound enrichment and source export.**
   Retain evidence, names and review decisions; render useful feature headers,
   entry-specific requirements, data declarations and selective local comments.
   Include caller/entry summaries with evidence scope. Verify save/reopen and
   Pasmo byte equality separately from whether a reader understands the behavior.
4. **Build blocks and overlapping routine candidates.** Preserve late interior
   entries, changed encodings and shared membership. Derive the routine call graph
   from typed edges plus versioned membership/context; never promote every jump
   to a call. Keep ambiguous caller attribution rather than double-counting it.
5. **Resolve runtime identity before integrating automatic capture.** Run/epoch
   meanings for pause, clear, reset, boot and image replacement need owner input.
   Only then broaden durable experiment aggregation and cross-capture metrics;
   matching addresses or filenames are insufficient identity.

The [current roadmap](docs/developers/roadmap.md) carries this delivery sequence.
Detailed contracts and checklists:

- [Deterministic analysis ladder](docs/developers/deterministic-analysis-development-plan.md).
- [Transfer graph and caller metrics](docs/developers/transfer-graph-and-caller-metrics.md).
- [Symbol analysis and assembly projection](docs/developers/symbol-analysis-development-plan.md).
- [Runtime identity decision](docs/developers/analysis-implementation.md#architectural-input-needed-runtime-evidence-identity).

## Verification and how to resume

Latest implementation validation at `3ff0629`: Debug/UI **45 passed + 2 optional
ZEX skips** (47 registered); Release/headless **44 passed + the same 2 skips**
(46 registered). Local Spectrum ROM and Pasmo 0.5.5 were supplied. Build logs had
no compiler warning/error matches. These are recorded results for that checkpoint,
not a new test run caused by this documentation update. No new UI or LSP acceptance
is claimed. Repeat relevant checks after implementation changes.

```sh
git status --short
git log -5 --oneline
cmake --build build -j 4
Z80_SPEC48_ROM="$PWD/spec48.rom" ctest --test-dir build --output-on-failure
build/z80_analyze transfers --source tests/fixtures/analysis/constructed-transfers.json --format text
```

The prior release build is `/private/tmp/z80-merge-release`; Pasmo is locally at
`build/tools/pasmo-0.5.5/pasmo`. Verify generated paths before using them. Latest ROM
artifacts are `/private/tmp/z80-rom-graph` and `/private/tmp/z80-rom-graph-replay`.
Use [the experiment reproduction instructions](docs/developers/rom-control-flow-experiments.md)
to recreate missing artifacts. Its original findings table predates the latest
coverage improvements; use the current ladder checkpoints for recovered findings.

Implementation map: `debugger/analysis/` contains tactics, resolution and graph
construction; `tools/analyze/main.cpp` provides the CLI; `debugger/disasm/` and
`debugger/symbols/` provide source projection and symbol compatibility. Tests are
in `tests/*analysis*`, `tests/transfer_graph_workflow_test.py` and
`tests/fixtures/analysis/`. The optional ROM harness/verifier are in
`tools/experiments/`.

## Boundaries and older acceptance still open

Do not silently change CPU timing, HALT/interrupt fidelity, runtime ownership,
mutable-memory identity, snapshot/resume or persistence guarantees. Address
accounting/overflow, completion summaries, overhead/resource measurements and
remaining native interactions are still tracked by the
[address-metadata checklist](docs/developers/address-metadata-checklist.md).

`Clear analysis` preserves machine state/protection; UI Restart program cold-boots
Spectrum and clears RAM/analysis. The existing
build/run command still launches a fresh process. Manual paused replacement is
now available through Live code update; automatic editor-driven reload remains pending. Preserve user work
in `examples/spectrum-dev/main.asm`; tests use their own fixtures. ROMs, tapes,
generated listings, tool binaries and temporary experiment outputs remain local.
