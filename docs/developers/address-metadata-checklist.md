# Address-based observation: developer checklist

Status: agreed direction documented; implementation not started for this increment.
Requested 14 September 2026. This document supersedes the retention and exclusive
state model in [observed disassembly](observed-disassembly-checklist.md), which
remains the record of the earlier implementation and its verification.

## Objective and observed problem

Make the Spectrum debugger accumulate useful knowledge about memory addresses
throughout an analysis session. Run with the actual Spectrum 48K ROM loaded and
write-protected, preserving instruction-level control, T-state accounting,
screen updates and audio behavior.

Today, address evidence depends on the latest record surviving an 8192-event
queue. Repeated execution of the RAM loop at 8000–8006 evicts evidence for the
previous NOPs, even though those bytes have not changed. The display also moves
between Modified and Observed as though they were exclusive facts.

Replace that coupling with address-indexed metadata. Execution and
self-modification are persistent facts; whether current bytes have been
observed executing is a separate, revisable fact. Repeated activity updates
bounded records in place. Do not preserve every modification or code version.

## Agreed semantics

| Property | Meaning and lifetime |
|---|---|
| Executed | This address began a completed instruction; sticky within the analysis session. |
| Used as instruction bytes | This byte was consumed by a completed instruction, including prefixes and operands; sticky and distinct from instruction start. |
| Self-modified | An emulated CPU write changed bytes already observed as code; sticky, including operand changes. |
| Current bytes observed | A complete observed instruction span still matches the revisions captured when it executed; invalidated by any changed byte in that span. |
| Read-only | Current machine protection property; independent of observation and modification evidence. |
| Read/written | Attributed activity and counters, with successful writes distinguished from value changes and refused writes. |

A byte can be used both as data and as code. Overlapping instruction starts are
allowed. Unobserved bytes are not proven data. Modified current bytes must not
be presented as a validated instruction boundary merely because old bytes at
that start executed.

Example at 8000:

1. Execute `LD A,0`: Executed + Current bytes observed.
2. CPU changes operand at 8001: Executed + Self-modified; current bytes not yet observed.
3. Execute `LD A,1`: Executed + Self-modified + Current bytes observed.

Changing a byte and restoring its value still invalidates the old observation.
Same-value writes count as writes but do not invalidate observation. Refused ROM
writes count as attempts, leave bytes unchanged, and do not establish
self-modification. Host loads/edits invalidate affected observations when values
change, but are not attributed to the emulated CPU as self-modification.

## Ownership and bounded data

- [ ] Preserve FastMemory and ObservableMemory as separate efficient policies.
      Rich accounting belongs to the optional MetadataMemory implementation.
- [ ] Keep byte-level accounting in memory; keep completed-instruction evidence
      in a headless address-indexed analysis component. Present both together.
- [ ] Maintain per-byte counters for instruction-stream reads, CPU data reads,
      committed CPU writes, value-changing CPU writes and refused CPU writes.
      Preserve separate sticky instruction-use and self-modification facts.
- [ ] Retain first/latest activity summaries for each supported activity kind:
      sequence, T-state count, and responsible instruction start when applicable.
      Retain latest change's old/new values and latest refused write's attempted
      value. Replace these summaries in place; do not append per-write versions.
- [ ] Maintain completed-execution counts and first/latest completion summaries
      per instruction start. Do not confuse starts with operand-byte usage.
- [ ] Keep at most one latest instruction observation per start, including actual
      captured bytes, span, and their revisions. It may become invalid for current
      memory; replace it on the next completed execution without retaining a
      version chain. Counts and sticky facts survive replacement.
- [ ] Ensure an operand change is visible both at the changed byte and on the
      affected instruction row. Preserve sticky self-modification at an affected
      start even when a later instruction there has a different length.
- [ ] Bound all storage independently of execution duration, including auxiliary
      indexes and capture buffers. Document actual allocation and limits rather
      than assuming 1 KiB per address is necessary. The user's 64 MiB example
      establishes room for richer metadata, not an exact layout requirement.
- [ ] Define counter/revision overflow handling so wraparound cannot silently
      validate stale observations. Treat byte revision and access count separately.

## Observation and execution boundaries

- [ ] Separate instruction-stream access, CPU data access, machine/device access,
      debugger inspection and host mutation. Rendering/disassembly/inspection
      must not increment CPU read counts or cause writes.
- [ ] Attribute CPU activity to the instruction's original start across prefix
      stages; preserve pending capture across bounded execution slices.
- [ ] Audit proxy assignments and read-modify-write instructions for missing or
      duplicate reads/writes. Distinguish internal implementation reads from
      emulated accesses; do not claim a cycle-exact bus trace.
- [ ] Keep machine/device reads distinct; do not let Spectrum display rendering
      inflate CPU data-read counts. Separate device counters are optional here.
