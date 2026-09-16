# ROM-first reverse development checklist

> Continuation update, 15 September 2026: instruction control and in-memory
> observed browsing have progressed beyond the initial checkpoint below.
> [Address metadata](address-metadata-checklist.md) tracks current acceptance;
> the durable save/reopen/export loop remains incomplete. Earlier unchecked
> items and dated notes are historical checkpoints, not a current feature list.

Agreed direction: 14 September 2026. Implementation starts from the dirty
`feature/spectrum-dev-loop` checkout at `98cf7fb`; preserve existing work.

## Decision and escalation boundary

Approved: one CPU instruction-completion contract; SpectrumMachine owns CPU,
devices, interrupts and frame progress; headless debugger control; original
image, observations and interpretation remain separate; bounded local analysis
persistence; evidence-aware display; byte-preserving annotated export.

Bring new architectural decisions to the user before implementing them,
particularly interrupt/time fidelity, public API compatibility changes,
resumable state, mutable-memory identity, and persistence guarantees beyond
reopening retained evidence. Routine implementation choices remain local.

## 1. Protect and identify the baseline

- [x] Move workflow acceptance inputs to a stable test fixture; preserve scratchpad.
- [x] Run the real Pasmo workflow and existing regression suite; record skips.
- [x] Record exact 16 KB ROM hash, load range, emulator revision and dirty build identity.
- [x] Verify paused reset, stepping and a bounded run without imported labels.
- [x] Export the full ROM at origin zero, reassemble and compare all bytes.

## 2. Establish trustworthy execution

- [x] Add CPU-owned bounded whole-instruction stepping with explicit completion,
      HALT and incomplete-budget outcomes; retain prefix progress on interruption.
- [x] Test long prefix chains, exhausted budgets and resumed completion.
- [x] Keep EI deferral and interrupt acceptance at complete instruction boundaries.
- [x] Migrate DebugSession to the CPU result, propagate incomplete stops and
      preserve original instruction context across continuation; remove its silent guard.
- [x] Record completed starts separately from decoder-derived spans.
- [x] Consolidate Spectrum wiring/frame progress into SpectrumMachine with a
      headless execution seam; migrate debugger, viewer and probe.
- [x] Preserve partial frames across pause/resume and instruction overrun.
- [x] Decision: instruction-level observation first; preserve display operation,
      defer the separate HALT/interrupt-signal fidelity corrections.
- [ ] Verify HALT clock/refresh/peripheral progression and interrupt transitions
      before claiming continuous boot tracing.

## 3. Retain evidence and interpretation

- [ ] Add optional bounded observation: ordered starts, execution-time bytes,
      before/after registers, resulting PC and emulator T-state deltas.
- [ ] Distinguish instruction, interrupt and incomplete execution events.
- [ ] Track counts, first/last observations and numeric successors; expose RAM transitions.
- [ ] Record run start context, stop reason, retention policy and dropped counts.
- [ ] Define versioned JSON metadata and JSONL events bound to image/machine/build identity.
- [ ] Preserve imported, user and generated annotations separately; use SymbolTable
      as a view rather than the authoritative provenance store.
- [ ] Reject wrong-image loading; make any explicit rebinding a separate action.
- [ ] Save/reopen evidence and annotations; do not claim machine resume or replay.

## 4. Close the user loop

- [ ] Show observed starts/counts/successors and supporting register context.
- [ ] Keep unobserved bytes unobserved and interpretations revisable.
- [ ] Annotate one routine, close, reopen and verify retained work.
- [ ] Export a selected original-ROM range with labels/comments and deterministic
      Pasmo-safe identifier mapping; retain unsupported encodings as raw bytes.
- [ ] Reassemble and compare the exact range, independently of code/data hypotheses.
- [ ] Verify actual CLI and GUI boundaries; record passes, skips and limitations.

Deferred: automatic function recovery, full call graphs, temporal mutable-RAM
reconstruction, snapshots, replay and time travel. No ROM bytes committed to Git.

## Acceptance

Identified ROM at reset -> bounded observation -> inspect -> annotate -> save
and reopen -> export -> byte-identical reassembly. Wrong ROM hashes are detected;
unobserved ranges do not become data by default.

## Checkpoint: 14 September 2026

