# Symbol analysis and assembly projection: development plan

**Audience:** developers implementing persistent program understanding and export.
**Date:** 15 September 2026.
**Status:** Stages 0–3 implemented and tested locally, with Stage 1's native
interactive walkthrough still open. Stage 4 has model/persistence groundwork;
automatic evidence capture awaits [runtime identity input](analysis-implementation.md#architectural-input-needed-runtime-evidence-identity).
Milestones are accepted only against the evidence recorded below.
**Inspected baseline:** `a5e3c3d`, including source commit `b94fa00`.

## 1. Outcome and first complete user loop

Maintain an evolving, durable account of a program's symbols and meaning. Let a
user rename and enrich that account without losing identity, references or
evidence, then project a selected revision into byte-preserving assembly.

The first complete acceptance loop is:

1. Open an exact binary image and create an analysis project.
2. Identify one routine and the variables it references; assign names and notes.
3. Save, close and reopen the project with identity and information intact.
4. Rename the routine and enrich its description while retaining its former name.
5. Export assembly whose definitions, references and comments reflect that revision.
6. Assemble with Pasmo 0.5.5 and compare every byte with the selected input range.

Run this first on a redistributable synthetic fixture. Repeat with the local
Spectrum ROM as an optional integration demonstration. ROM bytes and reconstructed
ROM source remain local. Byte equivalence and correctness of interpretation are
separate acceptance claims.

## 2. Observed baseline and concrete gaps

This table records the inspected baseline before Stage 0. The edit/index defects
are repaired by the Stage 0 work below; persistence and identity gaps remain.

| Existing surface | Observed behavior | Implication |
|---|---|---|
| [SymbolTable](../../debugger/symbols/symbol_table.h) | Address/name indexes; type, description and size; JSON `.sym` persistence. | Useful compatibility surface, but no stable identity, aliases, provenance or image binding. |
| [Define and persistence](../../debugger/symbols/symbol_table.cpp) | Define replaces an address entry; load merges entries; save truncates the destination directly. Name uniqueness is not transactionally enforced across both indexes. | Rename/import collisions and failed saves need explicit, tested outcomes. |
| [Symbol editor](../../debugger/ui/symbol_edit.cpp) | Reconstructs a symbol from address, name and a subset of types. It does not preserve description and derives size from that limited type selection. | An ordinary edit can lose description, region extent or unsupported type information. |
| [Disassembler](../../debugger/disasm/disassembler.h) | Optional address-to-name resolver, rendered operands and direct branch target. | Reusable rendering support; insufficient structured operand relationships for every semantic substitution. |
| [Pasmo exporter](../../debugger/disasm/pasmo_source.cpp) | Receives bytes and origin; linear decode with raw-byte fallbacks. | No project revision, symbolic definitions, data declarations or analysis source map. |
| [Build workflow](../../tools/spectrum_dev.py) | Converts Pasmo symbols to debugger JSON; retains build/input/artifact hashes. | Build facts can be imported with provenance; they must not become a second mutable analysis authority. |
| [Instruction evidence](../../debugger/exec/instruction_history.h) | Per-address latest observations/counts and bounded recent events. | Available enrichment input, but not durable evidence; sequence numbers alone cannot be persistent citations. |

The existing [Spectrum symbol seed](../../examples/spectrum48k.sym) is an import
candidate. Its historical source and applicability to the selected ROM are not
established by its filename. Import it with that uncertainty intact.

## 3. Scope and boundaries

The first target is one fixed, non-banked 16-bit address space with an immutable
binary image, an explicit origin and external address/constant declarations.
The Spectrum 48K ROM is the first real subject, not a dependency of the generic
symbol model. Unknown extents and overlapping interpretations must be representable.

In scope: stable symbol identity; lossless editing; aliases; contextual bindings;
field-level interpretation/provenance; save/reopen; legacy import; deterministic
assembly projection; source mapping; focused evidence attachment.

Deferred: automatic matching across changed binaries, banked memory, full machine
snapshots, replay, complete mutation archives, general call-graph discovery,
automatic source-edit ingestion, collaborative editing and additional dialects.
Runtime evidence persistence here means selected durable evidence extracts, not
an unbounded event store.

Keep the CPU and memory hot paths unchanged for the initial increments. The
independent LSP remains usable without a project or running emulator. Use the
same headless operations from CLI and UI; no service or database is needed.

## 4. Recommended model and invariants

### 4.1 Identity, binding and naming

Give every project and symbol an opaque stable ID generated once and persisted.
Neither the preferred name nor the address is the ID. Renaming retains identity;
creating another symbol at a reused address does not inherit it automatically.

| Record | Minimum responsibility |
|---|---|
| Project | Schema version, project ID, target/address-space description, image identities and current semantic revision. |
| Image | SHA-256, byte length and origin; path is a replaceable locator, not identity. |
| Symbol | Stable ID, binding, preferred name, aliases, kind, optional extent, descriptive fields and their provenance. |
| Reference | Source image/location and operand or data-field identity; target symbol ID plus optional offset; relation kind and basis. |
| Evidence | Stable evidence ID, source kind, image/run context and retained supporting content or a content-hashed artifact. |

Bindings have explicit variants:

- **Image location:** image ID plus byte offset; mapped address derives from origin.
- **External memory location:** address-space ID plus address. A RAM variable named
  by a ROM analysis does not claim that its current contents are part of the ROM.
- **Named value:** value plus an explicit kind/width where needed. Equal values do
  not establish equivalence to each other or to a memory address.

Within this increment an image binding is immutable. A different image hash can
be inspected, but cannot silently accept the existing bindings. Explicit rebinding
and cross-build matching remain future work. Renaming does not move a symbol.

Allow multiple interpretations/names at one address. Use aliases for different
names of the same entity; use distinct IDs where the entities or interpretations
differ. The export view must select an unambiguous preferred definition. Do not
infer routine length from the next symbol, or classify unseen bytes as data.

### 4.2 Enrichment and revision

Support preferred name, kind, optional extent, summary and optional routine notes
for inputs, outputs, clobbers and unresolved questions. Do not require invented
values to fill a schema. Unknown information stays absent.

Track provenance and review state on the specific field or assertion concerned:
an observed entry address does not make a proposed routine purpose an observed
fact. Keep origin and review separate: imported/user/static/runtime/generated is
the origin; proposed/accepted/rejected/superseded describes treatment of a claim.
Acceptance means selected for use, not proof of truth. Ordinary deliberate user
edits can become current immediately without a separate approval ceremony.

Start with current field values, their source/evidence references, and a small
collection of unresolved or rejected alternatives. Retain superseded names and
needed source references. Use deterministic JSON and Git for saved revision
history; do not build a general event-sourcing system. Without Git, the current
file and recovery copy do not promise a complete chronological edit history.

An edit must preserve fields it did not address. Structured relationships use
IDs and survive renames. Free-text descriptions are not rewritten by global
string replacement; they may mention historical names intentionally.

### 4.3 Names and conflicts

Preferred display names may differ from assembler spellings. Store an explicit
Pasmo export spelling when required; preserve an imported name such as
`START/NEW` even if its emitted identifier is `START_NEW`.

Validate emitted identifiers, reserved names and collisions according to the
selected assembler's tested rules, including case handling. Validate before
mutation or export. Never silently redirect another symbol's name index. An old
preferred name normally becomes an alias; conflicting alias retention needs an
explicit resolution, with the prior name still preserved as historical metadata.

Queries that can match several records return ambiguity instead of choosing an
arbitrary result. The existing simple resolver may expose only the selected,
unambiguous name for a given address.

### 4.4 One analysis authority

Place the model and operations in the UI-free debugger capability, initially
beside `debugger/symbols/`. A proposed `AnalysisProject` owns durable records.
Keep `SymbolTable` as a compatibility/read-view adapter as needed; do not maintain
two independently editable stores. Migrate write call sites through the project
operations before enabling durable project editing in the UI.

Separate the model, JSON storage/import, evidence adapters and assembly projection.
Do not introduce a new library hierarchy unless dependencies require it. The
machine-specific import adapter supplies Spectrum conventions; the model does
not hard-code Spectrum variables or ROM routine names.

## 5. Editing, import and persistence contracts

Provide shared operations for create, rename, add/remove alias, update individual
fields, add evidence/proposal, resolve a proposal, and retire a symbol. Validate
the full operation before changing indexes or records. Retiring a referenced
symbol preserves its ID and unresolved relationships; never retarget them silently.

Use a distinct versioned analysis JSON format so legacy `.sym` readers cannot
silently discard richer fields. Load into temporary state and validate IDs,
references, bounds, schema and image identity before replacing active state.
Reject unsupported schema versions without rewriting them.

Save to a temporary sibling file, check the complete write, then replace the
destination atomically using a tested filesystem operation. Preserve the last
valid file on failure. Detect a changed on-disk revision before overwriting it;
initially report a conflict rather than attempting an automatic merge. No-op
save/export must not change a revision merely because the clock advanced.

Legacy `.sym` import is a separate, explicit conversion:

- Preserve names, descriptions, types and sizes; report invalid entries and conflicts.
- Require a chosen image/address context; do not infer it from a `program` string.
- Record file hash, entry identity and source attribution; unknown provenance stays unknown.
- Allocate stable IDs once. Reimporting the same source is idempotent; changed input
  creates a reviewed update or conflict rather than overwriting user refinements.
- Preserve the original input. Do not reuse the existing permissive merge loader
  as the authoritative rich-project parser.

Build symbols arrive with build/image identity where available. Matching a name
or address in a later build is not sufficient evidence of entity continuity.
If legacy `.sym` export is offered, label it as lossy and report omitted fields;
the analysis file remains authoritative.

## 6. Assembly projection contract

Inputs: exact image bytes, validated project revision, selected non-wrapping
range, dialect and deterministic export options. Existing byte-only export remains
available and retains its current behavior when no analysis is supplied.

The existing Pasmo domain remains 1–65,535 bytes with origin plus size at most
65,536. Do not widen it as a side effect of semantic export.

Produce three associated artifacts:

1. Assembly: definitions, equates, selected aliases, comments and supported data directives.
2. Source map: emitted file/line and statement byte range to image location and
   symbol/reference IDs. Distinguish comments/definitions with no emitted bytes.
3. Export manifest: image hash, project revision, range, exporter/dialect/options,
   output hashes and unresolved interpretations. A subsequent verification report
   adds actual assembler identity and comparison results.

Reference substitution must operate on structured operand/data relationships,
never textual search-and-replace of hexadecimal strings. Initially support direct
branch/call targets and absolute memory operands. Leave ambiguous immediate
values numeric unless explicitly classified. Referenced external RAM locations
and labels outside the selected range use equates without emitting extra bytes.

Declared data may use byte/word/string forms only when their encoding is explicit
and byte-preserving. Unknown content retains numeric/raw forms or clearly marked
tentative decoding. A candidate data region does not become established merely
because it resembles text or was not executed in a sample run.

Overlapping instruction starts and labels inside another instruction cannot all
be independent emitted instruction definitions. Select one byte layout, retain
other names as address equates/comments and use raw-byte fallback when necessary.
Reject conflicting selected declarations with a useful diagnostic; never duplicate,
drop or move bytes to make the listing look plausible. An export label is not
proof of a valid instruction boundary.

Same bytes, semantic revision and options produce identical assembly and maps.
Avoid wall-clock timestamps and machine-specific absolute paths in reproducible
content. Rename changes definitions and structured uses; it does not change bytes.
Reassembly validates length, origin/range and every byte, not just process exit.

## 7. Source ownership

During reverse engineering, names and annotations are authored in the analysis
project and assembly is regenerated. Keep generated output in a designated
location; detect modified output before replacing it, or write a new export.
Do not silently erase manual work or promise bidirectional synchronization.

An explicit handoff may copy an export into an authored assembly project. There,
source owns future program changes and assembler symbols describe the resulting
build. The original ROM analysis remains attached to its original image. Automated
annotation transfer across builds and ingestion of source comments are deferred.

## 8. Incremental delivery

Each stage should be independently reviewable and end with executable evidence.
Unchecked gates below remain open until their full scenario passes.

### Stage 0 — make existing symbol editing safe

Change `SymbolTable` mutation/index handling and the shared edit form. Preserve
description, size and unsupported types on rename; separate rename from address
changes and detect conflicting names before updating either index.

- [x] Editing a named data region preserves description, size and type.
- [x] Rename updates lookups consistently; a collision leaves both symbols unchanged.
- [x] Tests cover changed-address behavior explicitly rather than treating it as rename.
- [x] Existing symbol/disassembly tests pass and the real edit form is exercised.

Implementation evidence (2026-09-15):

- `Define` rejects empty/conflicting names and preserves both indexes on failure;
  `Rename` preserves all other fields. Legacy import skips conflicting names with
  warnings. Same-address `Define` remains an explicit replacement operation.
- Existing-symbol addresses are read-only in the form. Creation at a new address
  remains available; creation over an occupied address is rejected. Relocation
  is deliberately unsupported at this stage. Removal targets the original address.
- The actual shared ImGui form is exercised by `symbol_edit_test` with input
  events and no native window backend. It verifies metadata/long-name preservation,
  fixed-address edits, collision feedback and recovery, creation and removal.
- `symbol_allocation_test` injects allocation failures into insertion/replacement
  and checks rollback and subsequent usability. Symbol/disassembly regressions pass.
- Debug UI build: 36 CTest cases passed, two optional ZEX cases skipped because
  their assets are unavailable. Spectrum ROM integration cases ran with the local
  ROM. Release headless symbol, allocation and disassembler tests also passed.
  This is not an interactive native-window acceptance claim.
- [C++ conventions](cpp-standards.md) record the agreed register-union exception.
  This increment changes neither register representation nor CPU execution.

This is an immediate correctness repair, not yet the durable symbol lifecycle.

### Stage 1 — deliver stable identity and save/reopen

Implement the minimal project model, stable IDs, aliases, field updates,
image/external/value bindings, strict persistence and legacy import. Route editing
through one authority and expose save/open plus image-mismatch diagnostics.
Share validation and operations between headless callers and UI.

- [x] Create, enrich, rename, save and reopen without losing ID or unrelated fields.
- [x] Old-name lookup resolves the same identity where unambiguous.
- [x] Duplicate IDs, name conflicts, dangling references and invalid extents are rejected.
- [x] Wrong-image open preserves existing work and does not apply annotations.
- [x] Invalid/truncated/newer-schema files and failed saves preserve the last valid state.
- [x] Same-source reimport is idempotent; changed imports cannot overwrite user edits silently.
- [x] No-op serialization is stable; on-disk edit conflicts are detected.
- [ ] A native save/reopen/rename walkthrough agrees with headless behavior.

Implemented in the UI-free analysis capability with shared CLI/UI operations.
See [implementation decisions and verification](analysis-implementation.md).
The actual ImGui form and a native rendering smoke passed. Interactive native
save/reopen remains unverified after a tool stall; it is not silently accepted.

At this point users can retain useful knowledge even before semantic export exists.

### Stage 2 — deliver the first complete symbolic export

Extend the exporter with optional project input and structured references for
direct branch/call and absolute memory operands. Add labels, external equates,
selected aliases and descriptions; emit the map and manifest. Keep the byte-only
mode and the existing external assembler boundary.

- [x] The complete user loop in section 1 passes on a mixed routine/variable fixture.
- [x] Renaming updates all supported structured references without altering bytes.
- [x] An equal-valued numeric constant stays numeric when it is not an address reference.
- [x] External RAM names and out-of-range branch targets produce valid equates.
- [x] Invalid/case-colliding assembler names, reserved names and ambiguous definitions fail clearly.
- [x] Repeated exports are identical; maps identify the correct original byte ranges and IDs.
- [x] Existing exceptional-encoding and byte-only round-trip gates still pass.
- [x] A local ROM sample is saved/reopened/exported and byte-compared when its ROM is supplied.

Verified with the fresh-process synthetic workflow, all existing byte-preservation
fixtures through the semantic renderer, and the complete local Spectrum ROM.
The ROM's RST first-pass constraint is covered by a dedicated regression.

This is the first complete release-sized increment. It does not require automated
routine discovery, a call graph or full runtime-evidence persistence.

### Stage 3 — represent richer declarations and partial exports

Add explicitly selected data regions, pointer entries, symbol-plus-offset
references and routine notes. Extend the projection with tested directives and
the overlap/interior-label policy. Preserve tentative and conflicting interpretations.

- [x] Mixed code, text, words, pointers and unknown bytes reassemble exactly.
- [x] Interior labels, overlapping starts, unknown extents and range cuts lose no bytes.
- [x] Conflicting selected regions fail before output publication.
- [x] Data/pointer interpretations carry provenance and can be revised independently of names.

### Stage 4 — attach evidence and proposals incrementally

Current groundwork: stable evidence/proposal records, field provenance, persisted
review outcomes and supersession are tested. Runtime adapters are not installed.
The [runtime identity proposal](analysis-implementation.md#architectural-input-needed-runtime-evidence-identity)
is the next owner decision, because reset, restart and observation clearing do
not denote the same execution boundary.

Add explicit adapters for static findings, imported descriptions and selected
runtime observations. Retain supporting content before a transient history record
can be evicted. Identify image, run, relevant bytes and observation limitations.
Introduce automated/LLM proposals only through the same field-level operations.

- [ ] Evidence remains inspectable after debugger exit and history eviction.
- [ ] A name can change without breaking evidence/reference links.
- [ ] Accepted, rejected and conflicting proposals survive save/reopen.
- [ ] Rerunning an analyzer does not erase deliberate interpretations or repeat dismissed proposals blindly.
- [ ] Changed bytes invalidate applicability without deleting the older observation or explanation.

Complete broad metadata acceptance separately. Selected reliable evidence can be
attached without claiming the entire runtime is a cycle-exact or complete oracle.

## 9. Code placement and validation strategy

Final local validation (2026-09-16): Debug/UI configuration registered 40 CTest
cases (38 passed, two optional ZEX asset cases skipped); Release/headless
registered 39 (37 passed, the same two skips). Pasmo and the local Spectrum ROM
were supplied in both runs. No compiler warnings were reported for the changed
code. Native rendering passed; interactive native save/reopen remains open.


| Area | Likely changes |
|---|---|
| `debugger/analysis/`, `debugger/symbols/` | Implemented project model, workspace, strict storage/import/export and legacy read-view support, all in the existing core target. |
| `debugger/ui/symbol_edit.*`, `debugger_app.*`, `ui_context.h` | Lossless form editing, project ownership, open/save, conflicts and dirty state. |
| `debugger/disasm/` | Structured references, selected-symbol resolver and semantic source projection. |
| `tools/disassemble/` | Optional analysis input and map/manifest output; existing byte-only interface preserved. |
| `tools/spectrum_dev.py` | Later provenance-aware build-symbol import; preserve current fresh-launch behavior. |
| `tests/` and `CMakeLists.txt` | Mutation/persistence fixtures, semantic export gates and failure-path coverage. |

Class/file names beyond current files are provisional. Finalize schema and command
spelling with Stage 1 fixtures; document new CLI options only after implementation.
Prefer existing dependencies; any new JSON dependency needs an explicit build/offline
impact assessment. Do not duplicate an incompatible serializer in the UI.

Use meaningful boundary tests: invariants after mutation, restart persistence,
failed-write recovery, real Pasmo output comparison and actual UI use. Synthetic
fixtures are mandatory in normal testing; missing optional ROM assets are reported
as skipped. Record commit, inputs, tools, passes, skips and native limitations.
Do not convert historical GUI results or schema examples into acceptance evidence.

## 10. Risks, decision points and stopping conditions

| Risk | Required response |
|---|---|
| A label becomes mistaken for semantic truth | Keep field provenance/review and supporting bytes visible. |
| Rename/import loses knowledge | Patch fields, validate transactions and test repeat imports/collisions. |
| Wrong image receives plausible names | Check content identity before activating image-bound analysis. |
| Generated and editable copies diverge | Keep one analysis authority and explicit source handoff. |
| Alias or dialect normalization changes meaning | Preserve original names and validate emitted spelling with Pasmo fixtures. |
| Persistent citations point into an evicted queue | Persist selected evidence content or mark it unavailable; never fabricate it. |
| Model grows into an ontology before one routine works | Stop expansion until Stage 2's complete fixture passes. |

Before Stage 1 implementation, settle in its schema/tests: ID encoding, assembler
name rules, revision hashing, proposal representation and atomic-save/recovery
behavior on the supported platform. These are bounded implementation decisions
within this plan. Expanding to banked identity, live rebinding, source round-trip
editing or a new service is a separate scope decision.

Existing [address metadata](address-metadata-checklist.md) acceptance stays open.
Stages 0–2 can proceed against immutable images without waiting for every runtime
audit; Stage 4 must state which observed evidence is actually supported. This
plan implements the symbol/persistence/export portion of the
[ROM-first loop](rom-reverse-loop-checklist.md) and refines the relevant
requirements in the [enhanced roadmap](enhanced-roadmap.md); it does not declare
those larger milestones complete.
