# Deterministic execution analysis: development checklist

**Date:** 16 September 2026.  
**Status:** agreed direction; implementation has not started.  
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
- [ ] Push the completed feature branch and open a PR targeting main with a
      description of the resulting baseline and its verified limitations.
- [ ] Review the full PR diff and required checks; merge into main when ready.
- [ ] Fetch and verify that the completed feature tip is contained in origin/main;
      update local main without losing work and record the resulting base SHA.
- [ ] Create and publish a fresh branch from that main SHA. Suggested name:
      `feature/deterministic-execution-analysis` (confirm availability at creation).
- [ ] Record the base SHA here before implementing Stage 1. Keep the old branch
      until integration is verified; branch deletion is a separate optional cleanup.

## 1. Establish the evidence and completeness contract

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

- [ ] Track continuation values created by CALL/RST using the actual stack writes
      and their lineage, not address equality or a conventional shadow stack alone.
- [ ] Match subsequent consumption, including stack-slot reuse and overwritten
      values; report unmatched events when capture starts mid-invocation.
- [ ] Track bounded register/value provenance through copies, EX/EXX, pushes,
      pops, spills/reloads and supported address arithmetic.
- [ ] Identify pointer loads, table-entry locations and computed target offsets;
      record memory versions and arithmetic widths/wraparound.
- [ ] Stop provenance explicitly at an unsupported operation, unknown initial
      value, missing write, capture gap or resource bound. Do not guess through it.
- [ ] Retain the difference between a saved continuation's lineage and an unrelated
      value that happens to equal its numeric address.

Acceptance: normal nesting, recursion, reused stack slots, coincident numeric
addresses, overwritten continuations and interrupted capture produce either
supported matches or explicit unresolved results, never fabricated matches.

## 4. Recognize constructed transfers and stack changes (rungs 5–6)

- [ ] Implement separate tactics for a popped continuation followed by JP through
      a register, a pushed target consumed by RET, and an indirect-call helper.
- [ ] Recognize explicit continuation construction, discarded continuations,
      return-address substitution, SP replacement and restored stack pointers.
- [ ] Describe continuation-preserving transfers before claiming a tail call;
      routine grouping is a separate interpretation.
- [ ] Track caller-skipping exits and stack rebuilding where provenance supports
      them; retain unresolved stack discontinuities otherwise.
- [ ] Require semantic/data-flow preconditions for each tactic. Nearby opcode
      patterns alone may produce candidates, not confirmed explanations.
- [ ] Allow different tactic results for different executions of the same opcode.

Acceptance: synthetic equivalents of UNSTACK_Z, USR dispatch and stack rebuilding
are explained alongside conventional CALL/RET paths into the same target. Negative
fixtures with similar opcodes but different value lineage do not match falsely.

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
