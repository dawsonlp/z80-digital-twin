# Spectrum Viewer

**Audience:** users running the ZX Spectrum 48K machine.
**Purpose:** explain ROM/tape loading, keyboard mapping, sound, and screenshots.
**Last reviewed:** 2026-06-09.

## Run

```bash
./build/spectrum spec48.rom
./build/spectrum spec48.rom --tape jetpac.tzx
./build/spectrum spec48.rom --turbo
./build/spectrum spec48.rom --shot boot.ppm
```

If no ROM path is supplied, tools also check `Z80_SPEC48_ROM` and common local
filenames such as `spec48.rom`.

## Tape Loading

The viewer plays `.tap` and `.tzx` images as cassette signal. That means normal
loads take real Spectrum time unless `--turbo` is used.

Typical flow:

1. Start the viewer with a ROM.
2. Load a tape with `--tape` or `F3`.
3. Type `LOAD""` in the Spectrum.
4. Press `ENTER`.
5. Press `F5` to play the tape.

`F6` stops playback.

## Keyboard Mapping

- Letters, digits, `ENTER`, and `SPACE` map to the Spectrum matrix.
- Host `Shift` is CAPS SHIFT.
- Host `Ctrl` is SYMBOL SHIFT.
- Host `Backspace` is DELETE.

The ROM keyword behavior is authentic: at the BASIC `K` cursor, the `J` key
enters the `LOAD` keyword.

## Debugging A Running Spectrum

Use `z80_debugger --spectrum spec48.rom` when you need breakpoints, memory
inspection, coverage, self-modifying-code events, or I/O history while the
machine runs. See [Debugger](debugger.md).
