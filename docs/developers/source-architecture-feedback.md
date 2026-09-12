# Source Architecture Feedback

**Review date:** 2026-07-13

**Scope:** source structure, KISS, DRY, dependency coherence, test evidence, and
fidelity to the Z80 and ZX Spectrum 48K hardware model.

**Constraint:** feedback only; no implementation code was changed.

## Executive judgment

The repository has a sound foundation: the generic CPU is separated from
machine behavior, I/O is modeled as device interaction rather than storage, the
debugger core is UI-free, and most important behavior has headless tests. This
is a substantially better starting point than a monolithic emulator.

It is not yet structurally safe for rapid expansion into a programmer-assistance
tool. The main problem is not directory naming or lack of abstraction. It is
that several important truths have more than one implementation:

- the Spectrum execution loop is independently assembled in
  `SpectrumMachine`, `DebuggerApp`, and `spectrum_probe`;
- CPU instruction meaning is independently represented by the executor and the
  disassembler, with at least one known disagreement;
- the documented policy/configuration model differs from the types actually
  built;
- the documented timing and performance guarantees are stronger than the code
  currently enforces.

The recommended direction is therefore **consolidation before expansion**. Keep
the present layering, establish one authoritative machine runtime and one honest
CPU stepping contract, close the observed hardware-semantic gaps with tests,
then place reverse-engineering and development-workbench state above those
stable contracts.

## Evidence reviewed

- Current source and build graph at commit `928ef03`.
- The pre-existing worktree changes were left untouched. They include CMake and
  test-asset documentation edits, a new register-aliasing test, an IDE directory,
  and a ROM-download script.
- A clean headless rebuild completed successfully.
- CTest reported 31 registered tests: 29 executed and passed; `zexdoc` and
  `zexall` skipped because `Z80_COMPAT_ASSETS` was not set and the referenced
  external assets were no longer present.
- The available `spec48.rom` was found, so both `spectrum_boot_test` and
  `spectrum_debug_test` executed and passed rather than taking their skip path.
- Existing artifact logs record earlier successful ZEXDOC/ZEXALL runs. Those
  logs are useful historical evidence, but they were not revalidated in this
  review.
- The current Release benchmark completed at roughly 1.5 GHz aggregate
  Z80-equivalent throughput on this host. It is a measurement, not a regression
  gate.
- A test install to `/tmp` showed that the installed public headers cannot be
  included: `z80_cpu.h` includes `memory/fast_memory.h`, while installation
  flattens that header to `include/z80/fast_memory.h`.

## What is already coherent and worth preserving

1. **Layer direction is mostly correct.** The CPU core does not depend on the
   Spectrum, debugger, or UI. The debugger core and machine layer remain
   headless. Graphics and audio dependencies are confined to applications.

2. **The I/O model reflects the hardware.** `In(port)` and `Out(port, value)`
   use the full 16-bit port and do not imply that ports retain values. The
   passive `ObservableIo` transaction view is a good debugger boundary.

3. **Hardware mechanisms are separated into useful units.** Screen decoding,
   ULA behavior, tape parsing/playback, keyboard mapping, beeper resampling,
   memory observation, disassembly, and symbol handling can be tested without a
   GUI.

4. **The tests are behavior-oriented.** Focused flag, prefix, timing, interrupt,
   ULA, tape, and debugger tests make local diagnosis possible. Passing the
   available Spectrum ROM tests is meaningful integration evidence.

5. **The project has not overbuilt a plugin framework.** That restraint is
   consistent with KISS and remains appropriate. A programmer-assistance layer
   needs stable evidence and state contracts before it needs extensibility
   machinery.

## Findings, ordered by consequence

### 1. There is no single authoritative Spectrum runtime

**Observation.** `machine/spectrum/spectrum_machine.h:51-91` wires the CPU, ULA,
tape, memory observer, I/O callbacks, interrupt, and frame lifecycle.
`debugger/ui/debugger_app.cpp:142-179` wires essentially the same machine again,
and `debugger/ui/debugger_app.cpp:242-270` implements a second frame driver.
`examples/spectrum_probe.cpp:74-88` implements a third instrumented frame loop.

**Consequence.** Viewer, debugger, and headless-probe behavior can diverge while
each continues to pass its own tests. They already differ in how HALT, a
mid-frame debugger stop, frame completion, audio, and the generic `Machine`
class are handled. This is the highest structural risk for the proposed tool:
observations made by the reverse-engineering UI must describe the same machine
that executes in headless and viewer modes.

**Recommendation.** Make `SpectrumMachine` the sole owner of Spectrum wiring and
frame lifecycle. Give it one execution/stepper seam that can be supplied by a
fast runner or by `DebugSession`. Viewer, debugger, and probe should compose
that runtime rather than reproduce it. A paused frame needs to remain an
explicit machine state, not a frontend convention.

