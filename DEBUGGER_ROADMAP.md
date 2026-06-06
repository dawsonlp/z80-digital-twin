# Z80 Digital Twin — Reverse-Engineering Laboratory Roadmap

**Status:** Vision / future work (not yet scheduled)
**Relationship to current work:** extends the existing debugger (see
[DEBUGGER_DESIGN.md](DEBUGGER_DESIGN.md)). Nothing here blocks the near-term
usability work; this is the north star it's heading toward.

---

## 1. Vision

Turn the debugger from a tool you *run and explore* into a tool that
*accumulates knowledge* about a program and lets you *export that knowledge* as
a real, commented, reassemblable assembly source file.

The difference is durability. Today, what you learn about a binary lives in your
head and evaporates when you close the window. The goal is to make the
understanding itself the artifact: which bytes are code, which are data, what
each routine does, who calls what, what self-modifies — captured, annotated,
saved, shared, and ultimately re-emitted as source that assembles back to the
original bytes.

That makes this a **developer's laboratory** for new Z80 work and a **code
archaeologist's treasure trove** for old ROMs and games.

Two capabilities anchor the vision and recur throughout:

- **Ground truth from execution.** The CPU running the program *tells us* what
  is code. We don't have to guess.
- **Self-modifying code, called out loudly.** SMC is where static tools fail and
  where the interesting tricks live. We detect it exactly and surface it
  prominently.

---

## 2. Why this is achievable here (architecture already in place)

This isn't a rewrite — every layer hangs off something that already exists:

| Existing piece | What it enables |
|---|---|
| Debugger owns the step loop ([debug_session](debugger/exec/debug_session.h)) | Record every executed instruction start — the coverage map (L1). One line in `StepInstruction`. |
| `DebugMemory` write hook ([debug_memory.h](src/memory/debug_memory.h)) delivers exact (addr, old, new) | Cross-reference writes against the code map → SMC detection (L2), for free. |
| Disassembler exposes `branch_target` + `symbols_used` ([disassembler.h](debugger/disasm/disassembler.h)) | Build the call/jump graph and cross-references (L7) without re-parsing text. |
| `SymbolTable` with typed symbols + JSON I/O ([symbol_table.h](debugger/symbols/symbol_table.h)) | The seed of the annotation database (L3): grow it into comments, data typing, equates. |
| Pluggable memory + live re-read each frame | The disassembly always reflects *current* bytes — better than a static disassembler for SMC. |

The reverse-engineering layer is mostly **consuming and persisting** what the
engine already produces.

---

## 3. Guiding principles

1. **Execution is ground truth; everything else is annotation or guess —
   and we mark which is which.** A byte the CPU fetched as an opcode is known
   code. A byte we *think* is code is shaded differently from one we *saw* run.
2. **Knowledge is the durable asset.** The annotation database (labels,
   comments, data typing, code/data map) is the product. The binary is just its
   input.
3. **Never silently guess instruction boundaries.** (See the analysis that
   prompted this doc: back-decoding variable-length Z80 is undecidable in the
   presence of data and SMC.) Anchor disassembly to *known* starts — PC, branch
   targets, symbols, and forward-decode from them.
4. **Round-trip or it didn't happen.** The export is trustworthy only if
   reassembling it reproduces the original bytes. Verification is a first-class
   feature, not an afterthought.
5. **Iterative, not all-at-once.** Reverse engineering is run → observe → label
   → refine. The tooling should make each loop cheap.

---

## 4. Capability layers

Each layer builds on the previous. Rough effort and dependencies noted.

### L1 — Execution coverage map  *(foundation)*
**What:** Record, as the program runs, every address that was an instruction
start, plus the byte span each instruction covered. Classify every byte of the
64 KB space as one of: *untouched · executed-opcode · executed-operand ·
declared-data · declared-code · unknown*. Optionally accumulate a per-address
**execution count** (a heat map / profiler).

**Why:** This is the ground truth that makes everything else honest. It answers
"is this really code?" and "what have I not explored yet?".

**Fit/effort:** Hook `StepInstruction` to stamp the decoded instruction's span.
A `std::array<uint8_t, 65536>` of flags (or a small struct) — trivial memory.
Small.

**UX:** Shade the disassembly and memory views by classification; a coverage %
read-out; a 64 KB minimap/heatmap (executed vs untouched vs data).

