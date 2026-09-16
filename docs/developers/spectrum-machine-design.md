# Spectrum machine design

**Audience:** developers working on the Spectrum 48K machine.
**Last reviewed:** 2026-09-15.
**Source of truth:** [SpectrumMachineImpl](../../machine/spectrum/spectrum_machine.h)
and the device headers beside it.

## Runtime ownership

`SpectrumMachineImpl<Memory>` owns the CPU, ULA, tape, frame deadline and latest
completed picture. `SpectrumMachine` selects `ObservableMemory` for the viewer;
`DebugSpectrumMachine` selects `MetadataMemory` for debugger/probe analysis.
Both use `ObservableIo<CallbackIo>`. Callback I/O connects the CPU to the ULA;
the ULA receives the CPU clock, a memory reader and write notifications.

Viewer, debugger and probe share this runtime. `DebugSession` attaches to the
machine CPU and uses execution hooks; it does not assemble a second Spectrum
from independent copies of the devices. The generic `machine/machine.h` frame
helper remains in the tree, but the current Spectrum lifecycle lives in
`SpectrumMachineImpl`.

## Instruction and frame progression

The PAL model uses 3.5 MHz CPU timing, 224 T-states per line and 312 lines per
frame (69,888 T-states). [timing.h](../../machine/spectrum/timing.h) defines the
constants. The CPU executes instructions/prefix stages atomically; this is not
a bus-cycle-stepped emulator.

`prepare_execution()` begins a frame if needed and makes the existing one-shot
maskable-interrupt attempt. `advance_execution()` checks consumed T-states,
finishes the picture at the frame boundary, preserves instruction overrun and
notifies the frontend. `step_instruction()` brackets CPU instruction execution
with those operations; `run_frame()` batches the same lifecycle.

Debugger hooks use the same preparation/advancement contract. Pause retains an
unfinished frame; stepping resumes it. Rendering returns the last completed
picture (or initial rendering before the first completed frame), so UI refresh
alone does not execute instructions or advance devices.

## Devices and memory

- **Screen:** Spectrum bitmap/attribute layout, ink/paper, BRIGHT and FLASH,
  border and beam-aware rendering through `video.h`, `screen.h` and `ula.h`.
  Memory-write notifications support display reconstruction within a frame.
- **Keyboard:** active-low matrix selected through the full port address.
- **ULA I/O:** keyboard/EAR input and border/MIC/speaker output; floating-bus
  reads derive from the ULA fetch phase. See [floating bus](floating-bus-design.md).
- **Tape:** `.tap`/`.tzx` parsing and EAR pulse playback. Some TZX flow-control
  blocks remain linearized; parsing a block is not proof of loader compatibility.
- **Audio:** beeper edge timing and resampling are machine capabilities;
  miniaudio output belongs to the GUI frontend.
- **ROM:** memory policies provide protection and refused-write callbacks.
  The low-level machine constructor does not enable protection automatically;
  frontends configure it. Normal debugger Spectrum launch protects ROM;
  `--writable-rom` is an explicit diagnostic override.

The low-level `load_rom()` accepts a nonempty image up to 16 KiB. The standalone
program launch contract is stricter: it requires exactly 16 KiB, RAM-only
program placement, an entry inside the image, and a non-overlapping writable
stack reserve. See [program_launch.h](../../machine/spectrum/program_launch.h)
and the [source workflow](../../examples/spectrum-dev/README.md).

Standalone programs start without BASIC initialization, with interrupts disabled
and RAM outside the program zeroed. The UI's **Restart program** action still
cold-boots ROM and clears RAM; rerun the build/run command to restart that binary.

## Fidelity limits

Contended memory timing is not implemented. The runtime retains an early-HALT
frame policy: it can close the frame at the current CPU cycle rather than
modeling continued halted bus/refresh/peripheral activity. Interrupt signaling
is a one-shot attempt at frame preparation, not a persistent hardware INT line.
NMI vector labels do not imply an NMI delivery implementation.

Shared scheduling and policy parity reduce frontend drift; they do not establish
complete Spectrum hardware fidelity. HALT/interrupt corrections need their own
focused implementation and acceptance before stronger timing claims.

## Validation

`machine_test`, `timing_test`, video/raster/floating-bus, keyboard/tape/beeper,
`spectrum_debug_test` and `spectrum_program_test` cover the relevant seams.
`spectrum_boot_test` needs a local ROM. Real tape/game compatibility and native
screen/audio behavior need separate evidence; see [testing](../testers/testing.md).

The [earlier design](../archive/spectrum-machine-design-pre-2026-09-15.md)
preserves the original milestones and hardware rationale. Current priorities
are maintained in [roadmap](roadmap.md).
