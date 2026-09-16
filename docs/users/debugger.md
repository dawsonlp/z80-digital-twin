# Debugger

**Audience:** users debugging binaries or running a Spectrum under inspection.
**Purpose:** explain the primary `z80_debugger` workflows.
**Last reviewed:** 2026-09-15.

## Launch

```bash
./build/z80_debugger --demo gcd
./build/z80_debugger --demo smc
./build/z80_debugger program.bin --org 0x8000 --sym program.sym
./build/z80_debugger --spectrum spec48.rom
./build/z80_debugger --spectrum spec48.rom --tape game.tzx
```

## Standalone Spectrum programs

For source development, start with the
[Spectrum assembly example](../../examples/spectrum-dev/README.md). Its CLI
assembles with Pasmo and launches the verified binary with symbols.

A standalone Spectrum binary can also be launched directly:

```sh
./build/z80_debugger program.bin --spectrum spec48.rom --org 0x8000 \
  --entry 0x8000 --sp 0xff00 --stack-reserve 256 --sym program.debug.sym --start
```

Omit `--start` to pause at entry. `--entry` and `--sp` are required when combining
a binary and Spectrum ROM. This starts with interrupts disabled, without BASIC
initialization, and requires an exact 16 KB ROM and a valid RAM/stack placement.
`--sym` takes debugger JSON, not Pasmo's unconverted symbol output. Tape and
writable-ROM options are incompatible with this standalone launch mode.
The button labeled **Restart program** cold-boots the Spectrum ROM and clears
RAM and analysis; it does not reload this standalone binary. Restart the binary
through the build/run command.

## What It Shows

- Registers and flags.
- Disassembly with current-PC highlighting, labels, symbols, and breakpoints.
- Memory with coverage, self-modifying-code, blocked-write, and ROM indicators.
- I/O bus transactions.
- Self-modifying-code and blocked-ROM-write logs.
- In Spectrum mode: live screen, keyboard matrix, tape, and beeper path.

## Execution Controls

The debugger controls execution through the CPU's bounded whole-instruction API.
Step and Step Over advance Z80 instructions, including prefixed instructions. Spectrum free-run is driven in
frame-sized T-state budgets so breakpoints still work inside a frame.

## Related Docs

- Architecture: [../developers/architecture.md](../developers/architecture.md)
- Debugger design: [../developers/debugger-design.md](../developers/debugger-design.md)
- Headless diagnosis: [../testers/headless-instrumentation.md](../testers/headless-instrumentation.md)

### Instruction steps and Spectrum display timing

Use **Step** to execute one instruction, or `--steps N` to step at startup in
any machine mode. For example:

```sh
./build/z80_debugger --spectrum roms/spec48.rom --steps 1
```

The debugger pauses after those steps. `--run` retains its existing batching
behavior; when both options are supplied, `--steps` runs after `--run`.
Spectrum frame progress advances with consumed T-states on both paths. Pausing
retains the unfinished frame; the screen holds the latest completed picture.
A long prefix chain can stop with an explicit incomplete-instruction status;
stepping or running again continues it. Debugger control does not inject an
emulated interrupt. Existing HALT and hardware interrupt fidelity limitations
remain documented separately.

### Browse earlier addresses and earlier execution

The **Disassembly** pane has two views:

- **Memory addresses** browses the full address space, including addresses before
  PC. The current instruction is kept a few rows below the top when **Follow PC**
  is enabled. Scrolling manually switches following off; select **Follow PC** to
  return. **Go** accepts a hexadecimal address or an imported/user symbol; the
  arrow buttons retain address-navigation history.
- **Execution history** shows retained completed instructions in execution order,
  with original bytes, sequence number, next PC and consumed T-states. It can
  contain multiple versions of an instruction at the same address. Click an
  address to inspect its current memory. **Follow latest** controls whether new
  execution moves the history view. This does not rewind registers or memory.

Evidence labels apply to the exact instruction start:

- **Observed**: the CPU read these instruction bytes and none have changed since.
- **Modified**: a byte changed after it was read, including an operand or a byte
  overwritten by the instruction itself. Memory view decodes today's bytes;
  history continues to show the bytes actually read during the old execution.
- **Unobserved**: tentative disassembly, not proof that bytes are instructions or data.
- **Partial capture**: an instruction completed but the retained capture cannot
  describe its whole span. It is not used as a reliable instruction anchor.

Recent history retains the latest 8192 events and at most 256 bytes per
instruction; older-event loss is displayed. The separate address store keeps
one latest observation and accumulated execution count per start, independent
of queue eviction. Re-execution replaces that address observation; older versions
are available only while their events remain in recent history. Machine preparation transitions are separate
rows, not invented instructions. Refused writes and same-value writes do not
invalidate byte evidence; a change followed by restoration does. Overlapping
observed starts remain visible (marked `*`). Analysis is currently in memory
only: closing the debugger does not save this evidence. JSON symbol files save
labels and descriptions, not execution evidence or machine state.

### Clear analysis and restart

**Clear analysis** clears counts, byte activity, captured
evidence, coverage and diagnostic logs without changing registers, memory bytes
or ROM protection. Pause first to inspect an empty analysis: a running session
continues collecting evidence immediately after clearing. If an instruction is
incomplete, finish it before clearing.
**Restart program** clears analysis and resets execution; in Spectrum mode it
cold-boots ROM and clears RAM. The core has a CPU-only reset API that preserves
analysis, but there is no separate CPU-only reset control in this UI.

Use a fresh debugger process when switching analyzed programs. Generic binary
and GCD-demo loading in an existing session do not consistently clear prior
analysis; changed-byte invalidation is not a complete replacement-session boundary.

### Compact address status

Address rows show independent markers in fixed positions: `RO` (read-only),
`X` (executed instruction start), `SM` (self-modified), and `O` (current bytes
observed executing). A dash means that property is not established; `?` in the
observation position means incomplete capture, and a final `*` marks overlap.
Execution and self-modification persist when current observation becomes invalid.

Colors reinforce the letters: read-only is blue, execution/current observation
are green, and self-modification is amber. Hover the status for full descriptions,
activity counts and latest access information. Hover the status key above the
column headers for the legend. Bytes and Instruction columns remain resizable;
headers stay visible during scrolling and status positions remain aligned.

### Architectural vector labels

Fresh debugger symbol tables include `RST_00_RESET`, `RST_08`, `RST_10`,
`RST_18`, `RST_20`, `RST_28`, `RST_30`, `RST_38_IM1`, and `NMI_66`.
These are debugger naming conventions, not standard-mandated assembler symbols.
Hover a label for its architectural role; imported/user labels take precedence.
The vector addresses remain navigable disassembly boundaries even before execution.

0038h is both the RST 38h target and maskable interrupt entry in IM 1. NMI enters
at 0066h. IM 0 depends on the device-supplied instruction, and IM 2 uses a vector
table. Labels describe architecture, not observed execution, the currently
selected interrupt mode, or implemented NMI support.
Reference: [Zilog Z80 CPU User Manual](https://www.zilog.com/docs/z80/um0080.pdf).