### 2. HALT stops emulated time instead of modeling halted bus cycles

**Observation.** `CPUImpl::HALT()` sets `_halted` and adds four T-states
(`src/z80_cpu.cpp:1572-1576`). `RunUntilCycle`, `SpectrumMachine::run_frame`, and
`DebugSession` then stop executing while halted. No T-states or refresh cycles
are added until an external interrupt is accepted.

**Hardware fact.** A halted Z80 continues normal M1 cycles, forces internal
NOPs, and maintains memory refresh until an accepted interrupt or reset. See
the [Zilog Z80 CPU User Manual](https://www.zilog.com/docs/z80/um0080.pdf),
"HALT Exit" and the HALT instruction description.

**Consequence.** Absolute CPU-cycle time becomes shorter than machine time when
software idles in HALT. Tape EAR timing and beeper sampling use that absolute
cycle count, and ULA event timestamps are derived from it. A ROM may still boot
because a frame interrupt wakes it, while time-dependent peripherals drift or
freeze during the omitted halted interval. `R` refresh behavior during HALT is
also absent.

**Recommendation.** Decide explicitly whether the core models instruction
results only or CPU bus-time. For the Spectrum path, the machine clock must keep
advancing through HALT. Prefer one authoritative time source and model halted
M1/refresh cycles at the fidelity required by tape, ULA, and interrupt
sampling. Add a test that halts for a known interval and checks T-states, `R`,
and peripheral time before accepting an interrupt.

### 3. CPU execution and disassembly disagree on opcode `ED 76`

**Observation.** The CPU maps `ED 76` to `SLL (HL)`
(`src/z80_cpu.cpp:584`, `4042-4058`). The disassembler's regular ED decoder maps
the same bytes to `IM 1` (`debugger/disasm/disassembler.cpp:25`, `138-163`). The
undocumented SLL memory form is `CB 36`, which the compact CB decoder already
handles. The regular ED decoding also identifies `ED 7E` as the IM 2 alias,
while the CPU leaves it as `ED_NOP`.

**Consequence.** The programmer-assistance surface can display one instruction
while executing another. `ED 76` currently mutates memory at HL instead of
setting interrupt mode 1. This is a direct correctness defect and a concrete
example of duplicated opcode knowledge drifting.

**Recommendation.** Correct the opcode behavior only after adding focused tests
for `ED 76`, `ED 7E`, and `CB 36`. Then add an executor/disassembler consistency
check over opcode families so instruction length and identity cannot silently
diverge. The undocumented ED alias pattern is summarized by the
[Z80 opcode decoder reference](https://www.z80.info/decoding.htm).

### 4. The advertised compile-time policy extension point is closed outside the core

**Observation.** `CPUImpl<Memory, Io>` is declared in `z80_cpu.h`, but all
template definitions live in `z80_cpu.cpp`. That file explicitly instantiates
only four configurations at `src/z80_cpu.cpp:4061-4076`.

**Consequence.** A consumer cannot define the documented `GpioIo`, `SerialIo`,
or a new machine memory policy and instantiate `CPUImpl` without editing the
core implementation and adding an explicit instantiation. This is controlled
variation, not an open policy interface. It conflicts with the documented
deployment model and will obstruct additional machines or tool-specific memory
policies.

**Recommendation.** Choose one honest contract:

- make policy specialization a real public template facility by exposing the
  required definitions; or
- keep a closed set of supported configurations and present named factories or
  types rather than advertising arbitrary policies.

Do not preserve an extensibility claim that external consumers cannot use.

### 5. The public stepping contract is internally inconsistent

**Observation.** `z80_cpu.h:91-92` says `Step()` executes one instruction.
Actually it consumes one opcode or prefix stage. Seventeen source/test files
repeat `do { Step(); } while (!InstructionComplete())`. The EI deferral flag is
cleared at the end of every `Step()` call (`src/z80_cpu.cpp:257-259`), not only
when the following whole instruction is complete.

**Consequence.** Callers must understand a private prefix-state machine to use
the CPU correctly. An interrupt injected by a new caller between prefix stages
can observe a state the public documentation says does not exist. The repeated
whole-instruction loops are DRY evidence that the abstraction boundary is in
the wrong place.

**Recommendation.** Expose a single public whole-instruction operation with a
clear result: T-states consumed, instruction boundary reached, and halt/stop
state. Keep byte/prefix progression private unless a future signal-level engine
genuinely needs it. Close EI deferral at a whole instruction boundary and test a
prefixed instruction immediately after EI.

### 6. `Machine::RunFrame` does not honor its short-run contract

**Observation.** The documentation at `machine/machine.h:46-50` says a short run
caused by a breakpoint carries the remainder into the next frame. The code at
lines 63-65 records only overrun; a short run sets `carry_` to zero. Devices are
then ticked and the frame count advances anyway.

**Consequence.** A caller that uses this documented path can turn a partial
frame into a completed frame and assert another interrupt too early. The
debugger avoids this by maintaining its own `frame_active_` state, which is
further evidence that the common abstraction is incomplete.

**Recommendation.** Make frame completion explicit. A stop before the budget is
consumed should return a partial-frame result and must not tick end-of-frame
devices or increment the frame number. Resume should consume the remaining
budget in the same frame.

### 7. The installed core library is not consumable

**Observation.** CMake installs all headers into one flat `include/z80`
directory. `z80_cpu.h` retains includes such as `memory/fast_memory.h`. A direct
compile against the test installation fails because that path does not exist.
There is also no exported CMake package target.

**Consequence.** The project currently works as a source tree, not as a library
that another programmer tool can reliably consume. That matters if the future
workbench, CLI, or integrations are intended to be separable applications.

**Recommendation.** Preserve header subdirectories during installation and add
an installed-consumer compile test. Decide later whether a full CMake package is
needed; the include contract should be fixed first.

### 8. Architecture documentation describes a different system

High-confidence examples:

- The documents describe a `SpectrumIo` type; the implementation uses
  `ObservableIo<CallbackIo>` and no `SpectrumIo` exists.
- The documents say `DebugSession` is templated on CPU configuration; the class
  is fixed to the `DebugCPU` alias.
- `ObservableIo` is described as recording a transaction cycle; it records a
  sequence number but no cycle.
- Runnable and debugger Spectrum configurations are described as distinct; both
  current paths use the same observable configuration.
- The performance benchmark is described as a build-failing regression
  guardrail. It is not registered with CTest, contains no baseline/threshold,
  and returns success regardless of measured throughput.

**Consequence.** Architectural decisions cannot govern changes when the map no
longer describes the code. New work will be designed against capabilities that
do not exist or guarantees that are not enforced.

**Recommendation.** Reconcile each statement by either changing the code or
changing the document. In particular, decide whether one instrumentable
Spectrum configuration is the intentional KISS choice. If so, document its
cost and stop claiming a separate zero-observation machine configuration.

### 9. Portability and object-lifetime assumptions are not encoded in the types

**Observation.** `RegisterPair` overlays a `uint16_t` and a two-byte struct in a
union and assumes a little-endian host. The new local register-aliasing test
explicitly says it is expected to fail on a big-endian machine. Reading a
different union member from the one most recently written is also not a portable
C++ representation contract. Release builds add `-march=native` globally.

`SpectrumMachine` is implicitly copyable even though its I/O handlers, memory
observer, ULA clock/reader, and generic `Machine` contain references or lambdas
bound to the original object's members. A default copy would therefore look
independent while still calling into the source instance. `DebuggerApp` also
adds a ULA memory observer without retaining its ID, so repeated Spectrum wiring
would accumulate callbacks and panels.

**Consequence.** These assumptions are stable on the current Mac build but are
unsafe foundations for a reusable library, saved sessions, worker instances, or
additional frontends. The failure mode is silent aliasing to another machine,
not a clean compile error.

**Recommendation.** Either encode the supported platform boundary explicitly or
use a defined register representation. Make callback-wired runtime objects
non-copyable/non-movable until deliberate rebinding semantics exist. Treat
snapshotting as serialization of explicit machine state, not copying the live
object graph. Keep host-specific optimization flags on benchmark/application
targets rather than silently making them part of every library consumer's
contract.

## Hardware fidelity boundary

The project is already functionally useful, but "cycle accurate" would be too
strong for the present implementation. The following boundaries are visible in
the code and should remain explicit in user-facing claims:

- **Memory and I/O contention are absent.** This is already documented and is
  the largest known Spectrum compatibility gap. The
  [48K Spectrum technical reference](https://worldofspectrum.org/faq/reference/48kreference.htm)
  describes both memory and port-contention effects.
- **The frame interrupt is an instantaneous method call.** Real `/INT` is a
  signal sampled at instruction boundaries over a finite assertion interval.
  Software that changes interrupt state during that interval can distinguish
  the models.
- **Screen reconstruction is scanline-granular, not byte-fetch-granular.** The
  `FrameSource` passes address and line, but not column/fetch time. All 32 bytes
  on a line use the line-start cutoff, so a write during the active part of a
  line cannot be reconstructed at its actual fetch position.
- **Floating-bus calibration is not externally closed.** The internal fetch
  pattern has focused tests, but `kFloatingBusReadT` remains zero with an
  explicit `fbustest` calibration note.
- **NMI injection is absent.** `RETN` exists, but there is no external NMI
  request path.
- **Interrupt mode 0 is narrowed to RST opcodes.** `Interrupt(bus)` masks the bus
  byte into a restart vector. The Zilog manual specifies that IM 0 can execute
  any instruction placed on the bus, including a multi-byte instruction whose
  remaining bytes come from memory. Either implement that behavior or name and
  document the RST-only restriction.
- **Undocumented WZ/MEMPTR effects remain an acknowledged unknown.** That is an
  acceptable boundary if it remains visible and is not conflated with broad
  ZEX pass evidence.

These are not all immediate blockers for the programmer-assistance direction.
They are fidelity classes that need names. A useful distinction is:

1. instruction-functional Z80;
2. T-state-accounted Z80;
3. bus-event-accurate machine;
4. signal/cycle-accurate machine.

Each feature and test can then state which class it supports.

## KISS and DRY assessment

### CPU implementation

`z80_cpu.cpp` is 4,078 lines with 339 instruction/helper member definitions;
roughly 142 handlers are register variants of load or ALU families. Explicit
handlers are easy to inspect and may be good for performance, so size alone is
not evidence that a rewrite is justified. The real maintenance problem is that
opcode identity, length, timing, prefix behavior, and disassembly are maintained
in different forms.

A large instruction-description DSL would add risk and violate KISS unless it
demonstrably improves correctness. Prefer smaller reversible moves:

- centralize whole-instruction stepping;
- share or mechanically compare opcode metadata where execution and analysis
  must agree;
- extract tested ALU/load primitives where variants are truly identical;
- split files by stable concern only after behavioral contracts are locked;
- benchmark every core refactor against the bare CPU configuration.

### Machine and frontend code

The strongest DRY violation is the duplicated Spectrum assembly and frame loop,
not repeated file-reading helpers. Consolidating the runtime will remove more
risk than introducing a general frontend framework. After that, small shared
helpers for ROM discovery, keyboard projection, tape loading, and audio pumping
may be justified if viewer and debugger still perform identical work.

`Device`/`Machine` is currently an incomplete abstraction: `Device::OnFrame()`
is used only by `machine_test`; the ULA does not implement it, and the debugger
bypasses `Machine`. Either make this pair the real machine lifecycle or remove
it. A tested abstraction with no production consumer adds conceptual weight
without reducing duplication.

### State for the future programmer tool

Do not place project knowledge into the CPU or ULA. Keep three kinds of state
separate:

- **machine truth:** registers, memory, devices, machine time;
- **observed evidence:** executed addresses, reads/writes, I/O, interrupts,
  provenance, trace positions;
- **human/tool interpretation:** labels, comments, code/data classification,
  function hypotheses, source mappings, and assembler configuration.

That separation preserves the reality-first property of the proposed tool: an
annotation can be wrong without changing the underlying execution evidence.

## Recommended order before major feature growth

1. Add focused failing tests for `ED 76`/`ED 7E`, HALT time/refresh, EI followed
   by a prefixed instruction, partial-frame resume, and installed-header use.
2. Establish one public whole-instruction stepping result and one authoritative
   machine-time contract.
3. Make `SpectrumMachine` the sole Spectrum composition root and run viewer,
   debugger, and probe through it.
4. Resolve the policy-extension decision and make the public API match it.
5. Reconcile architecture/status documentation with the actual types and
   guarantees.
6. Turn performance into either a real, host-calibrated regression check or an
   explicitly informational benchmark.
7. Begin the programmer-assistance layer above a durable event/evidence model,
   without coupling annotations to CPU internals.

## Recurring architecture analysis loop

Use this loop for each significant change:

1. **Observe:** record the current behavior, test result, dependency path, and
   performance measurement before proposing structure.
2. **Classify the claim:** distinguish hardware fact, verified project behavior,
   inference, and desired capability.
3. **Trace authority:** identify the one component that owns the behavior and
   every consumer that depends on it. Multiple owners are a stop signal.
4. **Apply KISS:** choose the smallest change that creates an honest contract or
   removes duplicated truth.
5. **Apply DRY to knowledge, not syntax:** prioritize duplicated rules, timing,
   opcode meaning, and lifecycle over harmless repeated plumbing.
6. **Test against consequence:** use focused unit tests, an integration path,
   and the relevant external hardware oracle or exerciser.
7. **Attempt disconfirmation:** run an edge case likely to distinguish the model
   from real hardware or from another frontend.
8. **Measure and record:** capture correctness, fidelity class, performance, and
   known exclusions in the living architecture/status documents.

## Decision summary

The project does not need a broad architectural rewrite. It needs a narrower
set of authoritative contracts:

- one meaning for each instruction;
- one whole-instruction stepping API;
- one machine time base;
- one Spectrum runtime;
- one explicit fidelity classification;
- one honest public extension/install surface.

Once those are real, the existing debugger, observation hooks, disassembler,
symbols, and headless tooling form a credible base for reverse engineering and
new Z80 development.
