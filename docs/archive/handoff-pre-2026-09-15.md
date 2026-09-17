> Historical snapshot preserved on 15 September 2026. Statements of current
> behavior and verification below refer to the original document period.
> Use [current status](../reference/status.md) for the maintained baseline.

# Development handoff: forward loop complete, ROM-first reverse direction next

**Updated:** 14 September 2026.
**Working branch:** `feature/spectrum-dev-loop`, based on `98cf7fb`.
**Git state:** implementation, documentation and example edits remain
uncommitted. Inspect `git status` and the diff before editing or staging.

For the subsequent backward-browsing increment, see the
[observed disassembly checklist](../developers/observed-disassembly-checklist.md).
It adds memory-address browsing and bounded execution history; it does not yet
provide persistent analysis save/reopen.

The next agreed increment is documented in the
[address-based metadata developer checklist](../developers/address-metadata-checklist.md).
It replaces history-dependent address evidence with bounded cumulative metadata
and independent execution, self-modification, current-observation and protection
properties. Implementation has started after checkpoint `a3dd462`: durable per-address
instruction observations and independent UI properties are in progress. See the
checklist for tested changes and remaining work.

## Constraints and current direction

Implementation continuation: see the [ROM reverse-loop checklist](../developers/rom-reverse-loop-checklist.md)
for completed baseline, instruction-control/runtime work and fresh verification.
The timing decision is resolved: instruction-level control preserves Spectrum
display progress; separate HALT/interrupt-signal fidelity corrections are deferred. The forward-loop history below is retained.

- The first objective was a small, complete development loop: edit Z80 assembly
  in VS Code, assemble it, and run it on the emulated Spectrum 48K. That loop
  works and the user has independently edited and run a program through it.
- The next objective is the reverse direction: load an existing binary,
  execute/debug it, and use runtime observations to build a useful disassembly
  and persistent understanding. **Start with the Spectrum 48K ROM.**
- Keep the external Pasmo CLI and independently executable language server.
  The debugger, terminal and editor should share capability boundaries; do not
  introduce an embedded assembler or an editor-specific execution engine.
- Preserve the distinction between original bytes, current machine state,
  observed execution, decoder interpretation, imported names and user meaning.
  A byte not observed executing is not thereby proven to be data.
- Preserve the user's scratch program. Do not replace it with the original
  demonstration source to restore an old test expectation.
- Use focused checks while implementing, then verify the actual CLI and GUI
  boundaries. Report passes, skips and untested claims separately.
- This file is the current task handoff. [disassembly/workflow history](disassembly-workflow-history.md) retains the
  earlier disassembly investigation and the initial forward-loop handover.
  Older roadmaps contain outdated implementation and assembler statements.

## What has been completed

### The forward development loop

1. The existing VS Code language client provides Pasmo-oriented diagnostics,
   completion, symbols and navigation through the independent `lsp-z80` server.
2. Example-local VS Code tasks invoke `tools/spectrum_dev.py` as an ordinary
   external process. **Spectrum: Build** and **Spectrum: Build and Run** are
   tasks, not top-level command-palette commands.
3. The CLI invokes external Pasmo 0.5.5, validates outputs, resolves entry and
   labels, records build identity, and launches the debugger after success.
4. The debugger can now combine a Spectrum ROM with a raw RAM program. It
   validates placement and startup state, imports symbols, and starts either
   paused at entry or running.
5. The Spectrum screen, registers, disassembly and execution controls can be
   inspected together. Initial screen/keyboard positions were adjusted to keep
   the controls and disassembly visible.

Verified in the installed VS Code client: cross-file definition lookup,
completion, red-to-green source edits producing different running output,
assembly failure stopping the run, and recovery after correcting the source.
Verified in the debugger: imported `start`/`idle` labels, pause, single-step and
resume. A step of the original idle `JP` advanced ten T-states.

The user subsequently expanded the example into a three-color program with
`setcolor` and `delay` routines, fixed a delay-loop branch, and confirmed the
updated program works. Investigation of the suspected stale build showed that
the running debugger used the latest 61-byte binary and that both input hashes
matched the then-current source. The issue was an infinite software delay,
not failure to load the new binary. The user also confirmed symbol names were
visible in the debugger.