Implemented stable fixtures under `tests/fixtures/spectrum-dev/` and CPU
`StepInstruction(stage_budget)`. Existing `Step()` remains available as a
documented low-level stage operation; consumers have not all migrated.
CPU interrupt acceptance now rejects a partial instruction; EI deferral is
cleared only at a completed boundary. No halted idle-time model was added.

Fresh `cmake --build build -j 4` succeeded. Fresh CTest: 33 passes, ZEXDOC and
ZEXALL skipped for missing external assets. Includes real Pasmo workflow,
both disassembly gates and ROM-backed headless boot/debug checks. GUI acceptance
and LSP tests were not rerun in this increment. `git diff --check` passed.

Local `build/rom-reverse-baseline/report.json` records executable hashes and
the dirty revision. The 16384-byte ROM at `$0000..$3fff` has SHA-256
`d55daa439b673b0e3f5897f99ac37ecb45f974d1862b4dadb85dec34af99cb42`.
CLI disassembly and real Pasmo reassembly compared equal across all bytes.
Generated source and ROM copies remain in ignored local build storage.

## Instruction execution and display continuation

The user resolved the timing question: debugger control requires no emulated
interrupt and no full-frame execution quantum. Run and single-step must both
advance the Spectrum display through the T-states actually consumed. Hardware
interrupts remain machine-originated events, not debugger control mechanisms.
The HALT/interrupt-signal fidelity increment remains deferred.

Implemented:

- DebugSession uses the CPU's bounded operation, reports incomplete instructions,
  preserves original writer PC across prefix continuation, and publishes start
  coverage only on completion. Operand spans remain decoder-derived.
- SpectrumMachine owns CPU/device wiring, persistent frame deadlines and frame
  completion. Debugger and probe attach headless prepare/advance hooks; viewer
  batching uses the same lifecycle. All actions are capped at 4096 prefix stages.
- Frame deadlines carry instruction overrun. Pause does not complete a frame or
  advance the emulated clock. The display retains the latest completed raster;
  audio is delivered on frame completion, before the next frame clears events.
- The debugger now permits machine execution requests while halted; only the
  machine's existing hardware interrupt may wake the CPU. Step-over decodes
  after machine preparation so it cannot interpret the interrupted instruction
  as the instruction actually being stepped.
- `--steps N` performs instruction steps in either mode, then pauses; existing
  `--run` behavior is retained. When combined, `--steps` runs after `--run`.

Verification: deterministic 25,000-instruction batch versus individual-step
comparison, with a mid-frame breakpoint/pause, matches CPU state, memory,
T-states, deadlines, completed pixels and beeper-edge timestamps. Prefix
exhaustion/continuation and interrupt-boundary step-over have focused checks.
The full CTest suite and the final GUI/CLI checks are recorded below.

GUI captures under local `build/rom-reverse-baseline/`: `instruction-runtime.png`
shows the booted ROM copyright screen; `reset-step.png` shows PAUSED, PC `$0001`,
4 T-states after one instruction from reset, without imported symbols. The
headless probe also booted 120 frames and ran two observed frames successfully.

Limitations remain explicit: the retained early-HALT frame completion policy
omits halted idle cycles, and frame interrupts remain one-shot requests. This
increment does not establish hardware-accurate continuous time through HALT.
Execution-history capture/counts, interrupt provenance, persistence and annotated
export remain unfinished; this is the execution/control foundation.

Final rebuild succeeded. Final CTest: **33 passed, 2 skipped** (external
ZEXDOC/ZEXALL assets absent); the ROM-backed checks executed. Full log:
`build/rom-reverse-baseline/instruction-runtime-ctest.txt`. GUI/probe evidence
and final executable identity are described in local
`build/rom-reverse-baseline/instruction-runtime-report.json`. The report records
that GUI captures precede the final prefix-budget clamp; their exercised
instructions do not encounter that clamp. No audible playback or fresh LSP
acceptance is claimed. `git diff --check` passed. Changes remain uncommitted.

## Observed browsing continuation

The subsequent [observed disassembly checklist](observed-disassembly-checklist.md)
records bounded execution history, per-address counts, byte revision validation
and full address browsing. Persistence remains deferred. Metadata is an opt-in
memory policy; the existing efficient policies remain available.
