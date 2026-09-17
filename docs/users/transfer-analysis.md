# Deterministic transfer analysis

**Status:** headless transfer, ordinary continuation, bounded value, constructed-transfer and stack-reconstruction analysis, 16 September
2026. Automatic runtime capture and routine recovery remain unimplemented.

Analyze a bounded file of recorded instruction effects without running the CPU:

```sh
build/z80_analyze transfers --source tests/fixtures/analysis/conditional-call.json
```

The command writes a JSON report to stdout. It does not modify the capture or an
analysis project. Use ordinary shell redirection to retain a report. The supplied
fixture is synthetic input; the core test separately exercises the same case
against the CPU and copies actual instruction history.

The fixture describes CALL NZ,$8003 at $8000. Its destination is also the next
instruction's address. Captured entry flags distinguish taken and untaken calls;
the sample without flags has an unresolved outcome. PC equality alone is never
treated as evidence that a conditional call was untaken.

## What the report means

- `occurrences` retains one analysis per input observation, in input order.
- `edges` groups source address, executed bytes, instruction mechanism, resulting
  PC and outcome, with every supporting sample ID. Different byte sequences at
  the same address remain distinct. Different memory revisions with equal bytes
  may share an edge; their individual occurrences remain in the capture.
- `encoded_target` is a decoded possible target, even for an untaken instruction;
  `observed_next_pc` is the actual supplied successor. They are separate facts.
- `taken` is true, false or null (unresolved). It describes an instruction's
  architectural transfer, not whether a logical function call/return occurred.
- `return_instruction` does not establish a matched return. PUSH/RET dispatch
  still appears as a return instruction. A separate `continuation` finding can
  match an ordinary CALL/RST using complete stack-access evidence.
- `completeness` describes captured transfer inputs, target basis, matched or unresolved
  continuation relationships, one-observation scope and explicit unresolved
  reasons. No probability or global symbol score is invented.
- `destination_sets_closed` is always false. Repeated identical transfers do not
  rule out new callers, entries, mechanisms or destinations.

Mechanisms cover sequential execution, jumps, indirect jumps, calls, restarts,
return instructions, interrupt-return instructions, block repetition, HALT and
machine transitions. Machine transitions are not automatically classified as
interrupts. Inconsistent context is reported; it is not silently corrected.

## Evidence interchange boundary

`z80-transfer-capture` version 2 is a bounded interchange format for these tactics,
not a run/session persistence schema or a resumable snapshot. Each file supplies:

- A nonempty `source` and explicit `limitations` supplied by the producer.
- Unique sample IDs; a transient event sequence is not inferred to be a durable
  run identity. Separate files require appropriately scoped source/sample IDs.
- Per sample: kind, start/resulting PC, sequence, cycles, instruction read count,
  completeness, copied instruction bytes and optional per-byte revisions.
- Optional entry flags, B, HL, IX and IY. Missing values are JSON null, not zero.
- Optional `stack` evidence: SP before/after, complete-data-access flag, predecessor
  sample ID and instruction-level reads, committed writes and refused writes.

Version 1 inputs remain readable and contain no stack evidence. Writers emit
version 2; reports use version 7 with separate transfer, continuation, value, constructed-transfer and stack-reconstruction tactic
versions, plus a versioned resolver. Stack accesses are bounded to 256 per sample; the writer rejects a
serialized capture larger than the reader's 16 MiB limit.

`stack.previous` is an explicit producer assertion of uninterrupted execution
without omitted host mutations since the named sample. It must identify the
immediately preceding sample to preserve lineage. Matching numeric sequence
numbers never supplies that assertion. This does not define reset/run/epoch
semantics for the debugger; production runtime capture still awaits that decision.

## Ordinary continuation matching

```sh
build/z80_analyze transfers --source tests/fixtures/analysis/continuations.json
```

For the return in this synthetic example, the report includes:

```json
{
  "status": "matched",
  "call_sample": "call-outer",
  "explanation": "This return consumed continuation $8003 saved by sample call-outer.",
  "tactic": "z80-stack-continuations/1"
}
```

A call must actually have written the expected two continuation bytes at the
expected stack addresses. A return must read those same, still-tracked bytes,
advance SP appropriately and reach that continuation. Nesting and recursion use
creation identities and stack slots, not numeric return addresses alone.

Writes invalidate saved-byte lineage even when they write the same value. Refused
writes do not. Consuming a continuation removes its identity even though RAM may
still contain its old bytes. Missing access records, contradictory effects,
capture gaps, machine transitions and unmodeled SP changes discard older lineage.
New calls after a gap can establish new local evidence.

