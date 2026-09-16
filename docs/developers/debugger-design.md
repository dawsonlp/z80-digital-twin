# Debugger design

**Audience:** developers changing debugger execution, evidence or presentation.
**Last reviewed:** 2026-09-15.
**Status:** describes the implemented source; remaining acceptance is tracked in
[address metadata](address-metadata-checklist.md).

## Composition

`z80_debugger_core` contains `DebugSession`, `InstructionHistory`, address
listing, the disassembler, Pasmo source rendering and `SymbolTable`. It has no
UI dependency. `z80_debugger` adds `DebuggerApp`, panels, GLFW/ImGui/OpenGL,
native file dialogs and audio. There is no plugin host or EventBus.

`DebugSession` references a `DebugCPU` configured as
`CPUImpl<MetadataMemory, ObservableIo<CallbackIo>>`. Generic debugging owns
that CPU directly; Spectrum debugging uses the CPU inside
`DebugSpectrumMachine`. The session attaches memory observers and removes them
on destruction. The CPU must outlive the session.

## Execution

The CPU owns bounded instruction stepping. The session supplies breakpoint,
watchpoint, self-modification and machine lifecycle control. A step may stop
with an incomplete-instruction result after the prefix-stage budget (4096 by
default); the next action continues the same instruction and capture.

`RunSlice()` is instruction-budgeted; `RunForTStates()` is T-state-budgeted.
Both can stop early. Resuming at a breakpoint steps past it once before checking
again. Machine preparation occurs before breakpoint evaluation, and machine
advancement follows execution. Accepted machine entry is recorded separately
from instruction execution.

`StepOver()` decodes CALL/RST, places a temporary return-address breakpoint and
runs synchronously. It can stop at a user breakpoint inside the routine and
has a simple recursion limitation: the first arrival at that address ends the
step-over. This is not stack-aware source debugging.

The session exposes registers, stop reasons, coverage, dirty cells, write
watchpoints, SMC events and refused writes. Conditional breakpoints and general
read-watchpoints are not implemented.

## Evidence and byte activity

`MetadataMemory` retains byte revisions and counters for instruction reads,
data reads, accepted CPU writes, value changes, refused writes and host changes.
Each activity kind has first/latest attribution; CPU scopes identify the
original instruction start across prefix stages and distinguish interrupt entry.
Debugger and device inspection does not increment CPU data-read counters.

`InstructionHistory` retains completed-start counts and one latest observation
per address, including captured bytes/revisions, successor and elapsed T-states.
It also retains a chronological queue of at most 8192 events. Evicting an old
event does not remove address evidence. Captures contain at most 256 bytes;
longer completed instructions are explicitly partial and cannot establish a
fully observed span. Incomplete execution is not published as a completed start.

Execution and self-modification are sticky facts within the analysis lifetime.
Current-byte validity is separate: changing any captured byte invalidates the
observation, even if restored later. Same-value and refused writes do not.
Re-execution can establish valid current bytes while preserving self-modification.
Host mutation invalidates bytes without becoming an emulated instruction write.

The legacy coverage bitmap includes decoder-derived operand spans; it is not
the same thing as captured instruction-stream reads. SMC/refused-write logs are
bounded diagnostic lists, not a complete mutation archive. Per-byte activity
storage is fixed by address count; per-address captures and recent events are
also bounded. Full allocation/overhead measurements and counter-overflow review
remain acceptance work; counters saturate and revision overflow invalidates
observations, but that does not prove every sequence counter is covered.

## Lifecycle

| API / UI action | CPU / machine effect | Analysis effect |
|---|---|---|
| Pause, Step, Run | Control execution through the same instruction boundary | Retains accumulated evidence. |
| `ResetCpu()` (core API) | Reset CPU, pause, abandon pending capture | Preserves accumulated analysis. |
| `ClearAnalysis()` / **Clear analysis** | Preserves registers, bytes and protection; does not pause a running session | Clears activity, counts, history, coverage and diagnostic logs. Refused while an instruction is pending. |
| `Reset()` | CPU reset | Also clears analysis. |
| **Restart program** in Spectrum UI | Cold-boots ROM and clears RAM | Clears analysis; does not reload the standalone program. |
| Fresh process or Spectrum program launch | Creates a new session / configures the machine | Fresh analysis boundary. |
| `LoadProgramFile()` / GCD demo load in an existing session | Resets CPU and loads bytes | Does not call session reset; prior analysis is not uniformly cleared. |

Replacement-load lifecycle remains an acceptance gap: generic binary/GCD loading
uses CPU reset directly, unlike the session-reset path used for Spectrum and SMC
setup. Do not treat every load call as a fresh analysis epoch.

CPU-only reset is not a separate UI command. Analysis does not survive process
exit. Symbol files persist independently. Machine resume, replay and persisted
analysis epochs are not implemented.

## Disassembly and presentation

The stateless `Disassembler::Decode(ByteReader, address, resolver, available)`
reads current bytes, reports bounded/incomplete input and resolves optional
symbols. It has no dependency on the live prefix state. `BuildAddressListing`
combines observed starts, architectural vectors, PC and navigation destination;
it preserves overlapping starts and bounds tentative decoding around anchors.

The address view covers the full 64 KiB space with Go, navigation history,
manual scrolling and Follow PC. RO/X/SM/O markers independently show protection,
execution, self-modification and current-byte observation; tooltips expand
activity details. The recent-history view shows bytes at execution time,
including older versions still in its queue. Selecting history does not rewind
machine state. Rendering is inspection, not another execution path.

## Symbols and export

`SymbolTable` maps addresses and names, with label/function/jump/data and
byte/word-variable types, description and region size. JSON `.sym` load/save
is forgiving of malformed entries. Pasmo's text symbol format must be converted;
`tools/spectrum_dev.py` produces the debugger JSON file.

Fresh UI symbol tables receive architectural reset/RST/NMI vector names without
overwriting existing names or addresses. These are naming conventions, not
observed execution, selected interrupt mode or implemented NMI delivery.

`z80_disassemble` produces byte-preserving Pasmo source for a supplied raw binary
range. It does not yet export accumulated debugger evidence, symbols and
annotations as a durable reverse-engineering project. See
[disassembly verification](../testers/disassembly-verification.md).

## Validation and remaining work

Headless checks include `debug_session_test`, `instruction_history_test`,
`disassembler_test`, `symbol_table_test`, `spectrum_debug_test` and
`spectrum_program_test`. The first two exercise execution boundaries, mutation,
retention and lifecycle; machine tests cover shared scheduling and policy parity.
CLI/GUI verification remains separate from unit tests. See the
[current verification record](../testers/documentation-refresh-verification.md).

Persistence, complete access-attribution auditing, full resource/overhead
measurement and remaining native interactions are tracked in the
[address checklist](address-metadata-checklist.md) and [roadmap](roadmap.md).
The [previous design](../archive/debugger-design-pre-2026-09-15.md) preserves
historical decisions and superseded proposals.
