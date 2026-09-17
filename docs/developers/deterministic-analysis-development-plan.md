# Deterministic execution analysis: development checklist

**Date:** 16 September 2026.  
**Status:** transfer classification, ordinary stack-continuation matching and bounded value tracing, constructed-transfer and bounded stack-reconstruction tactics with repeatable resolution implemented; runtime
capture integration and the broader stages remain open.
**Outcome:** build program understanding through reproducible experiments and
deterministic tactics, preserving every observed usage and keeping routine
boundaries, entries, exits and interpretations open to later evidence.

This plan extends the [symbol lifecycle plan](symbol-analysis-development-plan.md)
and [ROM-first loop](rom-reverse-loop-checklist.md). It does not mark their pending
acceptance complete. Agents are not required for any stage below; later agent
interpretations would use the same evidence and proposal boundaries.

## 0. Integrate the existing baseline before new implementation

Observed on 2026-09-16: local and GitHub tips agree. `main` is `9a9c817`, `dev`
is `98cf7fb`, and `feature/spectrum-dev-loop` is `a5e3c3d`. Main is an ancestor
of the feature branch, which contains four additional commits, including dev.
GitHub has no pull requests. These are a dated snapshot, not future guarantees.
The working tree additionally contains uncommitted symbol lifecycle, storage,
export, CLI, UI, test and documentation changes. This plan is also uncommitted.

Recommended integration: one reviewed feature-branch PR directly into main.
There is no need to merge dev separately: its committed work is already included.
Prefer a merge commit to retain existing ancestry and the constituent commits.
Do not mix new execution-analysis implementation into that integration.

- [x] Review the complete tracked and untracked changes; identify intended source,
      tests and documentation. Preserve unrelated work and local ROM/game assets.
- [x] Review remaining baseline acceptance and decide which items block integration
      versus remain explicit follow-up limitations. In particular, retain the open
      native save/reopen walkthrough, metadata audit and HALT/interrupt limitations.
- [x] Run the baseline's required build/test gates with actual tool and asset
      identities: Debug/UI, Release/headless, semantic export and real Pasmo
      round-trip checks; verify any affected LSP boundary as appropriate.
- [x] Record fresh passes, failures, optional skips and native acceptance status;
      historical results are not verification of the final commit.
- [x] Commit the reviewed existing work in coherent units, including this plan;
      inspect staged content and whitespace before each commit.
- [x] Push the completed feature branch and open a PR targeting main with a
      description of the resulting baseline and its verified limitations.
- [x] Review the full PR diff and required checks; merge into main when ready.
- [x] Fetch and verify that the completed feature tip is contained in origin/main;
      update local main without losing work and record the resulting base SHA.
- [x] Create and publish a fresh branch from that main SHA. Suggested name:
      `feature/deterministic-execution-analysis` (confirm availability at creation).
- [x] Record the base SHA here before implementing Stage 1. Keep the old branch
      until integration is verified; branch deletion is a separate optional cleanup.