Balanced register PUSH/POP and jumps can preserve a caller's continuation. A
PUSH/RET dispatch does not become a matched ordinary return simply because its
numeric destination equals an earlier saved return address. Register-mediated
return-role recognition is a separate constructed-transfer tactic described below;
stack reconstruction has its own stage described below.

Explanations are attached to individual observations and their supporting call
IDs. They are suitable input for future assembly comments, but are not yet
inserted into exported assembly. A match does not establish a function boundary,
exclusive entry point or complete calling convention.

Unsigned 64-bit counters and revisions use canonical decimal strings. Addresses
and bytes are bounded JSON integers. Unknown schema fields/versions, duplicate
IDs and malformed ranges/counts are rejected. The current limits are 8192 samples,
256 instruction bytes per sample and the shared 16 MiB JSON input limit.

The C++ API offers `WriteTransferCapture`, `ReadTransferCapture`,
`ClassifyTransfer`, `AnalyzeContinuations`, `AnalyzeAddressValues`,
`AnalyzeConstructedTransfers`, `AnalyzeStackReconstruction`, `ResolveTransferFindings` and `TransferReport`. Reports include tactic versions and
the canonical capture hash. Identical input and tactic versions produce identical
reports, including after save/reopen. This hash identifies input; it does not
authenticate its producer or establish hardware fidelity.

The analyzer never rereads current CPU memory to rewrite old observations. A
producer must copy evidence before history eviction. Automatic debugger capture,
run/epoch identity, full register and memory/stack capture, source-image binding,
and attachment to symbol claims remain subsequent integration work. The existing
runtime-identity decision remains pending before capture hooks are installed.

## Bounded address-value tracing (rung 4)

```sh
build/z80_analyze transfers --source tests/fixtures/analysis/value-origins.json
```

This synthetic example executes:

```asm
8000: CALL $8100       ; writes continuation $8003 to the stack
8100: POP DE           ; reads those bytes into DE
8101: EX DE,HL         ; carries their lineage into HL
8102: JP (HL)          ; observed target $8003
```

The jump has `value_origin.status: "traced"` and a root in `value_graph.nodes`.
Following each node's `inputs` reaches the original `call_continuation` at
`call-outer`, through the recorded writes, reads and exchange. Every node names
its supporting sample, operation, numeric value and width. Memory nodes also
name the accessed address. Nodes are report-local identities, not symbol IDs.

- `traced`: all value dependencies reach modeled creation events in this capture.
- `partial`: the numeric target is supported, but some history ends at an entry
  register or memory value predating the trace.
- `unresolved`: required evidence is missing, contradictory, unsupported or over budget.
- `not_applicable`: the instruction has no indirect/stack target to explain.

These statuses concern this value derivation only. They do not establish a
logical return, routine boundary, exhaustive target set or hardware fidelity.
The constructed-transfer stage separately interprets the popped-continuation
usage above. A coincidentally equal immediate address has different ancestry.

Supported effects include immediate register loads, unprefixed byte copies and
(HL) loads/stores, EX/EXX and EX (SP), register PUSH/POP, absolute word loads/stores,
16-bit ADD and INC/DEC, unprefixed register-byte INC/DEC, LD SP, CALL/RST/RET and
indirect jumps. Value tactic v3 also supports BIT, CP, SCF/CCF and AND/XOR/OR
(register, immediate and applicable HL/indexed memory operands). BIT and CP
validate memory reads while preserving their operands. Logical operations derive
the accumulator when its operands are known; otherwise only accumulator lineage
is lost. These operations discard flag-byte lineage and record that limitation
on their findings; a later captured flag value starts a new evidence boundary.
Unrelated register, memory and continuation lineage survives. Flags are not yet
tracked individually, and logical arithmetic never establishes byte-copy identity
even when its result equals an input.

HL, IX and IY word forms are supported where applicable. Other
operations conservatively discard lineage. The graph records arithmetic width
and wrapped results; pointer reads retain their source locations and address
inputs. It does not infer an entire table from one accessed entry.

Each observed committed write creates a new memory-value node, even for an equal
byte. Refused writes preserve the old version. Missing continuity discards old
lineage; new immediate values can start fresh chains. Unsupported operations,
unexplained accesses or contradictions also discard lineage. The budget is
32,768 nodes per report; exhaustion is explicit and stops subsequent tracing.
The existing capture size/access limits still apply.

`target_basis` reports the strongest available target evidence, such as
`matched_call_continuation`, `traced_value_origin` or
`partially_traced_value_origin`. `instruction_effect.target_basis` preserves the
original instruction-only tactic's finding. Earlier observations are unchanged.
This remains headless analysis of supplied captures; automatic runtime capture,
ROM-wide application and assembly-comment projection remain integration work.

