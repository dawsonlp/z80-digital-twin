# Step one: the Spectrum assembly development loop

**Date:** 13 September 2026  
**Status:** First loop implemented and verified on macOS, 14 September 2026.  
**Reviewed baseline:** `dev`, commit `98cf7fb`.

## Delivery status

Implemented on `feature/spectrum-dev-loop`: external build/run CLI, identifiable
Pasmo artifacts and debugger symbols, validated standalone Spectrum launch,
VS Code example tasks, and initial Spectrum panel placement. See the
[example instructions](../../examples/spectrum-dev/README.md) and
[verification record](../testers/spectrum-development-loop-verification.md).

The six-step acceptance scenario below was exercised through the installed
VS Code client and the Spectrum debugger. The final suite passed 33 CTest checks
and 18 Python LSP tests; two external CPU exercisers skipped for missing assets.
No architecture decision blocked this increment. The remaining sections retain
the implementation plan and boundaries, not a claim that later stages shipped.

## Constraints and prerequisites

- Deliver the user's first milestone: edit Z80 assembly in Visual Studio Code,
  assemble it, and run the resulting program on the emulated Spectrum 48K.
- Keep the existing independently executable language server and thin VS Code
  client. Editing must not depend on a running emulator.
- Use external Pasmo 0.5.5 for the first implementation. Use ordinary CLI
  commands and inspectable files; VS Code tasks invoke the same workflow as a
  terminal. Do not embed an assembler or introduce a build daemon.
- Launch a fresh Spectrum debugger process for each run. The user closes the
  previous window before repeating the loop. Live reload and session recovery
  are later work.
- Use a standalone RAM program with an explicit origin, entry point and stack.
  The initial program initializes its own state and does not depend on BASIC
  startup, ROM routines or interrupts. This is a development launch into a
  Spectrum machine, not a simulation of loading a program through BASIC.
- Keep Spectrum setup in the machine/debugger integration, and build tooling
  outside the generic CPU. Preserve existing binary, ROM and tape workflows.
- Validate a whole load before changing memory. Do not rely on `LoadProgram`
  silently stopping at the address boundary.
- Required local tools: C++23 toolchain, CMake, GUI dependencies, Pasmo 0.5.5,
  the language server's supported Python, VS Code and the existing extension.
  A valid user-supplied 16 KB Spectrum 48K ROM is required; do not commit it.
- Verify the small pieces during implementation, then prove the complete loop
  in the installed VS Code client and a visible Spectrum debugger window.
  Headless tests alone do not establish completion.

This plan takes priority for the next development increment over the broader
[enhanced roadmap](enhanced-roadmap.md). Runtime-informed reconstruction remains
the next product stage; it is not a prerequisite for this loop.

## Outcome

The user opens a supplied example folder in VS Code, edits `main.asm`, invokes
**Spectrum: Build and Run**, and sees the program's result on the Spectrum
screen. After closing the debugger, the user changes the source and runs the
task again; the new result corresponds to the new build.

The first example fills the Spectrum attribute area with a chosen color and
sets the border, then loops indefinitely. It uses documented instructions,
disables interrupts and avoids HALT. Its source exposes an obvious color
constant to edit. Exact raster timing, ROM services and interrupt-driven
animation are outside this first example's contract.

## What already exists and what is missing

| Area | Inspected foundation | Work required |
|---|---|---|
| Editing | `lsp-z80/` includes Pasmo analysis and a VS Code client | Package/configure the client and server for the example; verify installed behavior |
| Assembly | External Pasmo is the selected tool; assembler verification fixtures exist | Add a repeatable build command and explicit artifacts |
| Loading | Generic binary loader and Spectrum ROM setup exist | Compose ROM setup with validated RAM loading and explicit launch state |
| Execution | Spectrum debugger has screen, run/pause, stepping and breakpoints | Start the new program in Spectrum mode and verify its visible result |
| Symbols | Debugger loads typed JSON symbols | Convert Pasmo address symbols without confusing the two formats |
| Editor workflow | Extension supplies language features | Supply example-scoped settings and build/run tasks |