### Current user source and register conventions

`examples/spectrum-dev/main.asm` is now the user's scratchpad, not the original
21-byte acceptance fixture. It calls `setcolor` for red, green and blue, with
delays, then loops at `idle`. `palette.inc` defines those colors.

Input and `Clobbers` comments were added to each routine. At this handoff,
`delay` consumes A and B and does **not** preserve BC. A version using
`push bc` / `pop bc` with a separate `delay_outer` label was discussed and shown
in conversation, but was not applied. Do not claim that change is implemented.
The current inner-loop branch targets `inner_delay`, avoiding repeated
reinitialization of B on every decrement.

## How to use it

Open `examples/spectrum-dev/` as a folder, or the locally configured
`build/spectrum-dev.code-workspace`. Then:

1. Edit and save `main.asm` or its include files.
2. Press **Shift+Command+P → Tasks: Run Task → Spectrum: Build and Run**,
   or use **Terminal → Run Task**.
3. Close the current debugger window before launching the next build.

The generated local workspace supplies these tool paths in its terminal
environment:

- Pasmo: `build/tools/pasmo-0.5.5/pasmo`.
- Debugger: `build/z80_debugger`.
- ROM: `roms/spec48.rom`, through the existing local ROM-directory symlink.
- Language server: `lsp-z80/.venv/bin/lsp-z80`.

These local artifacts may need recreating on a new checkout. Machine-specific
paths are not committed. VS Code task terminals can retain an older environment;
close/recreate them after changing tool paths. A temporary macOS `.app` wrapper
was used for UI automation only; the configured workspace now targets the
ordinary CMake-built executable, avoiding a stale copied debugger.

From the example folder, with `Z80_PASMO`, `Z80_DEBUGGER` and `Z80_SPEC48_ROM` set:

```sh
python3 ../../tools/spectrum_dev.py build
python3 ../../tools/spectrum_dev.py run
python3 ../../tools/spectrum_dev.py build-run
python3 ../../tools/spectrum_dev.py build-run --paused
```

See [the example README](../../examples/spectrum-dev/README.md) for setup and options.
Its simple red-to-green walkthrough describes the original demonstration;
the current scratch program has since evolved.

## Build and launch mechanics

`spectrum-project.json` specifies source, include paths, defines, origin,
entry label/address, SP and an initial stack reserve. The current origin/entry
is `$8000`; SP is `$ff00` with 256 bytes reserved below it.

Each build uses a new attempt directory under the example's ignored `build/`.
Pasmo runs once to discover inputs from its verbose/debug output, and again
with input hashes checked for stability. This includes nested includes and
executed INCBIN inputs. Pasmo's emitted binary range is checked against the
configured load origin and actual size.

Successful output contains `program.bin`, original `program.pasmo.sym`, converted
`program.debug.sym`, `program.build.json`, and assembler stdout/stderr. The build
record identifies source/include/configuration/assembler hashes, artifacts and
resolved launch state. `build/last-build.json` is an atomically published pointer
to a successful set, or a failed/incomplete marker. A later `run` checks source,
configuration and artifact identity before launching. A failed build does not
silently run an earlier binary. Run one build per project at a time; this is
not an atomic filesystem snapshot or a concurrent-build service.

The symbol adapter imports listing-confirmed labels within the image. Constants,
out-of-image names and duplicate address aliases are reported as omitted while
remaining in Pasmo's original output. The debugger currently stores one name
per address. Imported names are not automatically classified as functions/data.

The standalone loader checks the exact 16 KB ROM, RAM-only non-wrapping program
range, entry inside that range, writable non-overlapping stack reserve, and
symbol-file validity. It reads back loaded bytes and establishes reset CPU
state, explicit PC/SP and disabled interrupts. Host loading does not count as
executed-code writes. The machine has not run BASIC initialization.

**Reset still cold-boots the ROM and clears the RAM program.** Restart a program
through a fresh build/run. Live reload, source-line debugging and ROM-dependent
program startup remain deferred.

## Verification and its limits

