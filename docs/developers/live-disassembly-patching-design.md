# Live disassembly, code replacement and state adjustment

**Status:** design baseline; implementation authorized by the user on 17 September.
Delivery is tracked in the [development checklist](live-disassembly-patching-checklist.md).
**Date:** 17 September 2026.
**Inspected baseline:** `c4be116`; source inspection, not a fresh execution test.
**Scope:** a continuing Z80/Spectrum session, editable enriched disassembly, saved machine state, code replacement and optional state adjustment.

## 1. Intended experience

Load a program and let it run. Open a debug session without restarting or leaving
the program paused. Its assembly opens in the editor, enriched with the knowledge
available about that image and its execution. Unedited assembly reproduces the
selected binary exactly. Edit and assemble while the machine continues running.
Pause, save state, apply the new code, review suggested state adjustments, and
resume or step. State edits can accommodate moved code or deliberately construct
a test scenario. Restore the saved state when the experiment is unsuccessful.

Variable-size edits and moved symbols are part of the intended experience.
Address-preserving patches are useful examples, not a product restriction.
Complete automatic migration is not a prerequisite for delivering the workflow:
the system can save, replace, expose uncertainty and offer manual adjustments
before it understands every possible live reference.

The default is to preserve machine state except for the code/data writes and
state adjustments the user selects. Suggested adjustments require user approval.
Declining a suggestion is allowed; it is not silently interpreted as approval,
and does not force a restart. Applying code leaves the machine paused.

## 2. Existing foundations and gaps

| Observed implementation | Design implication |
|---|---|
| [DebugSession](../../debugger/exec/debug_session.h) drives the CPU already owned by the debug Spectrum machine; instruction completion is explicit. | Retain one machine and one execution owner. Add commands at its execution boundary. |
| [SpectrumMachine](../../machine/spectrum/spectrum_machine.h) owns frame scheduling and devices. | Saving registers and RAM alone cannot establish a resumable Spectrum checkpoint. |
| [Semantic export](../../debugger/analysis/analysis_export.cpp) emits assembly, symbol references, a source map and manifest, tied to an immutable image. | Reuse export, but introduce explicit image versions and edited-build mappings. Export maps are not already relocation maps. |
| [Analysis projects](../../debugger/analysis/analysis_project.h) have stable symbol IDs, image bindings and provenance. | Preserve symbol identity across a version transition without silently rebinding the old project. |
| [Transfer analysis](../../debugger/analysis/transfer_analysis.h), value and stack tactics interpret supplied captures. | Reuse evidence rules where applicable. This is not yet live migration or complete runtime capture. |
| [MetadataMemory](../../src/memory/metadata_memory.h) distinguishes host mutation and CPU activity; raw writes bypass observers/protection. | Code deployment needs its own checked operation and machine notification contract. A raw copy is insufficient. |
| [Register panel](../../debugger/ui/panels/registers_panel.cpp) edits registers when paused. | Route future edits through revisioned state transactions so previews cannot become silently stale. |
| [Build tooling](../../tools/spectrum_dev.py) assembles with external Pasmo and launches a fresh process. | Add attach/export/apply operations without changing ordinary standalone assembly into a daemon-dependent operation. |

The current [status](../reference/status.md) explicitly excludes live reload,
automatic runtime capture and resumable machine persistence. Existing historical
test results establish their covered foundations, not this proposed experience.

## 3. Separate the identities

The design needs a small set of distinct records:

| Record | Meaning |
|---|---|
| Session | The machine the user is working with; survives attach/detach and code replacement. |
| Execution branch | One continuation history. Restoring an earlier checkpoint creates a child branch rather than rewriting subsequent history. |
| Image version | Immutable program bytes, load ranges, build identity and version-specific symbol locations. |
| Capture epoch | Observation continuity and coverage limits within a branch. Late attach cannot recover unrecorded earlier execution. |
| Export baseline | Exact selected bytes, image/context identities, evidence cutoff and source/map hashes used to generate editable assembly. |
| Checkpoint | Restorable machine state at a particular boundary, plus compatible machine/asset identities. |
| Patch transaction | Baseline, candidate build, paused-state revision, writes, accepted adjustments and resulting image/state identities. |

Program image identity is not whole-machine memory identity: live RAM can differ
from compiled initial data. A build hash never certifies all live RAM after patching.

