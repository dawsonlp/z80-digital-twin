# Durable symbols and semantic assembly

**Last verified:** 2026-09-16. The model, CLI workflow and rendered ImGui editor
are tested. A native rendering smoke test passed; the interactive native
save/reopen walkthrough remains open because the automation tool stalled.

## Create and enrich a project

An analysis project describes the **original loaded binary**, not a live memory
snapshot. Its image identity includes SHA-256, length and origin. Loading different
bytes or using another origin does not rebind existing symbols.

```bash
build/z80_analyze new --image program.bin --org 0x8000 --project program.z80analysis
build/z80_analyze create --image program.bin --org 0x8000 --project program.z80analysis \
  --name ENTRY --address 0x8000 --kind FUNCTION
build/z80_analyze set --image program.bin --org 0x8000 --project program.z80analysis \
  --symbol ENTRY --field summary --value 'Initializes the display; purpose still under investigation.'
build/z80_analyze rename --image program.bin --org 0x8000 --project program.z80analysis \
  --symbol ENTRY --name INITIALIZE_DISPLAY
build/z80_analyze show --image program.bin --org 0x8000 --project program.z80analysis
```

Creation prints the symbol's stable ID. Later operations accept an ID, preferred
name or alias. Renaming retains the old name as an alias and historical metadata;
free-text notes are not rewritten. A name or alias cannot silently take another
symbol's identity. Retired records retain IDs and references, and reserve their
names. Use `alias --symbol NAME --name ALIAS --remove` to remove a lookup alias
while preserving it historically.

Use `--address` for a location: addresses within the input binary bind to an image
offset; other addresses are external memory locations. Use `--value` and optional
`--width 8|16|32` for a named constant. Equal numeric values do not establish
identity or turn immediate operands into address references.

Supported fields are `name`, `kind`, `extent`, `summary`, `inputs`, `outputs`,
`clobbers`, `questions`, `export_name` and `encoding`. Extent is an explicit byte
count or `unknown`; it is never inferred from the next symbol. All edits preserve
unaddressed fields. The current CLI changes one field at a time; the core and
editor can apply several fields in one transaction.

## Use the debugger

```bash
build/z80_debugger program.bin --org 0x8000 --analysis program.z80analysis
```

For ROM analysis use `--spectrum roms/spec48.rom --analysis rom.z80analysis`.
For a standalone RAM program launched with a Spectrum ROM, analysis binds to the
program binary and its origin; the ROM is machine context, not a second image in
that project.

Right-click an address to edit its symbol. The shared form edits name, kind,
extent and description. An extent of zero in the form means **unknown**. Existing
addresses are fixed, and removal retires the record. Multiple interpretations at
one address require selecting a stable ID using the CLI; the simple debugger
view does not arbitrarily choose one. Architectural vector defaults are a read
view and are not saved as invented program knowledge.

The File menu has a path field and **Open Analysis / Save Analysis** controls,
an unsaved-edits indicator, and an explicit legacy import action. Save existing
edits before opening another project. Closing with edits offers save, discard or
cancel. `--analysis` and `--sym` are alternative startup inputs.

## Import legacy symbols

```bash
build/z80_analyze import --image program.bin --org 0x8000 --project program.z80analysis \
  --source program.sym --attribution 'Assembler output for this binary'
```

This is a strict, transactional conversion. It retains source hash, entry content,
names, types, sizes, descriptions and attribution. Use `unknown` when attribution
is unknown. The legacy `program` field does not establish image identity. Invalid
entries or conflicts reject the import without partial mutation.

Reimporting the same source key and identical bytes is a no-op, even after user
refinements. A changed file at that key reports a conflict. The supplied source
path spelling is the source key; keep it stable across imports. The original file
is never rewritten. The debugger no longer saves its rich state back to `.sym`.

## Export readable assembly

```bash
build/z80_disassemble --org 0x8000 program.bin --analysis program.z80analysis \
  --output program.asm --map program.map.json --manifest program.manifest.json
build/tools/pasmo-0.5.5/pasmo --bin program.asm rebuilt.bin
cmp program.bin rebuilt.bin
```

Choose three new output paths; existing files are not overwritten. Validation
precedes publication, and the manifest is written last. It hashes the assembly
and map. If an I/O failure interrupts publication, an incomplete set can remain;
only a complete matching manifest identifies a completed export.

The assembly contains selected labels, external/interior-address equates, notes,
and structured branch/call/memory references. `RST` targets use early equates
because Pasmo needs those values in its first pass. Source-map lines link byte
ranges, declarations and structured uses to image locations and stable IDs.
Numeric immediates stay numeric. Raw bytes preserve exceptional, incomplete and
unsupported encodings; ordinary linear decoding is not proof that a byte is code.

Options:

- `--offset N --length N`: a byte range within the original image. The origin
  supplied on the command line still describes the complete input binary.
- `--aliases`: emit retained aliases as equates. Otherwise aliases remain in the
  project without becoming assembly definitions.
- `--select SYMBOL_ID`: repeat to select interpretations explicitly. By default
  all active records are selected; ambiguous addresses fail with a diagnostic.

Pasmo export uses a conservative identifier subset:
`[A-Za-z_][A-Za-z0-9_]*`, at most 255 characters, excluding Pasmo keywords and
case-folding collisions. Display names remain unrestricted by those assembler
rules. Set `export_name` when a display name needs a different emitted spelling.
Selected aliases must also satisfy the export rules. Named values emitted by this
Pasmo dialect are limited to 16 bits. The existing byte-only command is unchanged.

## Declare data and references explicitly

Set an image-bound symbol's `extent`, then set `encoding` to `bytes`, `words`,
`text` or `pointers`. Word/pointer encoding is little-endian. Printable ASCII
text uses `defm`; other bytes use numeric directives. Partial words and range
cuts fall back to byte directives. Conflicting selected regions are rejected.
These are deliberate interpretations with field provenance, not data discovery.

Explicit references name an instruction start or a pointer-word offset within
the image, plus a stable target and optional offset:

```bash
build/z80_analyze reference --image program.bin --org 0x8000 --project program.z80analysis \
  --offset 6 --relation memory --symbol DISPLAY_BUFFER --addend 2
```

Supported relations are `branch`, `memory` and `pointer`. Export checks the
selected target expression against the encoded bytes. An unsupported reference
site, retired target or mismatching operand fails export. Reference selection is
currently additive; changing an existing selected reference needs a future
explicit revision operation. Repeated edits to the target's name keep the ID.

## Save, recover and current limits

Project JSON is deterministic, versioned and revision-hashed. Keep it in Git for
chronological history. A successful changed save keeps the preceding valid file
as `.bak`; open that file explicitly to recover it, then save to a new path.
No-op saves do not rotate recovery or advance a revision.

Saves use a cooperating-writer lock, checked temporary writes and atomic
publication on the tested POSIX platform. New-file creation cannot overwrite an
appearing destination. Changed or missing on-disk files cause conflicts. Existing
file updates detect external edits before publication, but cannot provide a
filesystem compare-and-swap guarantee against non-cooperating writers racing
inside the final check/rename interval. A post-rename directory-sync error says
explicitly that the file was replaced and must be reopened to verify durability.

Automatic runtime capture, run identity, automatic discovery, cross-image
rebinding, banked memory and source round-trip editing are not implemented.
Evidence/proposal records and review operations exist in the core; that does not
mean runtime history has been made durable or replayable.