The completed initial implementation passed **33 CTest checks and 18 Python LSP
tests**. ZEXDOC and ZEXALL skipped because their external assets were absent.
ROM-backed boot/debug checks ran with an explicit ROM path. Both Pasmo
disassembly gates passed. Ten additional CLI cases covered valid existing modes
and invalid new launch inputs. The installed VS Code client and GUI were
exercised, not merely packaged.

See [the verification record](../testers/spectrum-development-loop-verification.md)
and local `build/dev-loop-evidence/` artifacts. These results precede the user's
later scratchpad changes; the full suite has not been rerun against those edits.
In particular, `tests/spectrum_dev_test.py` currently copies source from the
editable example and assumes its original shape. **Separate stable test fixtures
from the user's scratchpad before relying on that test as an ongoing gate.**

## Reverse direction: what already exists

- ROM-only Spectrum launch, with screen, keyboard, pause/run/step and breakpoints.
- Live-memory disassembly and typed JSON symbol/description save/load.
- Coverage flags for executed starts/decoded operand spans, not execution counts.
- Write watchpoints, blocked-ROM-write events, and writes to previously executed
  code. This is not a complete generated-code or mutable-code history.
- `z80_disassemble`: bounded, linear Pasmo source reconstruction with explicit
  raw-byte fallbacks and byte-preservation gates. It does not use runtime
  evidence to separate code/data or export the debugger's full annotations.
- A small `examples/spectrum48k.sym` annotation seed. It has no demonstrated
  hash binding to every possible 48K ROM; inspect applicability before using it.

Start the ROM directly, from the repository root:

```sh
./build/z80_debugger --spectrum roms/spec48.rom
```

This starts paused at reset. Step from `$0000`, or press Run to boot. For an
explicitly seeded exploration, after checking the symbols against this ROM:

```sh
./build/z80_debugger --spectrum roms/spec48.rom --sym examples/spectrum48k.sym
```

Do not route ROM analysis through the standalone RAM-program build/run path or
inject an artificial entry/SP. Generic raw-binary debugging also remains
available, but arbitrary binaries will later need explicit machine/load/startup
contracts. Starting with the ROM gives us a fixed image and reset entry point.

## Next implementation sequence

### 1. Protect the baseline and establish the ROM subject

- Isolate the original two-color test fixture from the editable example without
  changing the user's source. Re-run the appropriate checks and record results.
- Select the local 48K ROM, check exact size, record its SHA-256 and load range
  `$0000..$3fff`, and record emulator revision/build identity. Do not distribute
  the ROM in Git.
- Verify paused reset state, initial stepping and a bounded run in the existing
  Spectrum debugger. Begin with no imported labels to demonstrate what runtime
  evidence adds; make loading the annotation seed a separate explicit action.
- Produce a baseline linear export and reassemble it at origin zero; compare
  all 16384 bytes. This size is within the current export contract. A pass proves
  byte preservation, not that every rendered instruction is actually code.

### 2. Record a bounded, trustworthy execution sample

Extend the UI-free execution boundary with optional observation. For the first
ROM sample, capture ordered instruction starts, bytes at execution time,
registers/flags before and after, resulting PC, and emulator T-state deltas.
Accumulate execution counts and first/last observations for ROM addresses.
Keep transitions into RAM visible even if full RAM reconstruction is deferred.

Make the observation boundary precise before calling it an instruction trace.
The current debugger stops prefix stepping after an eight-step guard; it must
not silently publish a partial instruction as complete. Coverage spans currently
come from the independent decoder. Distinguish observed PC transitions from
decoder-derived operand spans and control-flow classifications. Interrupt entry
must be distinguishable from an ordinary instruction transition.

Use a bounded trace with explicit retention/dropped-event information. Record
whether a run stopped on a breakpoint, HALT, user pause or a budget. Do not add
unbounded per-instruction history to every CPU configuration.

### 3. Save the evidence and annotations

Start with a small, inspectable local analysis artifact: ROM identity, machine
and emulator identity, run starting context, coverage/counts, bounded events,
and separately sourced annotations. Version its format. JSON metadata and
JSONL events are a proposed first implementation, not an agreed permanent schema.

