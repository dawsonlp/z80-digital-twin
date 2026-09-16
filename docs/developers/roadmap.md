# Roadmap

**Audience:** developers choosing implementation work.
**Last reviewed:** 2026-09-15.
**Purpose:** current delivery sequence. [Status](../reference/status.md) describes
implemented capability; the longer proposals preserve requirements and intent.

## Delivered foundations

- Independent LSP/VS Code integration and byte-preserving Pasmo disassembly gates.
- Source-to-Spectrum build/run using an external assembler, generated symbols,
  launch validation and a fresh debugger process. This does not deliver live reload.
- CPU-owned bounded whole-instruction stepping and a shared Spectrum runtime
  across viewer, debugger and probe, including retained partial-frame progress.
- Observed address browsing, execution-time bytes, bounded recent history and
  independently retained latest address evidence.
- Byte activity, sticky execution/self-modification properties, compact status
  markers, vector labels and clear-analysis control. Acceptance is still partial.

## Now: close the address-analysis increment

Use [address metadata](address-metadata-checklist.md) as the acceptance checklist.
Finish the access-accounting and overflow audit, first/latest completion contract,
resource measurements and remaining GUI checks. Preserve the distinction between
implemented APIs, tested cases and unverified broader requirements. Keep existing
restart semantics explicit; a CPU-only reset API does not imply a UI command.

The [July source review](source-architecture-feedback.md) remains historical
rationale. Shared runtime and stepping consolidation have landed; its remaining
HALT/interrupt, packaging, performance and correctness concerns still need
individual resolution rather than a blanket “consolidation complete” claim.

## Next: close a durable ROM reverse-engineering loop

Follow the [ROM-first checklist](rom-reverse-loop-checklist.md): bind evidence
and annotations to an exact image and build identity, retain provenance, save
and reopen one understood routine, export a selected range, and reassemble to
compare bytes. Reopening evidence is distinct from resuming a machine or replaying
execution. Existing JSON symbols and raw-binary export are foundations, not this
complete user loop.

Do not infer code/data classification from absence of execution. Separate observed
bytes, user interpretations, imported names and generated suggestions.

## Parallel maintenance priorities

- Correct and test HALT progression and interrupt signaling without conflating
  shared scheduling with hardware fidelity; contention follows with deterministic
  tests before raster-sensitive compatibility claims.
- Maintain executor/decoder consistency and independent encoding checks; preserve
  explicit limits on undocumented opcode behavior.
- Repair installed headers and validate intended compiler/platform support.
  Benchmark bare CPU and metadata-enabled execution separately.
- Extend external CPU-suite adapters when assets are available; implement manifest
  parsing before treating `compat/cpu-suites.json` as executable configuration.
- Expand TZX flow-control tests, floating-bus calibration and local compatibility
  artifacts; keep optional assets outside the repository.

## Later workbench capabilities

- Live reload/binary insertion into an existing machine with explicit provenance
  and state-placement rules; retain the current fresh-process workflow.
- Durable project/session formats, machine snapshots and tape-position recovery.
- Control-flow graphs, hot paths, candidate routines and evidence-based discovery.
- Integrated annotated export, additional assembler dialects and source recovery
  beyond linear byte reconstruction.
- Settings/load-save UI completion and full-program audio regression metrics.

## Supporting proposals and history

- [Enhanced roadmap](enhanced-roadmap.md): proposed M0–M6 requirements; partial
  delivery does not imply entire milestones or acceptance scenarios are complete.
- [Reverse-engineering roadmap](reverse-engineering-roadmap.md): L1–L9 vision.
- [Forward-loop plan](spectrum-development-loop-plan.md): delivered milestone and
  historical implementation sequence.
- [Current handoff](../../handoff.md): short continuation guide.
- [Early TODO](../archive/early-todo.md): historical record.