### L2 — Self-modifying-code detection  *(called out loudly)*
**What:** When the `DebugMemory` hook reports a write to an address currently
classified as executed-code (or declared-code), raise an **SMC event**: record
{ written address, old byte, new byte, the PC of the instruction that wrote it,
cycle/timestamp }. Keep a timeline.

**Why:** This is the headline feature — the thing static disassemblers can't do
and the place classic Z80 cleverness hides.

**"Loudly":** dedicated SMC panel/log; red marker + flash on the modified
address in disassembly and memory; optional **break-on-SMC**; a running count in
the status bar; "instruction at X modified instruction at Y" with before/after
disassembly.

**The "before" value is free — no instruction trapping.** The `DebugMemory`
proxy reads the existing byte the instant before it overwrites it and passes it
to the hook as `old_value`. The pre-overwrite byte is therefore captured at the
natural trap point — the write itself — and nothing intercepts or snapshots
instructions ahead of time. Reconstructing the *before instruction* (not just
the byte) is a one-off disassembly at event/display time using the old byte,
never a per-write cost.

**Cost is negligible.** Only writes are hooked (never reads / opcode fetches),
so the bulk of memory traffic doesn't touch the hook at all. Per write: a couple
of array touches, an `if(hook)` branch, one `std::function` call, an O(1)
coverage-map check, and — only on a genuine code-write — a vector append. At full
Spectrum speed (~50–100K writes/s) that is well under 1% overhead; the debug
build runs comfortably faster than 3.5 MHz real-time (the FastMemory path
benchmarks ~2 GHz-equivalent; even a several-times-slower DebugMemory stays
~100× above the Spectrum). The session adds one store per instruction to
snapshot the writer PC.

**Fit/effort:** Small–medium. The hook already delivers `(addr, old, new)`; we
add the writer-PC snapshot and the cross-check against L1.

### L3 — Annotation knowledge base
**What:** Grow symbols into a full annotation set:
- **Comments** — per-line (trailing) and per-address block comments.
- **Data typing** — mark regions as `DB` bytes, `DW` words, `DM`/text strings,
  pointer tables, screen data, etc., so they render as data, not bogus code.
- **Code/data overrides** — manually assert code or data where execution hasn't
  reached (augmenting L1).
- **Equates** — named constants and port numbers (`EQU`).
- **Cross-references** — "who jumps/calls/reads/writes this address," derived
  from `branch_target`/`symbols_used` + L1.

**Why:** This *is* the reverse-engineering work. It's what you'd otherwise keep
in a notebook.

**Fit/effort:** Extends `SymbolTable` into an annotation store; xrefs derive from
data we already produce. Medium.

**UX:** Inline comments in the listing; a data-typing action (right-click a
region → "mark as DW table"); an xref panel ("references to RAMTOP: …").

### L4 — Knowledge-aware listing
**What:** A full listing view (beyond the scrolling window) that renders code as
instructions and data as directives, each with its labels, block/line comments,
and an xref header ("; called from 0x1234, 0x2A00"). Unknown regions are flagged
distinctly.

**Why:** The human-readable face of the knowledge base; also the thing you
export.

**Fit/effort:** Consumes L1+L3 through the disassembler. Medium.

### L5 — Export to reassemblable source
**What:** Emit a `.asm`/`.z80` file: `ORG` directives, `EQU`s, labels, `DB`/`DW`/
`DM` for data regions, instructions for code, and the user's comments — in a
chosen assembler dialect (default likely **sjasmplus**, already referenced in
[design_decisions.md](design_decisions.md); pasmo/z80asm as options).

**Why:** The payoff — exploration becomes a saved, editable, buildable source
artifact. Partial export (a range/region) supported.

**Fit/effort:** A serializer over L4's model. Medium.

### L6 — Round-trip verification
**What:** "Reassemble & verify": run the chosen assembler on the export, compare
the output bytes to the original, and **highlight every mismatch** — those are
exactly the addresses still misclassified (data read as code, undocumented
opcodes, unresolved SMC). Drives the next iteration of RE work.

**Why:** Makes the export *trustworthy* and turns correctness into a visible,
closeable loop.

**Fit/effort:** Shell out to the assembler, diff, map mismatches back to
addresses. Medium; depends on a chosen toolchain being present.