Integration completed 16 September 2026 through [PR #1](https://github.com/dawsonlp/z80-digital-twin/pull/1). Verified base for `feature/deterministic-execution-analysis`: `253986272f4aab15c757f64d3a23ad991cd2d1fb`, the main merge commit. The merged tree matches the validated feature tip `12c6ca5` exactly. Local main and origin/main agree; the new branch is published. Old branches are retained. [Fresh validation](../testers/main-integration-verification.md) records passes and outstanding limitations. This branch begins with a documentation-only integration record; analysis implementation remains unstarted.

## 1. Establish the evidence and completeness contract

### First implementation checkpoint — 16 September 2026

Implemented `transfer_analysis` in the UI-free core and the read-only
`z80_analyze transfers --source` command. This is the independent portion of
rungs 1–2: classify supplied instruction effects, preserve occurrences, aggregate
open destination edges and report per-observation completeness. It does not
claim completion of Stages 1–3 or change debugger capture/reset/load ownership.

- [x] Preserve evidence inputs separately from versioned deterministic findings.
- [x] Distinguish taken/untaken conditionals using supplied entry flags/B;
      unavailable context stays unresolved even when successor PC looks obvious.
- [x] Distinguish architectural RET from a proven logical return; machine
      transitions from inferred interrupts; repetition from ordinary fall-through.
- [x] Retain alternative destinations and byte versions without exclusive routine
      ownership or closed target sets.
- [x] Round-trip bounded interchange evidence and regenerate identical reports;
      reject malformed inputs and preserve evidence after transient-history eviction.
- [ ] Resolve owner input on runtime run/epoch identity before adding capture hooks.
- [ ] Integrate full register/memory capture, image/run identities, continuation
      lineage and symbol-proposal attachment in subsequent increments.

The interchange file is not a run identity schema, resumable machine snapshot or
symbol-project revision. See [usage and format](../users/transfer-analysis.md).

Fresh validation: Debug/UI registered 42 tests (40 passed, two optional ZEX asset
tests skipped); Release/headless registered 41 (39 passed, the same two skips).
Pasmo 0.5.5 and the local Spectrum ROM were supplied in both configurations.
No compiler warning/error matches were found in the build logs. New tests exercise
the real CPU's conditional CALL behavior, all eight conditions, DJNZ wrapping,
index-prefix targets, block repetition, partial/contradictory evidence, late new
destinations, history eviction, strict serialization and fresh-process CLI reports.
No new native UI walkthrough is claimed; this increment adds no UI controls.

Observations are immutable records of captured emulator behavior. Analyses are
versioned explanations attached to those observations. Correct capture does not
prove hardware fidelity, complete recording or universal behavior.

- [ ] Define run, capture epoch, event, experiment and analyzer-version identities.
      Resolve the existing [runtime identity decision](analysis-implementation.md#architectural-input-needed-runtime-evidence-identity)
      before capture hooks: pause, clear observations, CPU reset, cold boot and
      image replacement must retain their distinct meanings.
- [ ] Bind evidence to image/build/machine configuration, execution-time bytes,
      relevant memory revisions and known initial context. Unknown context stays
      explicit; opening an evidence file does not imply resumable machine state.
- [ ] Separate immutable event facts from derived explanations, routine groupings,
      user/imported names and revisable completeness annotations.
- [ ] Define deterministic tactic records: identifier/version, preconditions,
      supporting event IDs, result, unresolved dependencies and applicability.
- [ ] Use idempotent analysis keys; reruns preserve review decisions and history.
- [ ] Preserve open sets of possible entries, targets and exits. Neither one normal
      call nor repeated identical calls closes any set.
- [ ] Define completeness dimensions: target provenance, continuation relationship,
      capture coverage, entry context and tested generalization scope.
- [ ] Derive any compact fuzziness indicator from those dimensions with visible
      reasons. It is not a probability of correctness or a reward for repetition.
      Complexity alone does not increase fuzziness; unknown dependencies do.
- [ ] Attach completeness to an observation's analysis, not one global score on a
      symbol. A contradicted generalization is superseded/rejected, not merely fuzzy.
- [ ] Specify retention/resource budgets and report omitted data explicitly.

Acceptance: save/reopen two observations at the same instruction with different
usage explanations; revise one annotation without changing either event or the
other explanation. Preserve missing-context and emulator-fidelity limits.

## 2. Capture and classify concrete execution (ladder rungs 0–2)

- [ ] Extend observation capture with before/after registers, including alternate
      banks, SP, relevant flags and instruction-associated data/stack accesses.
- [ ] Keep execution-time instruction bytes and actual next PC; distinguish
      completed instructions, incomplete execution and machine transitions.
- [ ] State access ordering guarantees; do not label instruction-level memory
      records as bus-cycle-accurate traces.
- [ ] Classify actual taken/not-taken CALL, conditional CALL, JP/JR, DJNZ, RET,
      conditional RET, RST, RETI/RETN and fall-through using instruction semantics.
      Do not infer a not-taken branch solely from PC equaling the sequential PC.
- [ ] Retain numeric indirect destinations and source–target edge occurrences,
      counts and supporting event IDs; distinguish static possibilities from
      observed transfers and interrupts from ordinary calls.
- [ ] Persist selected evidence plus its dependencies before transient eviction;
      mark partial extracts and capture gaps instead of inventing continuity.
- [ ] Verify that unexecuted bytes remain unknown, not automatically data or code.

Acceptance: a synthetic fixture exercises taken/untaken conditionals, an indirect
jump with multiple destinations and an interrupt transition. Reopened evidence
reproduces the same edge report without executing the program again.

## 3. Match continuations and trace target origins (rungs 3–4)

Rung 3 checkpoint (16 September 2026): headless analysis accepts version 2
interchange evidence with before/after SP, actual instruction-level data accesses
and explicit predecessor continuity. Version 1 remains readable with unresolved
stack context. This implements ordinary continuation matching on supplied evidence,
not automatic runtime collection or the general value tracing of rung 4.
Creation and matching findings carry explanatory text and supporting call IDs;
later projection can use those findings as assembly comments without treating a
single invocation as an exclusive function contract.

Validation for this checkpoint: Debug/UI 41 passed and two optional ZEX skips
(43 registered); Release/headless 40 passed and the same two skips (42 registered).
Both configurations supplied Pasmo 0.5.5 and the local ROM. The new CPU integration
fixture observes actual writes/read-count changes for CALL -> JP (HL) -> RET;
synthetic cases cover nested and recursive calls with equal numeric continuations,
partial/contradictory captures, late entries, same-value/refused writes, stack
wraparound, register saves and PUSH/RET dispatch. Fresh-process CLI tests verify
version 1 compatibility, version 2 matching/explanations and gap-induced uncertainty.

- [x] Track continuation values created by CALL/RST using the actual stack writes
      and their lineage, not address equality or a conventional shadow stack alone.
- [x] Match subsequent consumption, including stack-slot reuse and overwritten
      values; report unmatched events when capture starts mid-invocation.
- [x] Track bounded register/value provenance through copies, EX/EXX, pushes,
      pops, spills/reloads and supported address arithmetic.
- [x] Identify pointer loads, table-entry locations and computed target offsets;
      record memory versions and arithmetic widths/wraparound.
- [x] Stop provenance explicitly at an unsupported operation, unknown initial
      value, missing write, capture gap or resource bound. Do not guess through it.
- [x] Retain the difference between a saved continuation's lineage and an unrelated
      value that happens to equal its numeric address.

Acceptance: normal nesting, recursion, reused stack slots, coincident numeric
addresses, overwritten continuations and interrupted capture produce either
supported matches or explicit unresolved results, never fabricated matches.

Rung 4 checkpoint (16 September 2026): bounded address-value graphs now trace
supported register, exchange, stack, memory and arithmetic effects in supplied
captures. Every node links to its input nodes and supporting sample. Initial
register/memory boundaries yield partial findings; unsupported effects, gaps,
contradictions and node-budget exhaustion stop lineage explicitly. Source memory
locations are retained without inferring unobserved table entries. Same-value
writes create distinct versions, and equal numeric addresses do not imply shared
continuation ancestry. Report v3 preserves the original instruction-only basis
while exposing later target provenance. Capture v1/v2 remain readable.

The runnable `value-origins.json` fixture shows CALL -> POP DE -> EX DE,HL ->
JP (HL), and a real-CPU integration test independently supplies observed effects
for that path. This establishes value origin, leaving constructed-transfer role
recognition to rung 5. Automatic runtime capture and assembly projection remain
open. See [usage and limits](../users/transfer-analysis.md#bounded-address-value-tracing-rung-4).

Rung 4 validation: Debug/UI 42 passed and two optional ZEX skips (44 registered);
Release/headless 41 passed and the same two skips (43 registered), with the local
Spectrum ROM and Pasmo supplied. No compiler warning/error matches were found.
Tests include real CPU execution, pointer loads, index arithmetic and wraparound,
EXX, spills/reloads, overwritten/refused writes, contradictory snapshots, capture
gaps, unsupported effects, exhausted budgets and deterministic fresh-process
reports. No new native UI acceptance is claimed.

Resolution checkpoint (16 September 2026): before adding constructed-transfer
roles, implement the agreed repeatable roll-up over existing findings. Report v4
retains each tactic's raw limitations and exposes a separate versioned resolved
view. `--through effects|continuations|values` runs the requested stages; resolution
can also consume supplied findings directly without rerunning those tactics.
Covered dependencies name their resolving tactic. Per-site variants preserve
alternative destinations/usages and changed instruction bytes. Comments summarize
observed evidence without declaring universal functions or logical return roles.

- [x] Resolve after each available analysis stage, rather than only at the end.
- [x] Preserve raw findings and record explicit coverage of earlier limitations.
- [x] Group per-site comments with supporting samples and alternative usages.
- [x] Keep capture inputs and earlier occurrence findings unchanged as evidence grows.
- [x] Add rung 5 constructed-transfer findings as new inputs to this resolver.

Resolution validation: Debug/UI 42 passed and two optional ZEX skips (44
registered); Release/headless 41 passed and the same two skips (43 registered).
The local Spectrum ROM and Pasmo 0.5.5 were supplied. Build logs contained no
compiler warning/error matches. Fresh-process tests compare all three stages,
preserve raw findings and capture hashes, expose contradictions, retain a later
alternative target and changed code bytes, and report ancestry-summary budget
exhaustion without losing the value graph. The expanded CLI test also passed
separately in both configurations. No native UI changes or acceptance are claimed.

## 4. Recognize constructed transfers and stack changes (rungs 5–6)

- [x] Implement separate tactics for a popped continuation followed by JP through
      a register and a pushed target consumed by RET.
- [x] Establish the continuation-preserving indirect-jump primitive used by call
      helpers, while retaining internal jumps as an alternative interpretation.
- [x] Recognize supported explicit continuation preparation/consumption, stack-slot
      removal, return-address substitution, SP assignment and saved-SP restoration.
- [ ] Extend to arbitrary stack reconstruction beyond the documented supported forms.
- [x] Describe continuation-preserving transfers before claiming a tail call;
      routine grouping is a separate interpretation.
- [x] Track caller-skipping exits and stack rebuilding where provenance supports
      them; retain unresolved stack discontinuities otherwise.
- [x] Require semantic/data-flow preconditions for each tactic. Nearby opcode
      patterns alone may produce candidates, not confirmed explanations.
- [x] Allow different tactic results for different executions of the same opcode.

Acceptance: synthetic equivalents of UNSTACK_Z, USR dispatch and stack rebuilding
are explained alongside conventional CALL/RET paths into the same target. Negative
fixtures with similar opcodes but different value lineage do not match falsely.

Rung 5 checkpoint: report v5 adds the `constructed` stage and separate findings
for register-mediated returns, PUSH/RET target consumption and continuation-
preserving indirect jumps. Exact low/high byte identity is propagated through
modeled data operations; equal values, address-only dependencies and arithmetic
ancestry do not establish identity. Captured invocation lifetimes prevent reuse
of consumed continuations. The resolver now includes the observed continuation
relationship and retains raw earlier findings. This does not complete the broader
stack reconstruction or function-role work in this section.

Validation: Debug/UI 43 passed and two optional ZEX skips (45 registered);
Release/headless 42 passed and the same two skips (44 registered). The local ROM
and Pasmo were supplied; build logs contain no compiler warning/error matches.
The existing real-CPU CALL/POP/EX/JP fixture now verifies the return-role finding.
Synthetic tests cover nesting, IX, stack wrap, same-value and address-only false
matches, byte-lane identity, repeated consumption, PUSH/RET dispatch, refused
writes, SP mismatch, interrupt-return boundaries and unavailable value evidence.
Fresh-process tests verify all four stages, preserved raw findings, resolved
relationships and the three runnable examples. No runtime hooks or UI changes
are included.

Rung 6 checkpoint: report v6 adds a `stack` stage over existing findings and value
tactic v2 adds SP roots/snapshot lineage. Supported rules identify caller-skipping
through older live continuations, reconstructed/relocated continuation transfers,
slot substitution, SP restoration by provenance, and explicit literal continuation
preparation followed by observed consumption. Earlier candidates and raw findings
remain unchanged. This is bounded headless reconstruction, not arbitrary symbolic
stack recovery or runtime capture. See the runnable `stack-reconstruction.json`
fixture and [supported forms](../users/transfer-analysis.md#stack-reconstruction-and-caller-skipping-rung-6).

Rung 6 validation: Debug/UI 44 passed and two optional ZEX skips (46 registered);
Release/headless 43 passed and the same two skips (45 registered). The local
Spectrum ROM and Pasmo were supplied; build logs contain no compiler warning/error
matches. A real-CPU nested CALL/CALL/POP/RET fixture verifies caller-skipping.
Synthetic tests cover stack wrap, multiple bypasses, SP byte adjustments,
reconstructed and relocated continuations, pointer spills/reloads, equal-number
substitution, contradictory capture, preparation/consumption, repeated dispatch,
equal-value/refused writes and exhausted budgets. Fresh-process CLI tests verify
all five stages, unchanged earlier findings, supporting samples and resolved
comments. No runtime hooks or native UI acceptance are included.

## 5. Build overlapping structures and usage patterns (rungs 7–8)

- [ ] Build instruction blocks and split at newly established entries while
      preserving links from historical evidence to execution locations.
- [ ] Permit overlapping instruction decodings and changed code versions; a jump
      into an operand byte must not corrupt the earlier instruction interpretation.
- [ ] Represent routine candidates as revisable entry/block relationships, not
      exclusive contiguous byte ranges or ownership inferred from the next symbol.
- [ ] Accumulate alternative entries reached by CALL, jump, fall-through and
      stack-mediated dispatch without erasing earlier usage.
- [ ] Group invocations deterministically by recorded entry, mechanism, supported
      stack context, paths and exit behavior; retain the grouping rule/version.
- [ ] Add specific markers: multiple entries, multiple mechanisms, shared blocks,
      entry-dependent stack requirements and multiple continuation behaviors.
- [ ] Keep algorithmically derived register/memory dependencies distinct from
      semantic input/output contracts; unchanged in a test does not mean preserved
      universally, and numerical correlation does not prove causation.
- [ ] Preserve user symbol IDs/names when a routine grouping splits or overlaps.

Acceptance: observe a normal call first, then a jump to an interior entry, then
a PUSH/RET entry. All remain queryable with their original evidence; shared blocks
and entry-specific explanations appear without a forced exclusive partition.

## 6. Run controlled experiments (rung 9)

- [ ] Define experiments with setup, original observed caller context, deliberate
      input changes, predicted observation, stop condition and resource limits.
- [ ] Begin by exercising authentic callers. Direct invocation of internal entries
      must record required register, flag, alternate-bank, memory and stack setup.
- [ ] Establish reproducible setup through a bounded boot/input procedure or
      fixture. Machine snapshot/resume support is a separate decision, not assumed.
- [ ] Vary selected inputs and compare paths, destinations, memory effects and
      outputs; include machine/I/O state where relevant, not just register values.
- [ ] Record failed setup, timeout and unsupported context separately from routine
      behavior. Isolate experiments so one run's effects do not leak unnoticed.
- [ ] Report tested domains explicitly; claim exhaustiveness only for a defined,
      finite domain whose cases were all completed under specified assumptions.
- [ ] Generate deterministic next-experiment candidates from unobserved branch
      outcomes, unresolved selectors and missing provenance, without requiring an
      agent or automatically executing arbitrary proposed setups.

Acceptance: the same fixture/setup and input set gives the same analysis report;
the report distinguishes varied inputs, fixed context, tested cases and open cases.

## 7. Reconcile, persist and expose findings (rung 10)

- [ ] Recompute dependent interpretations and completeness annotations when new
      evidence arrives; preserve previous versions and reasons for revision.
- [ ] Permit new evidence to increase incompleteness or replace a broad uncertain
      grouping with several precise conditional explanations.
- [ ] Provide headless inspect/analyze/report operations using the same core as UI.
- [ ] Make every generated explanation navigable to retained supporting evidence
      and unresolved assumptions; report evidence that is no longer available.
- [ ] Keep deterministic outputs stable for identical evidence, tactic versions
      and settings; bounded computation must identify what it did not analyze.
- [ ] Project findings into the existing symbol/proposal lifecycle without silently
      overriding deliberate names, rejected proposals or imported attribution.
- [ ] Project supported discoveries into readable assembly comments as well as
      labels. Retain supporting observation IDs, tactic version, applicability and
      unresolved cases; never promote one observed convention to a universal one.
- [ ] Preserve byte-exact export independently of completeness of interpretation.
- [ ] Measure capture overhead, retained storage and analyzer scaling against
      declared budgets; keep metadata optional for ordinary CPU execution.

Acceptance: capture -> analyze -> inspect -> annotate -> save -> close -> reopen ->
reanalyze retains evidence and review state. Add an unfamiliar entry path and
verify that earlier facts survive while the usage model expands.

## 8. Spectrum integration experiments and independent fixtures

Use redistributable synthetic fixtures for mandatory tests. The local 48K ROM
with SHA-256 `d55daa439b673b0e3f5897f99ac37ecb45f974d1862b4dadb85dec34af99cb42`
is an optional integration input. ROM bytes and reconstructed source stay local.
Published names below are reference annotations, not independently discovered names.

- [ ] BEEPER: vary original L's low two bits; explain JP (IX) at `$03F0/$03F4`
      reaching `$03D1..$03D4` as internal timing entries, not four functions.
- [ ] Channel dispatch: explain `$162C` both through CALL from `$15FB` and through
      channel-selection fall-through, preserving the distinct continuation contexts.
- [ ] Shared printing/input: distinguish `$15EF`, `$15F2`, `$15E6` and shared
      `$15F7`, including the prepared stack/register state at the shared entry.
- [ ] UNSTACK_Z `$1FC3`: recover normal return through JP (HL) and caller-skipping
      syntax-time return through RET Z.
- [ ] USR `$34B3`: recover a constructed continuation to `$2D2B` and target in BC
      dispatched through RET at `$34BB`.
- [ ] Calculator: explain machine-level stack dispatch at `$33A1`, alternate
      entries `$335E/$3362`, and END_CALC at `$369B`. Keep interpreted bytecode
      locations distinct from CPU instruction locations; a calculator-bytecode
      decoder is a later explicit tactic, not implied by generic CPU tracing.
- [ ] Shared channel tail `$1646`, error handling `$0053` and CLEAR's transfer
      at `$1EEC`: retain shared code, inline data and nonstandard stack behavior.
- [ ] Record which conclusions were algorithmically recovered, checked against
      references, manually annotated or left unresolved. Missing ROM means skipped.

ROM experiment checkpoint (16 September 2026): [sixteen bounded CPU-only
ROM-entry runs](rom-control-flow-experiments.md) recorded 914 instructions and
replayed identically across 49 artifacts. These exercise UNSTACK variants,
BEEPER low-bit targets, printing/input and channel-selection paths, USR dispatch,
calculator table targets including a self-targeting RET, and error/CLEAR stack
changes. Several interpretations remain partial because unsupported instruction
effects discard lineage. The original integration acceptance items above remain
open where full callers, complete value chains or automatic structure recovery
were not established. The harness and verifier are optional laboratory tools;
ROM bytes and generated listings remain local.

Reference disassemblies: [BEEPER](https://skoolkid.github.io/rom/asm/03B5.html),
[printing](https://skoolkid.github.io/rom/asm/15EF.html),
[input](https://skoolkid.github.io/rom/asm/15E6.html),
[channel dispatch](https://skoolkid.github.io/rom/asm/1615.html),
[shared channel tail](https://skoolkid.github.io/rom/asm/1642.html),
[UNSTACK_Z](https://skoolkid.github.io/rom/asm/1FC3.html),
[USR](https://skoolkid.github.io/rom/asm/34B3.html),
[calculator](https://skoolkid.github.io/rom/asm/335B.html),
[END_CALC](https://skoolkid.github.io/rom/asm/369B.html),
[errors](https://skoolkid.github.io/rom/asm/0053.html), and
[CLEAR](https://skoolkid.github.io/rom/asm/1EAC.html).

## Delivery order and boundaries

- [ ] First increment: Stages 1–3 with durable evidence and ordinary continuation/
      bounded value tracking; validate one complete headless loop.
- [ ] Second increment: Stage 4 tactics and Stage 5 overlapping structure, with
      synthetic acceptance for late-discovered alternative invocation conventions.
- [ ] Third increment: Stage 6 experiment runner and Stage 7 integrated review,
      with selected Stage 8 ROM demonstrations and explicit resource measurements.

All increments include their own relevant persistence, reporting and regression
checks; Stage 7 is not permission to defer evidence durability until the end.
Resolve runtime identity, mutable-memory identity, persistence/resume guarantees
and changes to CPU timing/interrupt behavior before implementing those boundaries.
General symbolic execution, universal function recovery, banked-memory support,
hardware-fidelity corrections and agent interpretation are not prerequisites and
are not silently included in this effort.