Saving an analysis must not be presented as saving a resumable emulator. A
reliable resume would also require RAM, device, interrupt and timing state.
The first acceptance target is reopening the same ROM and retained evidence,
not exact replay or time travel.

On a different ROM hash, reject or explicitly rebind annotations; do not silently
apply them. Preserve imported labels separately from user edits and generated
names. User conclusions should be revisable without rewriting the observations.

### 4. Show what execution taught us

Anchor listing rows to observed instruction starts and expose counts and observed
successors. Keep statically decoded but unobserved ranges visibly distinct.
Let the user name and describe an observed ROM routine, save, reopen and retain
that work. Show the trace/register context that supports an interpretation.

Defer automatic function recovery, semantic explanations and a complete call
graph until the underlying observations are reliable. Numeric successor edges
can be recorded before confidently classifying them as calls, returns or loops.

### 5. Export a small understood ROM region

Choose one executed routine and export its bytes with the retained labels and
comments, preserving unknown surrounding bytes explicitly. Reassemble and
compare against that exact ROM range. Keep debugger display names separate from
Pasmo-safe export identifiers: existing ROM labels such as `START/NEW` and
`ERROR-1` need a deterministic name mapping, not blind insertion into source.

Byte-exact export must remain independent of whether the inferred code/data
classification is correct. A readable, round-tripping listing is useful evidence,
but does not recover the ROM author's original source or intent.

## First reverse-loop acceptance

Load the identified ROM at reset; step and collect a bounded execution sample;
see observed starts/counts and transitions; annotate one routine; save and close;
reopen the same ROM with that evidence and annotation intact; export a selected
range and reassemble it to identical bytes. A wrong ROM hash must be detected,
and unobserved bytes must remain unobserved rather than being labeled data.

Known HALT/time, frame-driver duplication and instruction-boundary concerns are
documented in [the architecture review](../developers/source-architecture-feedback.md).
The first short sample can stop before HALT. Before claiming a continuous,
time-faithful boot trace, resolve the relevant clock/interrupt/frame behavior
with focused tests. The prior instruction to treat CPU execution as the reference
was a display-investigation boundary, not proof of hardware conformance.

Raise an architectural question if meeting this acceptance requires choosing a
shared Spectrum runtime owner, defining resumable full-machine state, or choosing
a broader temporal model for mutable RAM. Do not quietly expand those boundaries
under the guise of adding a trace panel. ROM loading and bounded observation can
be advanced first; no new emulator or universal reverse-engineering framework is
needed to begin.

## Main implementation files

| Files | Responsibility |
|---|---|
| `tools/spectrum_dev.py` | External assembly, dependency/artifact checks, symbol conversion, launch |
| `examples/spectrum-dev/` | User-editable program, configuration and VS Code tasks |
| `machine/spectrum/program_launch.h` | Headless standalone load validation and byte loading |
| `debugger/main.cpp`, `debugger/ui/debugger_app.*` | CLI composition, Spectrum setup and UI execution |
| `debugger/exec/debug_session.*` | Stepping, coverage, break/watchpoints and write events |
| `debugger/disasm/disassembler.*`, `pasmo_source.*` | Decoding and byte-preserving linear export |
| `debugger/symbols/symbol_table.*` | Current symbol metadata and JSON persistence |
| `machine/spectrum/ula.h`, `spectrum_machine.h` | Spectrum device and machine/frame behavior |
| `tests/spectrum_program_test.cpp`, `tests/spectrum_dev_test.py` | Forward load/build/run checks |
| `tests/assembly_roundtrip.py` | External Pasmo encoding and reconstruction gates |

## Memory policy boundary (observed browsing continuation)

`MetadataMemory` now owns byte revisions and instruction capture separately.
`FastMemory` and `ObservableMemory` are unchanged. `SpectrumMachineImpl<Memory>`
shares frame/ULA behavior between the lightweight viewer and metadata-enabled
debugger. The final suite passed 34 tests, with two external ZEX skips; CPU and
Spectrum policy parity are covered. The native scrolling/history interaction
check is now verified: backward scrolling disengages follow, stepping preserves
manual position, historical versions retain old bytes, and address navigation
opens current memory without rewinding execution. See
[the development checklist](../developers/observed-disassembly-checklist.md).
