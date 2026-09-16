# Observed disassembly and backward browsing

> Historical acceptance record for the first observed-browsing increment.
> The [address-metadata checklist](address-metadata-checklist.md) supersedes
> queue-dependent address retention and the exclusive status model. Its checked
> items below do not certify all later metadata changes.

**Historical increment:** the next [address-metadata checklist](address-metadata-checklist.md)
supersedes the eviction-dependent evidence and exclusive Modified/Observed model
below. Its implementation is pending; retain this document as delivery evidence.

## Analysis and approved boundary

The existing pane starts at the paused PC and generates 256 forward rows.
Its Back button is navigation history, not execution history. Coverage bits
describe past execution and decoded spans; they do not establish that current
bytes still describe the observed instruction.

Implement two views over one headless evidence model:

- Address order: browse all memory, use unchanged observed starts as anchors,
  and identify speculative decoding. Manual scrolling suspends Follow PC.
- Execution order: inspect retained completed instructions using captured
  instruction bytes, including previous versions at the same address.

Instruction-byte reads are captured at the CPU/memory-policy seam rather than
reconstructed from a later memory image or decoder length. Data reads and
interrupt entry do not become instruction bytes. Per-byte change revisions
include host RawWrite loads, preserve same-value writes, and exclude refused
writes. A changed-then-restored byte remains changed since that observation.

Records are immutable. Their applicability to current memory is computed
separately. Operand changes, overlapping starts, wrapped instructions and writes
made by the instruction itself must be handled. No code/data classification,
historical machine replay or on-disk persistence is implied.

Use a bounded recent history: 8192 events and at most 256 captured bytes per
instruction. Overlong captures are explicit and cannot establish a reliable
span. Eviction removes unavailable anchors; cumulative counts distinguish
previously executed addresses whose evidence is no longer retained. These are
local implementation limits, not a permanent analysis-file schema.

## Memory implementation selection

The user's clarification requires metadata to be a separate interchangeable
memory implementation. `FastMemory` and `ObservableMemory` retain their existing
implementations. `MetadataMemory` privately composes `ObservableMemory`, retaining
its observer/protection behavior while adding revisions and instruction capture.
There is no public escape to the underlying writable storage. Revision changes
precede write notifications; RawWrite still bypasses notifications/protection.

CPU selection remains compile-time via `CPUImpl<Memory, Io>`. The ordinary CPU
uses FastMemory. The Spectrum viewer uses ObservableMemory. DebugCPU explicitly
uses MetadataMemory. `SpectrumMachineImpl<Memory>` shares one ULA/frame lifecycle;
`SpectrumMachine` selects ObservableMemory and `DebugSpectrumMachine` selects
MetadataMemory. Spectrum requires observer/protection capabilities in addition
to the CPU's byte access contract, so bare FastMemory is not a Spectrum policy.
Supported CPU combinations are explicitly instantiated in `src/z80_cpu.cpp`;
adding a new combination currently also requires an instantiation there. This
change does not introduce runtime memory swapping or virtual dispatch.

## Development checklist

- [x] Add instruction-byte capture without instrumenting ordinary data reads
      or adding capture overhead to the bare FastMemory configuration.
- [x] Keep metadata in a separate memory implementation; preserve both existing
      policies and select the policy at the CPU/machine type boundary.
- [x] Version changed bytes, including host loads; retain existing write hooks.
- [x] Add a bounded headless record/index with sequence, start, successor,
      cycles, exact retained bytes and applicability to current memory.
- [x] Preserve pending captures across prefix budgets; publish only completed
      instructions. Represent machine preparation transitions separately.
- [x] Clear evidence on session reset; report eviction and truncated capture.
- [x] Build a full address listing with observed/PC/navigation anchors; preserve
      overlapping observed starts and explicitly mark speculative/raw rows.
- [x] Implement backward/forward scrolling, Go to and Follow PC without snapback.
- [x] Add an execution-history view showing original bytes and allowing a jump
      to current memory without pretending to restore historical machine state.
- [x] Test operand writes, self-overwrites, same-value/refused/host writes,
      restored bytes, multiple versions, overlapping/wrapped instructions,
      prefix continuation, data-read exclusion, eviction and reset.
- [x] Test CPU parity across all three memory policies and Spectrum parity
      between lightweight and metadata policies.
- [x] Test address layout before PC, preserved anchors and memory boundaries.
- [x] Run core/Spectrum regressions and compare bare CPU benchmark before/after
      as an informational measurement.
- [x] Verify actual GUI scrolling, Follow PC, historical versions and memory jumps
      in the rebuilt isolated metadata demo after unlocking the Mac.
- [x] Update user documentation and handoff with actual delivery and limits.

## Evidence and limits

The added `instruction_history_test` covers exact byte capture across 1792
opcode-family cases as an executor/decoder consistency check, plus targeted
mutation and navigation cases. This is not independent hardware conformance.
Final complete CTest run after separating the memory policies: 34 passes and
two external ZEX skips. CPU policy parity, Spectrum policy parity,
batch-versus-step, display, beeper and ROM tests passed. The policy parity test
compares T-states, frame deadlines, completed raster and audio edge timestamps.
Both existing memory implementation files are unchanged from HEAD.
Native GUI interaction verified on 2026-09-14 in the rebuilt isolated metadata
fixture (`3E 00 21 01 80 34 18 F8`, loaded at 8000, 32777 initial steps):
- Scrolling upward reached 7D9A and disabled Follow PC.
- Stepping advanced PC from 8005 to 8006 while the listing stayed at 7D9A.
- Execution history scrolled backward and disabled Follow latest; re-enabling it
  returned to the newest records.
- History retained LD A operand versions 00, 01 and 02, marked Modified.
- Clicking the historical 8000 entry opened current bytes 3E 03 with Modified
  evidence; PC remained 8006 and cycles remained 131180.
- Re-enabling Follow PC and stepping the JR moved PC and the highlighted listing
  to 8000 at 131192 T-states.
Initial automated input did not affect controls; a user Step click confirmed the
application was responsive, and subsequent automated interactions succeeded.

The bare CPU benchmark reported 194.68 MHz before and 219.25 MHz after on this
Debug build. These short, noisy measurements show no observed slowdown; they
are not a performance guarantee or a measured speedup. FastMemory has no
capture or revision state. MetadataMemory has a 65,536-entry byte-revision table
(512 KiB of counters) and bounded instruction-stream capture. History payload
is bounded to 8192 events times at most 256 bytes and their revisions, plus
container metadata and fixed per-address counts.

The memory view uses unchanged observed starts as reliable spans and modified
starts only as inspection markers. Historical mnemonic decoding uses captured
bytes and current symbol names; names are not a historical symbol snapshot.
Machine-preparation transitions are explicit, but this is not a full bus-event
trace, register history or persisted analysis. Closing the application loses
history; Reset clears it. The old memory-pane coverage colours remain historical
coverage, not current-byte validation.