### L7 — Higher-level analysis
**What:** Build on the trace and the graph:
- **Call graph & basic blocks** (entry = CALL/JP target, end = RET/branch).
- **Function detection** → auto-propose `FUNCTION` symbols.
- **Auto-labels** for unnamed targets (`L_1234`) and data (`D_5C00`), user-renamable.
- **Data inference** — printable-run → string; sequence-of-valid-code-addresses →
  pointer table; screen-region detection.
- **Import existing knowledge** — seed from published disassemblies (e.g. Logan &
  O'Hara's ROM disassembly) as a starting annotation set.

**Why:** Accelerates the archaeology; turns raw coverage into structure.

**Fit/effort:** Larger, incremental; each item independently useful.

---

## 5. Data model (sketch)

```
ByteClass  : enum { Untouched, OpcodeStart, Operand, DeclaredCode, DeclaredData, ... }
CoverageMap: per-address ByteClass + execution count + first/last-seen cycle
DataRegion : { start, length, kind: Bytes|Words|Text|Table|Screen, element-stride }
Comment    : { address, kind: Line|Block, text }
Equate     : { name, value, kind: const|port }
Xref       : derived: address -> [referencing addresses, with ref kind: call/jump/read/write]
SmcEvent   : { write_addr, old, new, writer_pc, cycle }
Annotations: SymbolTable + comments + data regions + equates + (cached) xrefs + code/data map
```

The `CoverageMap` is cheap (a few bytes × 64 KB). `Annotations` is the saved
project state.

---

## 6. Project / annotation file format

Extend the current JSON `.sym` concept into a richer, human-readable,
diff-friendly **project file** (e.g. `program.z80proj`) that references the
binary and stores symbols + comments + data typing + equates + (optionally) the
captured coverage map and SMC log. Keep it JSON for consistency with today's
loader and so it's git-friendly and shareable — the accumulated RE work becomes
a portable, collaborative artifact.

(The current `.sym` files remain valid as the symbols-only subset.)

---

## 7. Export format & dialect

- Default to one real assembler dialect end-to-end (sjasmplus is the leading
  candidate) so round-trip verification is concrete; offer others later.
- Emit `ORG`, `EQU`, labels, `DB`/`DW`/`DM`, instructions, and comments.
- Support partial/region export.
- **Goal:** byte-exact round trip for everything classified.

---

## 8. Hard problems & honest caveats

- **Code/data boundaries are undecidable statically.** L1 (execution) sidesteps
  this for code that ran; everything else is annotation or marked-unknown. We
  never pretend otherwise.
- **Self-modifying code has no single "the" disassembly.** The listing is a
  snapshot of current bytes; L2 records *that* it changed and the before/after.
  Export of SMC regions needs explicit handling (emit the as-loaded bytes as
  data + document the modifier), and is a known round-trip caveat.
- **Undocumented opcodes** may not be supported by every assembler — affects
  round-trip; may need dialect-specific `DB` fallbacks.
- **Banking / >64 KB (Spectrum 128K paging)** isn't in the current flat
  address model. A real extension; the coverage map and annotations would need
  bank awareness. Note now, design later.
- **Annotation drift** if the binary is reloaded/changed — the project file
  should record a hash of the binary it describes.
- **Toolchain dependency** for L6 (an assembler must be installed); keep it
  optional and degrade gracefully.

---

## 9. Sequencing & dependencies

```
L1 coverage map ──▶ L2 SMC detection
      │                 │
      ▼                 ▼
L3 annotations ──▶ L4 listing ──▶ L5 export ──▶ L6 round-trip verify
      │
      ▼
L7 analysis (call graph, auto-labels, inference)  [draws on L1 + disassembler graph]
```

L1 is the keystone and the cheapest; it unlocks L2 (the marquee feature) and
feeds everything. L5+L6 are the "treasure trove" payoff. L7 is open-ended polish
that pays off continuously.

**Not yet.** This is deliberately parked behind the near-term usability work.
The intent is to record the destination so the small near-term choices stay
compatible with it.

---

## 10. Near-term seeds (cheap things that keep the door open)

- **Record executed instruction starts now** — the one-line hook in
  `StepInstruction` that begins the L1 coverage map, even before any UI uses it.
- **Capture the writing PC in the memory hook path** — so when L2 lands, "who
  modified this byte" is already available.
- **Keep the annotation model JSON and additive** — today's `.sym` is the first
  slice of the eventual project file; don't paint it into a corner.

These cost almost nothing today and make the laboratory a small step away rather
than a rewrite.

---

*End of roadmap.*
