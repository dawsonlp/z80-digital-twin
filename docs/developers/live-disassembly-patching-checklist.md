# Live disassembly and patching: development checklist

**Started:** 17 September 2026.
**Branch:** `feature/live-disassembly-patching`, based on `c4be116`.
**Contract:** [design](live-disassembly-patching-design.md). Checked entries require
implementation evidence; unchecked entries remain open. This is an incremental
delivery record, not a claim that the entire editor workflow exists.

## 1. Saved state and replacement foundation

- [x] Explicit CPU state capture/restore at completed instruction boundaries,
      including EI deferral, HALT, alternate registers and timing.
- [x] Explicit memory/protection and Spectrum device/scheduling snapshots;
      retain live callbacks and ownership on restore.
- [x] Paused checkpoint/apply/restore controller shared by UI and headless tests.
- [x] Validate the whole patch before mutation: bounds, protection, overlaps,
      expected memory/state, and instruction completion.
- [x] Support changed-size code ranges and explicit state edits, without reset.
- [x] Preserve device effects of host display-memory writes and invalidate stale
      observations without calling them CPU self-modification.
- [x] Review/apply and in-process restore controls in the debugger; remain paused.
- [x] Verify restoration of CPU, RAM, frame timing, raster, tape and audio state
      against uninterrupted continuation, plus rejection and rollback cases.

- [ ] Native button-level apply/step/restore acceptance (rendering alone is insufficient).

## 2. Enriched editor loop

- [ ] Session endpoint and CLI/editor attachment to the same running machine.
- [ ] Coherent live export with image identity and evidence cutoff; keep running.
- [ ] Enrichment projection and stable editable source with source/map manifests.
- [ ] Unchanged real-assembler output compared against export baseline bytes.
- [ ] Off-machine build and reviewable baseline/candidate/live conflict plan.
- [ ] Installed editor demonstration: attach, edit, build, pause, apply, resume.

## 3. Optional correspondence and state suggestions

- [ ] Retain stable symbol IDs and continuation anchors across edited builds.
- [ ] Distinguish observed/declared references from numeric coincidences.
- [ ] Optional PC, saved-return-word and indirect-target suggestions with exact
      expected values and explicit Update / Keep / Edit choices.
- [ ] Changed-size assembly fixture moves a live continuation; update its stack
      word without changing SP; exercise both approval and refusal.
- [x] Manual PC/HL/word scenario edits use the same validated mutation path;
      additional direct register-panel edits invalidate an existing review.
- [ ] Source/address breakpoint binding and stale source-map handling.

## 4. Bounded execution checks

- [ ] Optional stops before indirect jumps and taken returns consume stale state.
- [ ] Actual machine-preparation/interrupt order respected; stale proposals rechecked.
- [ ] POP/exchange and supported value lineage; explicit unsupported effects.
- [ ] Run-with-checks advances the actual machine and never implies a global proof.

## 5. Recovery and integration

- [ ] Durable, asset-bound checkpoint format and close/reopen acceptance.
- [ ] Session/branch/image/epoch transition records and idempotent protocol writes.
- [ ] Explicit data relocation and self-modification conflict dispositions.
- [x] Rollback failure blocks all debugger execution entry points; successful
      restoration clears the block. Host audio/pacing reset is wired into restore.
- [x] Targeted and full Debug/UI regression checks; skips and native acceptance
      gaps recorded below.
- [x] Update current status, user instructions and handoff for delivered scope.

## Verification record

The first foundation increment is implemented in `live_patch.{h,cpp}` and the
native **Debug → Live code update** panel (`--live-update`). All operations run
on the execution owner's thread. The controller also works without a Spectrum.
Private ULA/tape snapshot values exclude live callback ownership. Restoring
breakpoints/watchpoints, memory protection and machine state is tested; transient
analysis is cleared on restore. A patch retains historical observations and byte
revisions invalidate changed code.

`live_patch_test` covers EI deferral, HALT, alternate registers, partial-prefix
rejection, larger replacement code, explicit continuation/PC/HL edits, keeping a
continuation unchanged, stale CPU/memory/device reviews, protection and range
checks, overlap rejection, mid-frame continuation equivalence, tape/keyboard state,
beeper edges, host raster updates, and injected write/rollback failures.
`live_patch_workflow` uses real Pasmo on the before/after example and verifies a
byte-exact disassembly/reassembly baseline before executing the replacement.

Native rendering: the rebuilt debugger rendered five frames and produced a
3200x2000 screenshot. Visual inspection found the new window initially behind
other panels; corrected ordering and a second render showed the panel in front.
Button interaction is **not verified**: after the user unlocked the Mac, the
computer-use service twice timed out selecting the isolated verification app.

Remaining scope is explicit above: no automatic attach/export pipeline, stable
cross-build symbol mapping, suggested relocation UI, guarded execution or durable
checkpoint files. The manual panel does not infer data ownership or merge runtime
mutations. Final checks on 17 September: `cmake --build build -j 4` succeeded with
AppleClang 21 / C++23 / Debug / UI enabled. `ctest --test-dir build
--output-on-failure -j 4` completed 49 registered tests: **47 passed, two skipped**
(`cpu_suite_zexdoc`, `cpu_suite_zexall`, missing external assets). Real Pasmo
assembler/workflow gates and local-ROM tests ran. `git diff --check` and local
Markdown link checks passed. No Release/platform-portability or installed-editor
acceptance is claimed.
