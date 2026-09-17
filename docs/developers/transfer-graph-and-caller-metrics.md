# Transfer graphs, caller metrics and assembly understanding

**Date:** 16 September 2026.
**Status:** capture-local graph/metrics implemented; routine graph, cross-capture
aggregation and assembly projection remain planned. This extends Stage 5 of the
[deterministic analysis plan](deterministic-analysis-development-plan.md).

## Purpose

Collect evidence about who reaches a piece of code, through which entry and
mechanism, how often, and under which observed continuation conditions. Use it to
explain relationships in reconstructed assembly and select useful experiments.
High caller counts do not establish purpose, importance or a reusable contract.
Those interpretations require caller context, data meaning and observable effects.

The intended result is a source section that explains its role in the program,
with a concise account of its observed users and alternative entries. A graph
and counts supply supporting structure; they do not replace that explanation.

## Existing evidence and the gap

`TransferReport` already groups successor observations by source address, executed
bytes, transfer mechanism, actual next PC and taken/untaken/unknown outcome. Each
group retains its count and sample IDs. The report also preserves per-occurrence
continuation/value/stack findings and per-site comment variants. Its destination
sets remain open.

Those baseline edges include ordinary fall-through and return successors. They
are not a routine call graph. The first checkpoint below adds incoming-site
metrics, but no routine membership projection or durable cross-run site identity. Capture-local
sample IDs and a free-text source label cannot safely identify events across runs.

