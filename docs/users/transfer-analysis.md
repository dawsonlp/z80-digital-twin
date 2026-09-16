# Deterministic transfer analysis

**Status:** headless transfer and ordinary continuation analysis, 16 September
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
version 2; reports use version 2 with separate transfer and continuation tactic
versions. Stack accesses are bounded to 256 per sample; the writer rejects a
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
returns, stack restoration and general value provenance remain later tactics.

Explanations are attached to individual observations and their supporting call
IDs. They are suitable input for future assembly comments, but are not yet
inserted into exported assembly. A match does not establish a function boundary,
exclusive entry point or complete calling convention.

Unsigned 64-bit counters and revisions use canonical decimal strings. Addresses
and bytes are bounded JSON integers. Unknown schema fields/versions, duplicate
IDs and malformed ranges/counts are rejected. The current limits are 8192 samples,
256 instruction bytes per sample and the shared 16 MiB JSON input limit.

The C++ API offers `WriteTransferCapture`, `ReadTransferCapture`,
`ClassifyTransfer`, `AnalyzeContinuations` and `TransferReport`. Reports include tactic versions and
the canonical capture hash. Identical input and tactic versions produce identical
reports, including after save/reopen. This hash identifies input; it does not
authenticate its producer or establish hardware fidelity.

The analyzer never rereads current CPU memory to rewrite old observations. A
producer must copy evidence before history eviction. Automatic debugger capture,
run/epoch identity, full register and memory/stack capture, source-image binding,
and attachment to symbol claims remain subsequent integration work. The existing
runtime-identity decision remains pending before capture hooks are installed.