Recommended lifecycle: pause/resume stays in the same branch; code replacement
adds an explicit version transition and starts a new capture epoch; restoration
creates a new branch rooted in the checkpoint. A restart is a new execution
branch with its actual reset scope recorded. The older
[runtime identity proposal](analysis-implementation.md#architectural-input-needed-runtime-evidence-identity)
treated image loading as a new run. This proposal refines replacement during a
continuing session; the terminology and schema must be reconciled before implementation.

## 4. Attach and produce a stable editing baseline

The runnable application must expose a session endpoint before an editor can
attach. Launching a replacement process with the same binary is not attachment.
Opening the editor does not transfer machine ownership to the LSP or editor.

Take a coherent capture between completed instructions, copy the required memory
and evidence, and continue execution before source generation and file I/O.
This short synchronization is not a user-visible stopped debug session. Do not
claim zero execution latency. Never assemble a source image from reads that span
different machine moments. A pending prefix sequence must complete before this
capture; an exhausted completion budget reports that capture is unavailable,
without resetting the CPU to manufacture a boundary.

The user can distinguish two export bases:

- **Loaded image:** reproduces the original or selected compiled image.
- **Captured live image:** reproduces selected current memory, including changes
  made by the program. This is the natural default for editing what is running.

Each export records its ranges and explicitly names the baseline. A tape file,
container or compressed input is not reproduced merely by reassembling loaded
RAM. Unknown data and exceptional encodings use byte declarations where needed.
The existing Pasmo range limit remains explicit: individual exported ranges are
nonempty and at most 65,535 bytes. A full memory checkpoint is a separate artifact.

Enrichment includes selected names, descriptions, code/data interpretations,
references and applicable runtime findings. Historical findings remain tied to
their observed bytes; unmatched findings remain visible as historical evidence.
Missing capture is shown as missing. Source text is stable while being edited;
new observations update decorations or an inspection panel, never silently
rewrite an edited file. Explicit refresh uses a reviewable merge/new export.

Before accepting the initial editing baseline, assemble it and compare the
complete selected output with the baseline bytes. Record the result and tool
identity. Byte equality is a mechanical gate, not a claim to have recovered the
original author's source or intent.

## 5. Build correspondence, not just an address delta

Assembler symbols make relocation tractable when a stable entity survives an
edit. They do not say whether an arbitrary register or memory word denotes that
entity. These are separate questions:

1. Where did this symbol or instruction continuation move?
2. Does this particular state value refer to it?

Retain a sidecar mapping stable symbol IDs to emitted labels. Build output maps
those labels to new addresses. Renames, deletion, duplicate labels and removed
anchors produce explicit correspondence questions; numeric equality or similar
spelling cannot silently create identity. Arbitrary source editing may require
the user to associate an old identity with a new label.

For live control flow, routine entry symbols are not sufficient. Capture/export
should supply optional stable local anchors for known continuation locations and
the paused instruction. The candidate build must locate those anchors. An old
`routine + offset` is valid only when correspondence for that interior position
is established; insertion inside the routine can invalidate the offset.

Return continuations are especially suitable: a CALL saves the address following
that call. A retained continuation anchor identifies where execution should
continue in the new source. A deleted/restructured call site may have no unique
successor; offer a user-selected continuation, not a guessed address shift.

The initial correspondence mechanism can use explicit labels and assembler symbol
output. General instruction matching, macro expansion identity and arbitrary
source transformation inference are later refinements. Failure to match limits
assistance, not the user's ability to construct and run an experiment manually.

## 6. Saved state and restoration contract

Create a checkpoint after pausing and before any code or state write. It must
contain enough state to resume the current emulator model:

| State area | Required content |
|---|---|
| CPU | Main/alternate registers, PC/SP, IX/IY, I/R, WZ and all other execution-relevant internal values; IFF1/IFF2, interrupt mode, EI deferral, HALT and cycle count. |
| Memory | RAM, protection/layout, modified ROM overlay if supported, and immutable backing asset hashes. |
| Machine | Frame-active status/deadline, ULA latches, partial-frame raster/write history, FLASH phase, beeper state and pending emulated events. |
| Input/devices | Emulated keyboard state, tape identity, playback position/timing and all device state required by the supported configuration. |
| Debug context | Image/source versions, breakpoint/watchpoint definitions and the evidence cutoff; instruction-control bookkeeping must be restored or explicitly reconstructed. |

Use explicit state structures, not object-memory dumps: CPU/machine objects
contain callbacks, observers and ownership that cannot be serialized as state.
Rebuild those connections on restore. Saving at completed instruction boundaries
avoids promising arbitrary mid-prefix restoration, but EI deferral and HALT still
matter. Save/restore must not call ordinary reset/load paths as a shortcut.

Separate an immediately restorable in-process checkpoint from a durable checkpoint
that survives closing the application. Both belong to the design; delivery may
start with the first, clearly labelled. Do not say “saved to disk” until durable
publication succeeds. A durable format is versioned, checksummed and asset-bound;
missing assets or unsupported state fail explicitly. Crash recovery requires the
durable form and is not implied by transactional in-process apply.

Restoring recovers old code together with old state and creates a new execution
branch. An optional later action can restore old state against a chosen newer
image, but that is another migration transaction. Restoring the machine cannot
undo sound already played or other external effects. Flush/rebase host audio and
presentation queues; define how physical keyboard input is resampled on resume.
Equivalent continuation means equivalent emulated behavior under the same future
inputs and current fidelity model, not new hardware-accuracy guarantees.

## 7. Plan the code and data replacement

Assembly happens off the running machine. Pause for final review, checkpoint and
commit. Compare baseline bytes, candidate bytes and paused live memory, using
symbol/region correspondence where layout moved. A raw offset-wise diff is not
enough for relocation.

Classify destination ranges explicitly:

- New/replaced code and immutable data: candidate build bytes.
- Live mutable data: preserve in place, migrate to a specified destination, or
  initialize from the candidate, as selected by the user.
- Stack and other retained state: preserve unless an explicit adjustment changes it.
- Vacated ranges: retain by default; clearing/reusing them is a visible choice.
- Unknown regions or overlapping destinations: require an explicit disposition.

Code growth can overwrite a variable or active stack even if code relocation is
well understood. Show ownership/range conflicts and the proposed writes before
applying. Copy relocated live data from checkpoint/pre-transaction bytes so
overlapping moves cannot corrupt their own source. Changed data layouts require
a supplied migration or manual scenario setup; symbol movement alone does not
describe a structure conversion.

For self-modifying code, show the captured baseline, assembled result and current
live bytes. Keep a runtime change, replace it, or rebase the edit deliberately.
Retaining a live code mutation means that range may differ from candidate output;
record the actual installed bytes and applicability of source maps accordingly.

ROM remains protected by default. An experimental ROM change needs an explicit
ROM-overlay operation and a new machine/image identity; it must not happen because
a low-level host copy bypasses protection. First delivery can report ROM replacement
as unsupported while supporting RAM replacement.

## 8. Optional state adjustment

State changes have two purposes: compatibility suggestions and deliberate scenario
construction. Both use the same reviewable edit mechanism, with different reasons.
No bulk “fix every word equal to an old address” operation is inferred.

Each suggestion records the state location, expected old bytes/value, proposed
new value, old/new symbolic meaning, supporting evidence, unresolved assumptions,
and applicable image/state revision. Explanations distinguish:

- **Observed reference:** retained execution provenance establishes how this value
  became a return address or pointer, with uninterrupted relevant lineage.
- **Declared reference:** the user or source metadata explicitly identifies it.
- **Possible reference:** a value matches a moved address but its role is unknown.

All are optional proposals. Confidence in value provenance does not certify the
new routine's register/data contract. Missing evidence does not prevent manual edits.

### Stack example: repair the stored continuation

Suppose `SP=$FF00`, memory at `$FF00/$FF01` contains little-endian `$8123`, and
an observed CALL established that word as `after_draw`. New assembly places
`after_draw` at `$8130`. Offer:

> Saved return address at [$FF00]: after_draw moved from $8123 to $8130.
> Update this return address? **Update / Keep / Inspect**

The proposed writes are `$30,$81` to those two stack bytes. SP remains `$FF00`.
SP names the stack storage, not the code destination stored there. Moving the
stack region itself is a separate operation: copy the relevant state, adjust SP
and any established references, with separate approval. A random matching word
on the stack is not necessarily a return address; it can be saved data.

Nested calls can leave multiple established continuations. List known affected
ones at apply time without claiming the stack can be exhaustively unwound.
Interrupt continuations and unconventional stack use retain their distinct evidence.

### Indirect jump example

For a pending `JP (HL)`, HL is itself the target address; the instruction does not
read a target word from memory at HL. If HL denotes moved symbol `draw`, offer:

> JP (HL) will use $9000. draw now assembles at $9020.
> Set HL to $9020? **Update / Keep / Edit**

The same applies to IX/IY forms. An observed/declarative pointer relationship
supports a stronger explanation than an equal number. A nearby `POP HL` may
replace HL before that jump; do not eagerly change a value that will be overwritten.

### Current PC and scenario state

If the paused instruction moved, suggest a mapped PC before any instruction
executes. The old numeric PC is not automatically meaningful in the replacement.
If correspondence is absent, offer a symbol/address chosen by the user or keeping
PC explicitly. Show the instruction that would execute at the selected PC.

Allow explicit changes to flags, registers, stack words, memory and supported
device inputs. For example, setting a counter and Z flag to exercise a particular
branch is a scenario edit, not a discovered compatibility repair. PC changes while
HALTed must expose whether the HALT latch is retained or cleared; never silently
change interrupt enable state, stack or timing to make an experiment run.

## 9. Bounded assistance at the next relevant operation

The user's proposed near-term scope is useful: inspect the next jump, return or
stack consumption instead of requiring whole-program state migration. It limits
what the assistant promises, not where compatibility problems can occur.
Old pointers can be used by a data read/write before any jump, and older return
addresses can remain buried for many calls. Surface these limits in the review.

Use two complementary mechanisms:

1. **Immediate review:** inspect current PC and known live reference locations
   against the new build. No execution is needed to propose these edits.
2. **Optional guarded continuation:** execute normally, but stop before a relevant
   operation consumes a possibly stale value. Re-evaluate the actual current state
   and offer an adjustment before that instruction executes.

“Run to next review” advances the real machine and may change state on the way;
the UI must say so. It is not a pure preview. A future sandbox lookahead must run
on an isolated checkpoint clone, suppress external outputs, use bounded budgets
and stop on unresolved inputs. Do not implement prediction by invisibly running
and rewinding the live machine.

| Operation | Review concern |
|---|---|
| `JP (HL)`, `JP (IX)`, `JP (IY)` | The target register and its current reference lineage. |
| `RET`, conditional RET, `RETN`, `RETI` | The actual word at current SP; conditional return consumes it only when taken. Preserve interrupt-return semantics. |
| `POP rr` | The word being consumed; it may be ordinary saved data. Track an established pointer into its destination, or ask about the stack word before consumption. |
| `EX (SP),HL/IX/IY` | Exchange can introduce or replace a continuation without popping it. |
| `LD SP,HL/IX/IY`, other supported SP writes | Stack-storage relocation or reconstruction, distinct from code-target relocation. |
| CALL/RST, PUSH and interrupt entry | New stack writes and continuation creation; establish new-version lineage and invalidate overwritten old values. |
| Direct jumps/calls and relative branches | Assembly resolves symbolic references. Literal targets, raw bytes and references outside the replaced range can remain stale. |
| Pointer reads/writes, `EXX`, register exchanges | Data-pointer use and movement can matter before control transfer; preserve supported lineage and report unsupported effects. |

This is a declared support matrix, not a requirement that all rows ship together.
An initial guard can support indirect jumps and returns, with POP/exchange and
data-pointer assistance added later. Unsupported effects reduce knowledge; they
must not leave old provenance falsely valid.

The guard must inspect what will actually execute under the machine's scheduling
order. An accepted interrupt can change PC and SP before the anticipated user
instruction. Re-evaluate after any machine preparation transition and before
instruction execution. Never freeze or suppress interrupts implicitly to make a
predicted path come true. Pending machine transitions belong in the checkpoint.

Every proposal is revalidated at consumption time. Intervening register/memory
edits, code writes, interrupts and execution can invalidate it. A prior Keep
decision suppresses repeats only for that exact value/reference and image version;
changed state can produce a new question. Partial-byte writes and overlapping
register aliases invalidate or update affected provenance.

Guarded continuation is opt-in after apply. The user can instead run unchanged,
step, make manual edits, or restore. No path is labelled “fully compatible” solely
because all supported guards were satisfied.

## 10. User interaction

The editor supplies **Open running assembly**, **Build**, **Pause and review update**,
**Apply**, **Edit state**, **Step**, **Run**, **Run with checks**, and **Restore checkpoint**.
These describe actions, not an assumed set of final command names.

The review shows the checkpoint, build, memory changes and state suggestions
together. A user can accept selected suggestions, keep current values or enter
alternatives. Show details on demand; do not interrupt for every numeric match.
Group known affected return addresses and pointers into one review, with per-item
choices. A later guard presents one actionable question at the relevant operation.

Suggested adjustment preview:

| Location | Current | Proposed | Reason | Choice |
|---|---|---|---|---|
| PC | `$8040` | `$8045` | Paused instruction anchor moved | Update / Keep / Edit |
| Word at `$FF00` | `$8123` | `$8130` | Observed continuation `after_draw` moved | Update / Keep / Edit |
| HL | `$9000` | `$9020` | Declared target `draw`; indirect jump pending | Update / Keep / Edit |
| `counter` | `237` | `0` | User's requested scenario | Apply / Remove |

Unselected adjustments remain unapplied. There is no implicit “accept all” from
opening or dismissing the panel. Missing automated correspondence permits explicit
manual continuation. Malformed artifacts, unsupported checkpoint state, unresolved
overlapping writes or a stale transaction prevent committing that transaction;
they are integrity failures, distinct from a user choosing an uncertain experiment.

Source/symbol breakpoints can move by established identity. Address breakpoints
remain address-based unless the user changes their binding. Deleted source
locations become unresolved rather than silently attaching elsewhere. Watchpoints
receive the same address-versus-entity distinction.

## 11. Atomic apply and failure behavior

Use this state sequence:

```text
Running -> coherent export -> Running + editable source
        -> candidate build -> Paused + checkpoint
        -> reviewed plan -> Applied, still paused
        -> step / run / guarded run / restore
```

At commit:

1. Check session/branch, paused-state revision, candidate hashes, boundary status,
   writable destinations and all expected old values. Any intervening state edit
   invalidates dependent suggestions; recompute and show changed proposals.
2. Finalize code/data writes and approved state edits as a single plan. An
   adjustment targeting a moved stack/data location uses its final destination.
3. Apply with execution and competing mutation commands excluded. Preserve the
   checkpoint until complete verification succeeds.
4. Read back all writes and state edits; update active image/source bindings and
   record the version transition. Stay paused.
5. On failure restore the pre-apply machine and active bindings. If restoration
   cannot be verified, remain stopped with an explicit failure, never resume a
   partially installed image.

Atomic means no execution or client observes a partially committed installation;
it does not mean the multi-byte copy is one hardware operation. Persistent
transaction journaling and recovery after process failure require a separate
durability acceptance gate.

Host patch events must invalidate stale byte evidence and retain their own origin;
they must not masquerade as CPU self-modification. Notify machine devices of memory
changes at the frozen emulated time where their model requires it, including
display-memory history. Do not accidentally consume T-states or reset the frame.
Rollback also restores these device effects and separates subsequent analysis
history so failed writes do not appear to have been executed successfully.

## 12. Component boundaries

| Component | Responsibility |
|---|---|
| Machine/session host | Sole execution owner; attach endpoint, boundary synchronization, checkpoints, restore and atomic state mutation. |
| Export/build tools | Immutable export baseline, enrichment, external assembly, byte comparison and candidate manifests. Usable from CLI independently. |
| Correspondence service | Old/new symbols and continuation anchors, with explicit unresolved mappings. No live writes. |
| Patch planner | Memory dispositions, state suggestions and revision-bound transaction plan. No live writes during planning. |
| Execution analysis/guards | Current supported reference lineage and stops before consumption; preserve evidence limits. |
| Editor/debugger frontend | Present source, previews and questions; submit explicit user choices and display authoritative results. |

Use a small local session-control protocol shared by CLI and editor; exact transport
is an implementation decision. Requests carry session identity, operation IDs and
expected state revision. Retried apply returns the recorded result rather than
applying twice. Disconnection during review leaves the machine paused; reconnect
queries transaction status. Only one client can commit a state mutation at a time.
The endpoint is local and scoped to the launched session; do not expose arbitrary
remote mutation as an incidental feature.

An editor debug adapter may translate editor actions into these operations. The
LSP remains responsible for language features; neither LSP nor adapter owns a
second execution loop. Machine capture support is optional at launch so the bare
CPU/cheaper runtime is not burdened by all analysis. If a session lacks earlier
observation, attach exposes that limit; changing memory-policy instantiations
in flight is not assumed to be possible without a separate state-transfer design.

## 13. Delivery increments

Each increment implements part of this contract without redefining the final
experience around its temporary limits.

1. **Checkpoint and manual replacement:** validated in-process save/restore;
   continuing session; variable-size range replacement with explicit range
   dispositions; manual register/memory edits; stay paused; source/build identity.
   No complete migration inference required. Expose unsupported machine states.
2. **Enriched editor loop:** attach, coherent export, byte-exact initial assembly,
   stable source, candidate builds and review/apply through the real editor.
3. **Symbol-assisted adjustments:** old/new symbol and continuation mapping;
   optional PC, established stack-word and target-register proposals. Include a
   fixture where inserted code moves a live return target.
4. **Bounded guarded continuation:** stop before supported indirect jumps/returns,
   then extend stack/value operations. Explicit support and stop-reason reporting.
5. **Durable recovery and broader migrations:** close/reopen checkpoints, branch
   records, data moves/layout migration, additional devices and richer provenance.

An early end-to-end slice should combine the smallest parts of 1–3 needed to
demonstrate a moved symbol and a user-approved adjustment. Do not spend the entire
effort building checkpoint infrastructure without exercising the developer loop.

## 14. Acceptance and disconfirmation

| Scenario | Required observation |
|---|---|
| Attach while running | Same session continues; no reset or user-visible stopped state; observations state their actual start. |
| Unchanged enriched export | Reassembled selected bytes match the named baseline exactly, including raw-byte fallbacks and data. |
| Edit while running | Source remains unchanged by incoming observations; failed build leaves machine and active build untouched. |
| Moved return target | Inserting code changes a retained continuation label; proposal updates the stack word only after approval; SP remains unchanged. |
| Moved indirect target | HL/IX/IY adjustment is optional; changed register values invalidate an old proposal; the actual next jump uses the selected value. |
| Changed current instruction | PC correspondence is reviewed before fetch; missing mapping supports manual selection without inventing a mapping. |
| POP then indirect jump | Ordinary saved data is not auto-relocated; an established pointer can be followed or manually adjusted; overwritten HL is not repaired prematurely. |
| Conditional return | Untaken return consumes no word and triggers no fabricated consumption; flag edits force reevaluation. |
| Nested/interrupt continuations | Distinct live occurrences remain distinct; equal words do not establish identity; interrupts invalidate a stale next-operation preview. |
| Growth/data/stack overlap | Preview identifies overlap; staged copies preserve source bytes; no partial writes on invalid plans. |
| Self-modification | Three versions remain distinguishable; chosen runtime/code disposition is reflected in installed-byte identity. |
| Deliberate scenario | User changes flags/counter/PC, steps, and observes the chosen condition; changes are recorded as user decisions. |
| Decline adjustments | Code can be applied and the user can intentionally run with retained state; no hidden repair or forced restart. |
| Mid-frame restoration | CPU, memory, frame phase, tape and beeper continuation match an unmodified control under identical inputs. |
| Apply failure | Injected failure restores pre-apply machine/device state and bindings; UI stays paused. |
| Repeated/retried commands | Same transaction does not apply twice; stale clients cannot overwrite a newer paused state. |
| Durable checkpoint, when claimed | Close/reopen and resume with verified assets; incompatible/corrupt/missing state is rejected explicitly. |

Test negative evidence as well as successful suggestions: equal numeric constants,
deleted labels, shifted interior offsets, unsupported arithmetic, partial register
writes, interrupted captures, stale symbol maps and unrelated stack words must not
produce unjustified automatic reference identities.

Use focused headless tests for state equivalence, correspondence, planning and
failure injection. Prove the actual developer experience in the installed editor
against a running machine: visible changed behavior, retained state, optional
repair accepted and declined, followed by successful restore. Headless tests alone
do not establish editor attachment or usable prompts.

## 15. Decisions proposed for review

The user's requested direction establishes saved state, code replacement, optional
state adjustment and no permanent same-layout restriction. This document proposes
the following more specific decisions, still subject to design review:

- The running machine remains authoritative; attachment does not replace it.
- Export baseline and pre-apply checkpoint are separate captures at different times.
- Stable symbols and explicit continuation anchors supply version correspondence;
  runtime provenance or user declarations establish which state values refer to them.
- A default apply saves state and remains paused. Repair suggestions require explicit
  choices; deliberate uncertain continuation remains available.
- Bounded next-operation checks are assistance with stated coverage, not a proof
  of global compatibility or a mandate to migrate every live reference up front.
- First recovery is in-process and explicitly labelled; durable restoration is a
  distinct deliverable with its own fidelity and asset requirements.
- Code replacement records a version transition; restoration branches history.

Open implementation choices include the local transport/debug-adapter integration,
checkpoint format, stable-anchor editing representation, first supported machine
state profile and precise guard support matrix. These should be resolved against
the first end-to-end acceptance case, without expanding this effort into general
time travel, whole-program correctness proof or automatic data-schema migration.