- [ ] Keep hardware interrupt entry distinct from instruction execution; attribute
      its accesses explicitly rather than inventing an instruction or reusing a
      stale writer PC. Debugger control must not use simulated interrupts.
- [ ] Publish instruction evidence only for completed instructions. Retain explicit
      incomplete/truncated status when a bounded capture cannot establish a span.
- [ ] Handle self-overwriting instructions using bytes/revisions actually read:
      completion must not validate an observation already changed by that instruction.
- [ ] Preserve existing write observers, protection, watchpoints and SMC behavior.
      Metadata must be coherent when committed-write observers are notified.

## Retention and lifecycle

The intended lifecycle distinguishes CPU state from analysis state. The following
rules must be made explicit at the controls and call sites before implementation
is declared complete; report a conflict with existing reset/load semantics rather
than silently choosing broader behavior.

- [ ] Pause/resume and stepping preserve all accumulated metadata.
- [ ] CPU-only reset preserves analysis; explicit Clear analysis starts a new
      analysis epoch without changing machine bytes or protection. Define how
      CPU cycle resets are represented: analysis sequence/epoch disambiguates
      activity even when the CPU T-state counter restarts.
- [ ] Starting a fresh program/session clears analysis. Treat replacement program
      loading as an explicit boundary, distinct from edits within the current
      session. Do not accumulate cross-build evidence implicitly.
- [ ] Abandon pending instruction capture safely when resetting execution or
      clearing analysis; never join bytes from opposite sides of that boundary.
- [ ] Remove address evidence's dependence on the 8192-entry history queue.
      Queue eviction must have no effect on flags, counts or current validity.
- [ ] If the existing recent-history view remains, label it as a bounded optional
      inspection aid. It is not the evidence store; do not expand its retention.
- [ ] Keep analysis in memory for this increment. Save/reopen and version archives
      are outside scope; sticky means within the explicit analysis lifecycle.

## Presentation

- [ ] Show independent properties together: for example ROM / Read-only /
      Executed / Current bytes observed, or RAM / Executed / Self-modified /
      Current bytes not yet observed. Avoid an exclusive Modified-or-Observed label.
- [ ] Make persistent facts and current observation validity understandable
      without relying solely on colors; expose counts and latest activity in
      address details/tooltips, including last reader/writer and changed values.
- [ ] Replace eviction-driven Not retained in address browsing. Do not substitute
      a confident status when instruction capture was genuinely incomplete.
- [ ] Preserve full address browsing, overlapping starts, Go to, navigation
      history and manual-scroll disengagement of Follow PC.
- [ ] Keep protection visible in the actual Spectrum ROM demonstration. Explain
      refused writes independently of changes to RAM or observation validity.

## Acceptance tests and delivery evidence

- [ ] Execute NOPs through 7FFF, then run the 8000–8006 loop well beyond 8192
      instructions. Earlier NOP evidence and counts remain valid and visible.
- [ ] Verify all three stages of the LD A example above. Executed and
      Self-modified survive repeated rewrites and re-execution.
- [ ] Verify same-value writes, changed-then-restored bytes, operand/prefix changes,
      overlapping starts, address wraparound and self-overwrites.
- [ ] Verify generated code written before its first execution is recorded as
      written then executed, not automatically classified as self-modified.
- [ ] Verify CPU data-read/write attribution, read-modify-write accounting, host
      edits, refused ROM writes and exclusion of inspection/device reads.
- [ ] Verify prefix continuation, incomplete captures, interrupt transitions and
      each reset/clear/load lifecycle boundary.
- [ ] Demonstrate stable bounded allocations after warm-up during a long modifying
      loop; verify counters continue and no modification/version list grows.
- [ ] Run CPU policy parity and Spectrum policy/batch-versus-instruction tests:
      compare CPU results, T-states, frame deadlines, raster and audio events.
- [ ] Measure metadata-enabled execution overhead separately from the bare CPU;
      report build mode and observations without claiming noisy runs as speedups.
- [ ] Run the actual Spectrum debugger with its 48K ROM loaded read-only. Verify
      ROM properties and execution evidence, RAM mutation behavior, pause/step/run,
      backward browsing and continuing screen updates at the native GUI boundary.
- [ ] Update user documentation, this checklist and the handoff with passes,
      skips, limits and actual GUI evidence. Keep unverified items unchecked.

## Deferred and architectural escalation points

Control-flow edges, graph visualization, basic blocks, candidate routines,
version archives, historical replay and disk persistence are subsequent work.
Retain responsible instruction addresses now, but do not build an unbounded
read/write relationship graph as part of byte accounting.

Bring back any requirement to change machine ownership/timing, introduce runtime
memory-policy swapping, retain historical versions, select a banked-memory
identity model, or change the agreed analysis lifecycle. The current scope is
the Spectrum 48K address space; no such expansion is authorized by this checklist.