At the reviewed baseline, `debugger/main.cpp` chooses Spectrum ROM loading
instead of raw binary loading when both arguments are supplied. The existing
`LoadProgramFile` also resets the CPU. It must not simply be called after ROM
setup without defining and checking the resulting machine/session state.

The preceding review ran all 18 Python language-server tests successfully.
It did not revalidate the GUI or installed extension. Pasmo and `lsp-z80` were
absent from that shell's PATH. Existing binaries and historical test results
are not proof that this proposed workflow works.

## Implementation sequence

### 1. Establish the local baseline

1. Confirm the current tree and preserve unrelated changes.
2. Locate or install the required tools using the repository's existing setup
   guidance. Record actual executable paths and versions. Build the GUI from
   current source rather than relying on leftover binaries.
3. Run focused debugger, Spectrum and symbol tests; run the Python LSP tests.
   Reproduce the existing Pasmo assembler gate with the real executable.
4. Confirm a ROM-backed Spectrum debugger can open and show its screen.

**Exit:** tool paths, ROM availability and baseline results are recorded;
missing assets and skipped checks are explicit.

### 2. Add one external build/run command

Use a small Python CLI under `tools/`, with no runtime dependency on the LSP.
Support separate `build`, `run` and `build-run` actions. The last action launches
only after that invocation's assembly and artifact validation succeed.

The checked-in example configuration records source path, include paths,
defines, output directory, origin, entry label/address and initial stack pointer.
Machine-local executable and ROM paths are supplied through explicit options
or documented environment settings, not committed absolute paths.

Produce these artifacts beneath the example's ignored build directory:

| Artifact | Purpose |
|---|---|
| `program.bin` | Headerless assembled bytes |
| `program.pasmo.sym` | Original Pasmo symbol output |
| `program.debug.sym` | Converted debugger JSON symbols |
| `program.build.json` | Build and launch identity |
| Assembler stdout/stderr | Inspectable diagnostics, also shown in the terminal |

The build record includes assembler path/version/options, source and include
hashes, configuration hash, binary hash/size, artifact paths, origin, resolved
entry, stack pointer and target machine. Track all inputs for the supported
example/include workflow; do not call a build reproducible if dependencies are
omitted. A separate `run` validates the record and artifact hashes and rejects
changed inputs with an instruction to rebuild.

Build into a temporary output location and publish the successful artifact set
only after validation. A failed assembly must neither launch nor advertise a
previous binary as the new result. Capture exit status and preserve diagnostics.
Invoke subprocesses with argument arrays so paths containing spaces work.

Resolve the entry label from Pasmo output. Convert valid address symbols to
ordinary debugger labels; preserve other constants in the original output and
report omissions rather than inventing function or variable classifications.
Treat malformed symbol output or an unresolved entry as a build failure.

The manifest origin is the loader contract; check the example's assembled
address relationships and visible result in acceptance. A headerless binary
does not independently prove the source's `org` value.

**Exit:** a terminal command builds the example with real Pasmo, gives usable
errors for invalid source, and produces a verified artifact set.

### 3. Add validated Spectrum program launch

Extend the debugger CLI with an explicit Spectrum program launch path,
including load origin, entry and stack pointer. Keep ROM-only, tape and generic
binary invocations compatible. Provide a clear start-running option; leave a
paused launch available for inspection. Final option spelling should follow
the existing CLI conventions and be documented with the implementation.

The launch sequence is:

1. Parse and validate all inputs before mutating the machine: exact ROM size,
   nonempty program, numeric ranges, RAM-only non-wrapping load, entry within
   the loaded image, and an initial writable stack reserve outside the image.
   Define the reserve size in the example configuration and validate it; this
   protects startup placement, not arbitrary later stack growth.
2. Set up the Spectrum ROM, ULA, screen, keyboard and protected ROM region.
3. Load program bytes through an explicit host-load operation, preserving
   necessary display updates without reporting them as executed-code writes.
