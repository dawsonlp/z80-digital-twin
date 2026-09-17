# Status

**Audience:** users and developers checking current capability.
**Last reviewed:** 2026-09-16.
**Baseline:** implementation checkpoint `3ff0629` on
`feature/deterministic-execution-analysis`. The earlier symbol/runtime baseline
was merged through PR #1 at `2539862`; execution-analysis additions remain on the
feature branch. These are unreleased development capabilities.

## Implemented

- C++23 policy-based Z80 CPU, opcode/prefix stepping, bounded whole-instruction
  stepping, T-state accounting, maskable interrupts and focused correctness tests.
- `FastMemory`, `ObservableMemory` and optional `MetadataMemory`; open-bus,
  latched, callback and observable I/O policies.
- Shared Spectrum 48K runtime for viewer, debugger and probe: screen/border,
  keyboard, tape, beeper, floating bus, ROM protection and instruction/frame
  progress. Both cheaper and metadata-enabled memory configurations use it.
- Debugger stepping, breakpoints, write-watchpoints, disassembly, typed JSON
  symbols, coverage, SMC and refused-write reporting.
- Full address browsing, independent RO/X/SM/O markers and tooltips, per-address
  execution counts/latest captured bytes, byte activity summaries and a separate
  bounded recent-history view. Address evidence survives recent-history eviction.
- Architectural vector default labels, preserving existing labels.
- `Clear analysis` preserves machine state; the core also has `ResetCpu()`.
  The UI's **Restart program** still clears analysis and cold-boots Spectrum ROM.
- Independent Pasmo-oriented Python LSP and VS Code client. Terminal and editor
  tasks share `tools/spectrum_dev.py`: external Pasmo assembly, inspectable build
  artifacts, symbol conversion, launch validation and a fresh debugger process.
- `z80_disassemble` retains byte-only export and now supports image-bound analysis
  input, semantic definitions/references, data declarations, source maps and
  manifests. Independent Pasmo gates verify reconstructed bytes; this does not
  recover original authored source.

## Deterministic execution analysis

Headless supplied-capture analysis now classifies transfers, traces values and
continuations, recognizes bounded stack reconstruction, and resolves findings
repeatedly after each stage. Report v7 includes capture-local typed transfer
graphs, distinct caller-site/occurrence metrics and continuation variants, with
JSON and readable text output. BIT/comparison/logical coverage preserves unrelated
lineage while retaining explicit limitations.

Sixteen bounded CPU-only ROM-entry experiments preserve observed targets and
repeat deterministically. They do not establish authentic BASIC calling contexts
or universal routine contracts. See [usage](../users/transfer-analysis.md) and
[the current handoff](../../handoff.md) for scope and next work.

## Verification

Latest analysis checkpoint: Debug/UI 45 passed plus two optional ZEX skips;
Release/headless 44 passed plus the same skips, with local ROM and Pasmo supplied.
All sixteen ROM experiments replayed identically across 49 artifacts. This is
recorded implementation verification, not a fresh run during documentation edits.

The [15 September verification record](../testers/documentation-refresh-verification.md)
separates fresh build/test results, optional skips and GUI limitations. Tests
establish their covered cases, not exhaustive Z80 or Spectrum compatibility.
Older workflow and native-interaction evidence remains dated in the linked
checklists; it is not a fresh acceptance result for every current change.

## Remaining limitations

- Runtime observation history is still transient. Durable project records do not
  yet automatically capture execution evidence, full machine state or replayable
  runs; runtime identity is the next architectural decision.
- Address metadata is implemented but full acceptance remains open: complete
  access-accounting/overflow audit, resource/overhead measurement and remaining
  native interaction checks. First/latest byte activity exists; first-completion
  summaries per instruction start are not yet a complete delivered contract.
- Replacement-load lifecycle is not uniform: generic binary/GCD loaders reset
  the CPU without clearing the session analysis. Use a fresh process when
  switching analyzed programs; full lifecycle acceptance remains open.
- No contention, complete HALT bus/refresh progression or persistent INT-line
  model. Some TZX flow-control blocks remain linearized.
- No live reload into an existing debugger, automatic durable runtime capture,
  integrated execution-evidence-aware source export, routine-level call graph,
  cross-capture occurrence aggregation or complete mutation archive. Image-bound
  annotations already persist; selected supplied captures can be saved/reanalyzed.
- External CPU runner has built-in ZEXDOC/ZEXALL cases; the manifest is descriptive,
  not parsed. Broad game compatibility and full-program audio regression remain open.
- macOS is the exercised development platform. Windows/MSVC and installed-header
  SDK use are not verified; the current install layout needs correction.

See [roadmap](../developers/roadmap.md) for sequencing and
[testing](../testers/testing.md) for reproducible commands.

## Durable symbol analysis (2026-09-16)

Stable IDs, aliases, field enrichment, image-bound save/reopen, strict legacy
import and semantic Pasmo export are implemented in the merged baseline.
The shared debugger editor and `z80_analyze` use one project authority. Assembly,
source maps and manifests support selected code/data interpretations and preserve
bytes across the synthetic gates and the supplied Spectrum ROM.

See [commands and limits](../users/symbol-analysis.md) and
[implementation evidence](../developers/analysis-implementation.md). Native
rendering is verified, while interactive native save/reopen acceptance remains
open after an automation stall. Automatic runtime evidence capture is awaiting
an architectural decision about run and capture-epoch identity.
