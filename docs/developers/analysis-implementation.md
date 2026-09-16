# Analysis implementation and next architectural decision

**Status:** working-tree implementation verified 2026-09-16. Stages 1–3 have
headless acceptance evidence; Stage 1's interactive native walkthrough remains
open. Stage 4 runtime integration awaits the decision below.

## Current boundaries

`debugger/analysis/` is part of `z80_debugger_core`, not a new library or service.

- `analysis_project.*`: typed IDs, image/external/value bindings, field claims,
  aliases, retirement, references and proposal decisions. Operations stage and
  validate changes; ordinary failures use `std::expected`.
- `analysis_storage.*`: strict schema, deterministic serialization, legacy import,
  revision hashing, save tokens, locks, recovery and atomic publication.
- `analysis_workspace.*`: active project, original image bytes, file token and
  derived read view. Edits stage the project **and** read view before publishing.
  Allocation-failure tests verify that a failure during either stage leaves both
  unchanged. The UI holds a read-only view and routes all mutations here.
- `analysis_export.*`: declaration selection, validated identifiers, semantic
  source, map and manifest. The existing decoder supplies structured direct
  address operands. It does not find/replace hexadecimal text to discover links.
- `analysis_json.*`, `content_hash.*`: bounded dependency-free JSON and SHA-256.
  No new network fetch, package dependency or UI dependency is required. JSON
  integers are sufficient for this schema; floats are rejected. Duplicate keys,
  invalid UTF-8/surrogates, excess nesting and unknown fields are rejected.

Legacy `SymbolTable` remains available for compatibility and derived lookup;
its extent representation now holds 65,536 bytes without narrowing. The richer
view detects ambiguous overlapping regions. The CPU and register union are
unchanged. The architectural vector labels remain presentation defaults unless
explicitly imported/created as project records.

## Version 1 format decisions

The outer JSON contains `format: z80-analysis`, integer `version: 1`, `revision`
and `project`. Revision is SHA-256 of the canonical pretty-printed project object,
including its final newline. JSON object keys are sorted; record arrays are
ordered by their typed IDs. No wall-clock timestamps enter reproducible content.

IDs contain a type prefix and 128 random bits as 32 lowercase hex digits:
`prj_`, `sym_`, `src_`, `ev_`, `ref_`, `prop_`. They are opaque, not addresses and
not claimed to be RFC UUID encodings. The image is identified by SHA-256, byte
length and load origin. Version 1 supports one immutable image in `z80:flat16`.
Files are limited to 16 MiB; JSON nesting is limited to 64 levels.

The project stores symbols, sources, evidence, references and proposals. Field
claims carry origin, review state, optional source and evidence IDs. Current
fields and proposal alternatives survive serialization. Subsequent deliberate
edits supersede an accepted proposal while retaining its content. Imported entry
content is retained as evidence, together with source hash and entry ordinal.

Retirement hides a record without deleting its ID or relationships. Retired
names remain reserved. Alias removal retains historical metadata. Multiple
symbols at an address are allowed; the simple display omits an ambiguous label,
and export requires an unambiguous selection. Unknown extent is null, never an
inferred span. Numeric constants and external addresses are separate binding
variants.

The disk token hashes the exact previously read file. New destinations use an
exclusive hard-link publication; updates use sibling rename after write/fsync
and creation of a previous-file `.bak`. An advisory `.lock` sidecar serializes
cooperating writers; it may remain after exit without holding a lock. Crash
recovery is explicit through the backup. Directory-sync failures after rename
are reported as post-publication failures. Native Windows persistence has not
been implemented or validated; current storage code targets POSIX APIs.

## Validation evidence

- `analysis_project_test`: known SHA-256 vectors; strict JSON; stable IDs/aliases;
  metadata preservation; duplicate/dangling/bounds/schema rejection; image
  mismatch; idempotent/conflicting import; proposal retention/supersession;
  deterministic serialization; recovery, disk conflicts and writer locks;
  full-64-KB region view and architectural-default alias collisions.
- `symbol_allocation_test`: allocation faults in legacy indexes and in staged
  project/view/revision publication, including recovery after failure.
- `symbol_edit_test`: the production ImGui editor driven by input events, including
  enrichment-preserving rename, aliases, collisions/retry, original-address
  removal and occupied-address creation rejection. It uses no window backend.
- `analysis_workflow`: fresh CLI processes create/enrich/rename/reopen/export;
  text/bytes/words/pointers, explicit references with offsets, external locations,
  numeric immediates, interior labels, partial ranges, case/reserved-name
  rejection, maps/manifests and deterministic output. Real Pasmo reconstructs
  every byte. The semantic renderer also runs all existing opcode-family,
  exceptional-prefix, wrap, truncation and deterministic-random byte fixtures.
- The local 16,384-byte Spectrum ROM with SHA-256
  `d55daa439b673b0e3f5897f99ac37ecb45f974d1862b4dadb85dec34af99cb42`
  was imported with architectural vector labels, reopened, exported and assembled
  byte-for-byte. This found and repaired Pasmo's first-pass requirement for RST
  operands. Artifacts and tool/content hashes are local under
  `build/rom-export/semantic-20260916/`; no ROM/source bytes are checked in.
- A time-bounded native smoke run reopened that ROM analysis, executed two frames
  and one instruction, and rendered five UI frames successfully. Native automation
  selected the app but stalled despite a requested timeout, so interactive
  rename/save/reopen is **not** claimed as accepted.

See [user commands and limits](../users/symbol-analysis.md). Test counts and build
configurations are recorded in the development plan after the final checks.

## Architectural input needed: runtime evidence identity

**Observation:** `InstructionHistory` identifies events by an in-memory generation/
sequence counter. That counter is not a durable execution identity. The application
can clear observations, reset CPU state, cold-boot the Spectrum, or replace a
session. Those operations have different effects on machine state. The new project
records must not infer that an event sequence identifies a unique persistent run.

**Proposed decision, not implemented:**

1. Give each execution run a stable opaque ID, separate from project/image IDs.
2. Start a new run when execution state is reset/restarted or an image is loaded.
   Record the actual trigger and known initial context. A CPU-only reset must not
   be described as a clean machine boot or a complete initial-state snapshot.
3. Pause/resume remains the same run. Clearing transient analysis begins a new
   capture epoch within that run; it does not imply that the machine restarted.
4. Persist only deliberately selected observation extracts: run/epoch, local
   sequence, instruction location, captured bytes, completion status, relevant
   counters and explicit capture/fidelity limits. Copy supporting content before
   transient records can be replaced or evicted.
5. Determine applicability to the immutable project image by byte comparison.
   Divergent runtime bytes retain their original observation and explanation,
   with an explicit mismatch; they do not rebind the project's symbols.

This choice defines evidence continuity across runtime lifecycle operations. It
requires owner input before installing capture hooks or modifying reset/load
ownership. It does not introduce replay, resumable snapshots, banked identity,
or changes to CPU stepping/timing. The current evidence/proposal schema is
infrastructure; automatic runtime attribution remains off until this is settled.