Implement the first graph and metric pass over one supplied capture. Later
aggregation requires explicit compatible image bindings and evidence identities;
it must not infer them from matching addresses or filenames. The existing
[runtime identity decision](analysis-implementation.md#architectural-input-needed-runtime-evidence-identity)
remains open. This proposal adds no capture hooks or reset/resume semantics.

## The records to retain

Keep records within the existing evidence/finding/resolution structure. Graphs,
metrics and assembly comments are derived views with declared input scope and
tactic versions, not separately editable sources of truth.

| Record | Information and meaning |
| --- | --- |
| Instruction site variant | Within one capture, source address plus executed bytes. Across compatible images, include explicit image/location identity. Equal bytes at an address do not establish a shared memory revision or invocation. |
| Destination reference | Observed target address and applicable address space/image binding. Attach a target instruction variant only when evidence establishes it; a transfer can target code absent from the extract. |
| Transfer occurrence | Supporting event, source variant, actual successor, architectural mechanism and conditional outcome. Preserve any encoded target separately. |
| Usage finding | Supporting occurrence and dependencies, continuation relationship, constructed-transfer interpretation, unresolved conditions and tactic version. Multiple stages may explain one occurrence. |
| Graph edge | Grouped occurrences with the same relevant source, destination, mechanism and outcome; retain usage variants rather than choosing one permanent role. |
| Routine relationship | Versioned candidate-to-entry/block memberships, applicability and supporting findings. Membership need not be exclusive. |
| Metric finding | Definition/version, input capture or selection, filters, value, supporting sites/events and limitations. Counts must be reproducible from the retained evidence. |

One source address containing two observed instruction encodings has two site
variants. Offer an address-level count alongside variant-level counts, explicitly
named. Repeated execution of one variant increases occurrences, not distinct sites.
Do not manufacture target byte identity from current memory.

## Two graph views

### Observed transfer graph

Start with instruction-site nodes, destination references and typed edges. Build
a block projection later, preserving the underlying instruction evidence when
new entries split blocks. Keep overlapping decodings and changed bytes distinct.

- Taken CALL/RST edges establish observed architectural calls. They do not prove
  that the destination later returns normally.
- Taken direct and indirect jumps remain jump edges. An intact continuation is
  an additional finding, not by itself proof of a tail call or helper role.
- RET-family edges retain the architectural instruction and observed destination.
  Attach supported ordinary-return, caller-skipping or PUSH/RET-dispatch findings.
- Conditional instructions with false conditions contribute a fall-through
  successor and an untaken count, not an observed call to their encoded target.
- Unknown conditions retain the observed successor but stay out of confirmed
  taken-call counts, even when successor and encoded target happen to agree.
- Ordinary fall-through is available for block construction. Count it as an
  alternative entry arrival only relative to a declared entry/grouping view;
  every sequential instruction is not a new routine invocation.
- Machine/interrupt transitions and incomplete evidence have separate categories.
  Do not convert a jump to a conventional vector into an observed interrupt.

Render call/jump structure separately from return flow by default, while allowing
inspection of all edges. Preserve destinations outside the captured instructions
as unresolved nodes rather than dropping them.

### Derived routine call graph

Project the transfer graph through a specific revision of routine candidates.
Initially expose architectural call relationships; extend with supported logical
invocation findings as their rules become available. Keep uncertain transfers
visible without labeling them calls.

A source block shared by two candidates can have ambiguous caller attribution.
Retain that ambiguity or use supported invocation context for the particular
occurrence. Do not duplicate one event into two confirmed caller counts. A helper
call followed by an indirect jump creates two transfer occurrences; it does not
automatically establish two independent logical invocations of the final target.

Multiple entries into one candidate remain separately inspectable. Merging or
splitting candidates changes the routine graph, not the underlying observations.
Cycles identify cyclic reachability; a cycle of jumps alone does not prove
recursion. Recursive invocation requires appropriate call/continuation evidence.

## Metrics and counting rules

All counts describe a named evidence selection. Missing capture cannot be counted
as zero execution. No finite set of observed callers closes the possible caller
set. No frequency below represents elapsed time or CPU cost.

| Metric | Definition and exclusions |
| --- | --- |
| Distinct observed CALL/RST source sites | Cardinality of source variants with at least one confirmed taken CALL/RST into the selected entry. Return edges and untaken calls are excluded. Also offer distinct source addresses. |
| Distinct additional transfer sites | Separate source sets for taken direct jumps, indirect jumps and supported constructed dispatch. These sets can overlap with other classifications; never add them to obtain a unique total. |
| Transfer occurrences | Number of retained events satisfying the selected edge/entry filter. Deduplicate supporting event references when multiple findings explain the same event. |
| Conditional outcomes | Taken, untaken and unresolved occurrences for each source site. The denominator is retained executions of that conditional instruction, not all program executions. |
| Observed outgoing destinations | Distinct applicable target locations per source variant and mechanism, plus unresolved-outcome counts. Many destinations suggest a dispatch site worth investigating, not proof of a table or function family. |
| Entry and usage diversity | Distinct observed entries and mechanism/continuation variants under the selected routine grouping. Preserve counts per entry and set-union totals. |
| Distinct caller candidates | Caller candidates supported by the selected membership revision, with ambiguous or unattributed source sites reported separately. This is distinct from the number of call instructions. |
| Continuation outcomes | Counts of supported matched returns, register returns, caller-skipping exits, dispatches and unresolved cases. A missing return at the end of an extract does not prove non-returning behavior. |
| Experiment presence, later | Number of explicitly identified experiments containing supporting events, with per-experiment counts and setup category. Artifact filenames are not experiment identities. |

For example, 1,000 executions from one source and one execution from a second
source produce two distinct source sites and 1,001 occurrences. Two source sites
inside one caller candidate produce two sites and one caller candidate. Routine
totals union supporting events/sites across entries; they do not sum overlapping
membership counts.

Reanalyzing or reimporting identical evidence must not increase counts. A repeated
experiment is another execution only when evidence identity establishes that it
is distinct. Until cross-capture deduplication is defined, report captures
separately and do not claim unique cross-run occurrence totals. Static source-site
unions require compatible image bindings even when occurrence totals are withheld.

## Static possibilities versus observed execution

Keep statically decoded references in a separate layer with their own source and
decode assumptions. A candidate CALL in unexecuted bytes may be data or an
alternative decoding. Do not label it an observed caller. A pointer-table entry
is a data reference until transfer analysis establishes a relevant use.

Where correspondence is established, a static edge and an observed edge can link
to each other. Show static-only, observed, and unresolved cases explicitly. A
percentage such as observed/static callers is meaningful only for a declared
validated decoding domain; do not present it as whole-program coverage.

## Contribution to enriched assembly

The roll-up should select information that explains the code's relationships:

- A header can identify distinct observed call sites and named callers, retaining
  unknown caller attribution and the experiment/capture scope.
- An interior-entry comment can explain which path reaches it and what stack or
  register preparation distinguishes that path.
- A dispatch comment can connect the selector and pointer field to observed
  destinations, with links to the value findings that support the relationship.
- Shared-block comments can explain participating paths without assigning
  exclusive ownership or repeating an entire routine description.

Illustrative format, not measurements of the ROM:

```asm
; Selects the handler used by the current channel. [semantic claim reference]
; Observed: 6 taken CALL sites; 2 additional direct-jump sites.
; Counts cover the selected captures; further callers remain possible.
; Interior dispatch entry also reached by fall-through; see entry notes.
```

Purpose prose needs its own supporting semantic claim; the counts cannot generate
that first sentence by themselves. Detailed event lists belong in the associated
report/source map. Keep source comments readable, deterministically regenerated
after each stage, and independent of byte-preserving assembly verification.

Metrics also guide investigation: inspect a representative caller from each
distinct usage, investigate sites with multiple destinations, and design a new
experiment for an unseen outcome. Frequency or graph centrality alone is not a
measure of understanding, confidence or completeness.

## Delivery checklist and acceptance

- [x] Define capture-local site/destination keys and versioned metric definitions.
- [x] Derive typed transfer edges and reverse adjacency from existing occurrences;
      retain raw findings, unknown outcomes and targets absent from the extract.
- [x] Add incoming source-site sets, occurrence counts, conditional outcomes and
      outgoing destination counts with supporting evidence and explicit filters.
- [x] Expose stable headless JSON and a readable graph/report view; cap graph size
      explicitly and distinguish complete counts from truncated displays.
- [ ] Project concise metrics through the existing resolution/enrichment boundary
      into a selected assembly section, with navigable supporting evidence.
- [ ] Add block and routine graph projections as Stage 5 membership rules become
      available; preserve ambiguous ownership and entry-specific usages.
- [ ] Add static-reference and cross-experiment views only with their required
      decode, image and evidence-identity contracts established.
- [ ] Compare channel dispatch's CALL and fall-through paths, UNSTACK's distinct
      exits, and the calculator's dual-use RET in the optional ROM fixtures.
- [ ] Verify a reader can trace a feature's callers and explain its alternative
      entries from the generated source and report, then test a predicted case.

Mandatory synthetic tests should establish the counting examples above, recursion
versus jump cycles, conditional false/unknown outcomes, CALL targets equal to
fall-through, changed source bytes, multiple targets, missing target instructions,
overlapping memberships, a late interior entry and two uses of one RET. Adding a
new usage must expand the graph and roll-up without changing prior raw findings.
Repeated analysis must produce identical counts and deterministic serialization.
Partial capture and resource bounds must remain visible. Round-trip assembly
must still reproduce the selected bytes; this is a separate semantic acceptance.

The first implementation is intentionally capture-local and does not wait for
universal routine recovery. It can provide useful source-site metrics immediately
while routine names, grouping and semantic explanations remain revisable.

## First implementation checkpoint — 16 September 2026

Report v7 includes `transfer_graph` with tactic `z80-transfer-graph/1`.
`--format text` exposes a readable projection; JSON retains sample references,
per-category metrics, conditional outcomes, continuation-relationship variants
and incoming edge indexes. Counts are bounded by the existing capture limit.
The graph emits proposed destination comments without changing symbols or
exported assembly. Routine memberships and durable cross-capture identities
remain outside this increment.

Synthetic CLI tests cover 1,001 calls from two sites, changed bytes at one address,
conditional false/unknown/contradictory outcomes, a call whose encoded target equals
fall-through, multiple indirect destinations, jump cycles and a self-call without
invented routine grouping, RST, missing target instructions, incomplete capture,
machine transitions, duplicate IDs, over-limit input and deterministic text/JSON.
Later stage resolution preserves source metrics and architectural edges.

The sixteen optional ROM runs retain their established targets and continuation
findings. Graph assertions distinguish CALL versus fall-through arrival at `$162C`
and two RET successors at `$33A1` without counting either as an architectural call.
Two executions reproduce all 49 artifacts byte-for-byte. Debug/UI: 45 passed,
two optional ZEX skips (47 registered). Release/headless: 44 passed, the same two
skips (46 registered). Local ROM and Pasmo were supplied. No runtime or UI changes.