## Resolution after any stage

The same report holds the raw findings and their current resolved presentation.
Each tactic retains its own limitations, even when another tactic resolves them.
The resolver can run after any currently implemented stage:

```sh
build/z80_analyze transfers --source tests/fixtures/analysis/value-origins.json --through effects
build/z80_analyze transfers --source tests/fixtures/analysis/value-origins.json --through continuations
build/z80_analyze transfers --source tests/fixtures/analysis/value-origins.json --through values
```

`stack` is the default. This selects which tactics run, not just a display filter.
A tactic that has not run has a null finding, distinct from `not_applicable` or
`unresolved`. The capture hash is unchanged across stages.

Each occurrence has a versioned `resolution` containing its current comment,
target basis, continuation relationship, remaining unresolved dependencies and `resolved_dependencies`.
Each covered dependency identifies the original tactic/reason and the tactic
that supplied the missing evidence. Raw `instruction_effect`, `continuation`
and `value_origin` findings remain available alongside it. The compatibility
fields `target_basis` and `completeness.unresolved` reflect this resolved view.

For the POP in the sample, continuation analysis reports that register provenance
is not tracked by that tactic. After successful value analysis, the roll-up says
that the popped bytes are tracked into registers, and records why the earlier
limitation is covered. Contradictory or unavailable value evidence does not cover
it. Value ancestry alone still does not establish a logical return role.

`sites` groups occurrences by address and executed bytes, retaining distinct
comment variants and the supporting sample IDs for each. Its combined comment
includes every variant and keeps possible additional usages open. Changed code
bytes at the same address produce a separate site. Equal bytes across revisions
may share a view; this does not establish identical memory context or universal
behavior. Original revision metadata remains in the capture.

Resolution reads existing findings without modifying them. It is deterministic
and may be rerun as new evidence arrives. The ancestry summary examines at most
4,096 unique nodes per occurrence; exhaustion is explicitly reported and leaves
the full bounded value graph intact. These report comments are ready for review;
projection into exported assembly remains separate work.

## Constructed transfers (rung 5)

```sh
build/z80_analyze transfers --source tests/fixtures/analysis/constructed-transfers.json --through constructed
```

The fixture contains three independent synthetic paths, with explicit capture
gaps between them. The new stage runs after value tracing and adds a separate
`constructed_transfer` finding to each occurrence:

| Pattern | Established by this tactic |
| --- | --- |
| `popped_continuation_jump` | An observed register-mediated return: both original CALL/RST continuation bytes were popped from that invocation's stack slot, carried unchanged into the indirect jump, and SP is restored. |
| `pushed_target_ret` | RET consumed the target bytes written by a particular PUSH. This alone does not establish a logical return or call. |
| `continuation_preserving_indirect_jump` | The indirect jump leaves the live caller's saved continuation intact at the current SP. A helper and an internal jump remain possible interpretations. |

Findings name supporting CALL/POP/PUSH samples and the target's value-graph root.
The resolver incorporates them without replacing earlier tactic findings. The
sample's jump now says `Observed register-mediated return to $8003`, while the
`values` stage continues to show only the value ancestry and unresolved role.
Resolved continuation relationships also appear in `completeness.continuation`;
the raw ordinary-continuation result remains in `continuation`.

Identity is propagated in one forward pass over the bounded value graph. Each
byte retains its original low/high position. Only modeled copies, exchanges,
splits/joins and memory data lineage preserve that identity. Memory-address
inputs never confer identity on the data read. Arithmetic deliberately breaks
identity even when it produces the same numeric address; this tactic does not
prove algebraic equivalence. A non-PUSH overwrite breaks PUSH-write identity,
even at equal value; refused writes leave it intact.

A register return requires the most recent live captured invocation and its
recorded stack slot, not just an ancestor mentioning a CALL. Consuming a call's
continuation retires that identity, including when a PUSH/RET consumes its bytes.
Missing continuity, unsupported value effects and unclassified possible exits
conservatively discard lifecycle knowledge. Partial value origins may still
support a PUSH/RET finding when the actual pushed/read bytes establish that
specific mechanism; their earlier value provenance remains partial.

The next stage handles supported stack reconstruction and caller-skipping cases.
Arbitrary stack reconstruction, exclusive helper/function classification and
ROM-wide capture remain open.
These tactics do not modify CPU behavior, source labels or exported assembly.

## Stack reconstruction and caller-skipping (rung 6)

```sh
build/z80_analyze transfers --source tests/fixtures/analysis/stack-reconstruction.json --through stack
```

