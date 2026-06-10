# Fault Diagnosis

**Audience:** testers stabilizing emulator behavior.
**Purpose:** map observed failures to likely subsystems and next tests.
**Last reviewed:** 2026-06-09.

| Observation | Likely subsystem | First tool | Disconfirming test |
|---|---|---|---|
| CPU suite reports wrong CRC or flags | CPU instruction semantics, undocumented flags, `R`, MEMPTR/WZ | CPU suite runner once added; current focused unit tests | Run `instruction_timing_test`, `refresh_register_test`, `daa_test`, `rotate_flags_test` |
| Tape header loads, then PC pins in RAM | TZX parser, pulse generation, EAR timing, unsupported TZX flow block | `spectrum_probe --tape ... --load --window 1000` | `tape_test`; inspect parsed block/pulse counts |
| Border stripes stop during load | Loader is waiting for a signal that never arrives, or tape playback ended early | `spectrum_probe` window report | Check `RAMwr`; a real loader keeps writing RAM |
| Title screen appears but gameplay fails | Floating bus, contention, keyboard, interrupt timing, or game-specific protection | Compatibility manifest case once added | Run `floating_bus_test`; compare with non-floating-bus-dependent title |
| Multicolour or raster effects tear | Contended memory or beam timing | Future contention harness | Existing `raster_test` only proves per-scanline reconstruction, not contention |
| Audio tempo or pitch is wrong | CPU timing, beeper edge capture, resampling | Future audio capture harness | `instruction_timing_test`, `beeper_test` |
| ROM writes look like SMC | Could be blocked ROM writes, not committed self-modification | Debugger SMC panel / blocked-write log | Run with ROM write protection off only as a controlled experiment |
| `spectrum_probe` says `IDLE/spin (ROM)` | BASIC editor or ROM idle loop, often normal after load/input | End screen dump and PC range | Type/press expected keys and watch for new coverage |

Prefer a narrower test before a whole-game diagnosis. Whole-game behavior is a
good symptom source, but CPU suites and synthetic device tests give cleaner
fault localization.
