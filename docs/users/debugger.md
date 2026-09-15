# Debugger

**Audience:** users debugging binaries or running a Spectrum under inspection.
**Purpose:** explain the primary `z80_debugger` workflows.
**Last reviewed:** 2026-06-09.

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
Reset retains its existing cold-ROM-boot behavior; restart the program through
the build/run command.

## What It Shows

- Registers and flags.
- Disassembly with current-PC highlighting, labels, symbols, and breakpoints.
- Memory with coverage, self-modifying-code, blocked-write, and ROM indicators.
- I/O bus transactions.
- Self-modifying-code and blocked-ROM-write logs.
- In Spectrum mode: live screen, keyboard matrix, tape, and beeper path.

## Execution Controls

The debugger owns the execution loop. Step and Step Over advance complete Z80
instructions, including prefixed instructions. Spectrum free-run is driven in
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
- **Not retained**: execution was counted here, but its byte evidence has been evicted.
- **Partial capture**: an instruction completed but the retained capture cannot
  describe its whole span. It is not used as a reliable instruction anchor.

History retains the latest 8192 events and at most 256 bytes per instruction;
older-event loss is displayed. Machine preparation transitions are separate
rows, not invented instructions. Refused writes and same-value writes do not
invalidate byte evidence; a change followed by restoration does. Overlapping
observed starts remain visible (marked `*`). Reset clears the history. It is
currently in-memory only: closing the debugger does not save this evidence.