4. Read back and compare the complete loaded range.
5. Establish deterministic CPU/session state, set PC and SP, import symbols,
   and clear load-related runtime evidence. The program begins with interrupts
   disabled and cannot receive an interrupt before establishing that state.
6. Run through the existing Spectrum debugger execution path, or remain paused
   at entry if requested. Any failure exits nonzero without running the program.

Put validation and load preparation in code that can be tested headlessly;
the GUI should not own an independent copy of those rules. Do not undertake a
broad runtime rewrite unless a demonstrated failure blocks this milestone.

**Exit:** the CLI launches the actual assembled bytes in a Spectrum machine,
with correct initial state and matching symbols. Invalid loads fail explicitly.

### 4. Supply the VS Code example project

Add `examples/spectrum-dev/` with `main.asm`, the project configuration, a short
README, and tracked example-local `.vscode/settings.json` and `tasks.json`.
Keep generated files ignored. Opening this folder is the supported first path.

- Associate `.asm` and `.inc` with Z80 within this example only.
- Select the Pasmo dialect and document server/assembler/debugger/ROM setup.
- Provide **Spectrum: Build** and **Spectrum: Build and Run** tasks invoking
  the CLI from increment 2. Keep compiler diagnostics visible; provide clickable
  source locations if Pasmo's observed output supports a reliable matcher.
- Retain normal language diagnostics, completion and label navigation.
- Explain how to close the debugger, edit the color, and run again.

Use the existing extension's packaging path. Do not add build orchestration to
the language server or require VS Code's Debug Adapter Protocol for this loop.

**Exit:** the installed extension and checked-in task operate on the supplied
example without hand-editing emulator state or manually copying binary bytes.

### 5. Prove and document the complete loop

Run the acceptance scenario below and record exact commands/tool versions,
build hashes, test outcomes and visible observations. Update user documentation
with the now-working commands. Update this plan and the handover to distinguish
delivered behavior from deferred work.

## Acceptance scenario

1. Open `examples/spectrum-dev/` in ordinary VS Code with the installed extension.
   Verify completion and definition lookup in `main.asm`.
2. Invoke **Spectrum: Build and Run**. Confirm successful assembly artifacts and
   the Spectrum debugger opening with the expected screen and border colors.
3. Pause execution. Verify PC is in the example program and an imported label
   appears in disassembly. Step and resume to demonstrate usable execution
   control without claiming source-line debugging.
4. Close the debugger, change the color constant, and repeat. Confirm the binary
   hash changes and the new visible color matches the edit.
5. Introduce an assembler error. Confirm a failed task, useful diagnostics and
   no newly launched debugger or execution of an older binary. Fix and rerun.
6. Run the equivalent command from a terminal and obtain the same program result.

Targeted automated checks additionally cover address overflow, ROM overlap,
invalid entry/stack placement, missing ROM/tool, malformed or missing symbols,
tampered/stale artifacts, and paths containing spaces. Check failed deployment
leaves the machine unmodified, and successful loading preserves exact bytes,
ROM protection and requested entry state. Retest existing launch modes affected
by the CLI/loader changes.

Run the relevant full CTest suite and LSP suite once integration is complete;
report skips separately. Exercise the GUI and installed VS Code boundary in
addition to automated checks. Completion requires all six user steps, not just
a successful build or a test percentage.

## Limits and next stage

Known HALT/time and long-prefix stepping concerns remain visible in the
[architecture review](source-architecture-feedback.md). The first example
avoids those cases and makes no timing-fidelity claim. If they block the
specified loop, reduce the failure to a focused test and fix the responsible
boundary; do not conceal it by claiming broader compatibility.

Deferred: deployment into an existing instance, BASIC/ROM-dependent startup,
tape packaging, physical Spectrum transfer, 128K banking, source-line debugging,
execution counts/trace history, observed control-flow reconstruction, mutable
code versions, durable annotations and full session persistence.

Once this loop is proven, the next plan should build runtime-informed
disassembly on the existing debugger, preserving the distinction between live
bytes, observed execution, static interpretation and user annotations.
