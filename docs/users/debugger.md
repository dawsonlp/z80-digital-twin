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