This synthetic fixture demonstrates an outer return after popping the inner
continuation, a saved-SP restoration, an explicitly prepared return address, and
a replaced continuation. `stack` is the default stage; `--through constructed`
provides the earlier view using the same capture and unchanged raw findings.
Report v6 adds a separate `stack_reconstruction` finding, supporting sample IDs,
skipped-call IDs, and value-graph roots. Capture versions 1 and 2 remain readable.

The deterministic rules distinguish:

- **Caller-skipping exit:** exact bytes identify a live older CALL/RST
  continuation, SP matches that caller's restored context, and recorded POPs or
  supported SP adjustments prove removal of every intervening continuation.
- **Reconstructed continuation return:** exact original continuation bytes were
  carried through copies/writes and consumed again with the caller's SP restored.
  If they are consumed on a different stack, the report identifies the transferred
  continuation while leaving caller-stack restoration unresolved.
- **Substituted continuation transfer:** recorded writes replaced bytes in a
  captured call's original stack slot, and RET consumed those bytes. Equal numeric
  addresses do not establish the original continuation's identity.
- **Saved-SP restoration:** value lineage leads back to a recorded SP through
  supported copies, spills/reloads, and affine 16-bit pointer arithmetic. Loading
  an equal immediate address is recorded as an assignment, not proven restoration.
- **Prepared continuation:** a PUSH/RET dispatch leaves a separately pushed literal
  address at SP. It remains a candidate until an observed later RET consumes that
  same pushed value. The later finding names the preparation and dispatch; it
  does not rewrite the earlier candidate.

A POP records removal from the stack, leaving later register use open. Explicit
SP changes are reported independently of whether a restoration is proved.
Caller-skipping rules cover POP, one/two-byte upward SP adjustments and explicit
assignments between live invocation slots. Other stack arrangements remain
unresolved rather than being inferred from numeric address ordering.

Value tactic version 2 retains before/after SP roots for explicit SP operations
and SP snapshot/adjustment lineage. Arithmetic preserves affine SP provenance
only for supported forms; it still does not preserve return-address identity.
Data-read address dependencies never become identity of the data read.

The resolver selects a supported combined explanation, preserves all raw tactic
findings, and records which earlier limitations are covered. Gaps, contradictory
or unsupported value effects, interrupt-return boundaries and exhausted value
budgets discard uncertain reconstruction state. Consumed continuation identities
cannot be reused merely because their bytes remain in RAM. Prepared candidates
are invalidated by consumption or committed overwrites, including equal-value
writes; refused writes preserve them.

These are observations about supplied execution paths, not closed function
contracts. Automatic debugger capture, arbitrary symbolic stack recovery and
assembly-comment projection remain separate work.

## Transfer graph and caller metrics

Report v7 adds `transfer_graph` after every selected stage. Existing occurrences,
edges and raw findings remain present. The graph tactic is `z80-transfer-graph/1`.

```sh
build/z80_analyze transfers --source capture.json --through effects --format text
build/z80_analyze transfers --source capture.json --format json > report.json
```

JSON is the default full report. Text is a readable graph/metric summary with
source addresses, report-local site IDs, destination addresses, edge categories
and counts; it does not replace the detailed occurrence evidence in JSON.

- `sites`: deterministic address/byte-variant IDs, successor counts and supporting
  samples, outgoing destinations, category counts and conditional outcomes.
- `edges`: typed successor groups with actual destination, encoded target when
  present, original architectural mechanism/outcome, supporting samples and
  resolved continuation-usage variants. Later tactics can refine usage without
  changing the architectural edge or duplicating its occurrences.
- `destinations`: reverse edge indexes, taken CALL/RST caller metrics, separate
  category and continuation-relationship metrics, and concise proposed comments.
  Same-address captured site candidates do not establish which target encoding
  executed after a transfer; missing target instructions remain explicit.

`callers` counts only consistent, confirmed taken CALL/RST occurrences. Untaken
calls contribute fall-through; missing outcomes and contradictions are unresolved.
Jumps, RET dispatch and machine transitions do not inflate architectural call
counts. Continuation relationships provide a separate view of those same events;
do not add the two views' totals.

A metric retains its sample IDs, source-variant IDs, distinct source addresses,
destinations and counts. A thousand calls from one site and one from another
produce 1,001 occurrences and two sites. Changed bytes at the same source address
produce another variant but not another address. Conditional counts concern
retained executions of that instruction, not whole-program coverage.

Counts are complete for the supplied capture only, bounded by its 8,192-sample
limit; over-limit captures are rejected rather than silently truncated. Duplicate
sample IDs are rejected, and rerunning a report does not accumulate counts.
There is no cross-capture deduplication, image binding, static-reference union,
routine graph or automatic assembly export of these comments yet. No observed
callers means zero in this selection, not proof that no callers exist.
