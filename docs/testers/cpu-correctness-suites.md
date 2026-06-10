# CPU Correctness Suites

**Audience:** testers and developers stabilizing CPU behavior.
**Purpose:** define how to run freely available Z80 correctness suites in a
repeatable headless harness.
**Last reviewed:** 2026-06-10.

## Position

Yes: CPU correctness suites should be the first external compatibility harness.
They isolate CPU behavior from Spectrum ULA timing, tape loaders, games, audio,
and contention. A failing CPU suite gives a narrower fault surface than a game
that merely hangs.

The harness should support two execution adapters:

- **CP/M-style adapter:** for original exercisers such as ZEXDOC/ZEXALL that are
  commonly distributed as CP/M `.COM` programs or source. Run them on the bare
  CPU with a tiny BDOS console shim.
- **Spectrum-machine adapter:** for Spectrum-native tests such as `.tap`,
  `.sna`, `.z80`, or raw binaries that expect the 48K ROM, screen, or keyboard.
  Run them through `SpectrumMachine + DebugSession`.

Do not commit third-party binaries until their license and redistribution terms
are verified. The harness must skip cleanly when assets are absent.

## Target Suites

| Suite | First adapter | Why it matters | Initial oracle |
|---|---|---|---|
| `ZEXDOC` | CP/M-style preferred; Spectrum port acceptable | Documented instruction results and flags | Console output contains all expected `OK`/checksum-pass lines and no failure text |
| `ZEXALL` | CP/M-style preferred; Spectrum port acceptable | ZEXDOC plus undocumented flag bits 3 and 5 | Same as ZEXDOC, with longer timeout |
| `z80doc` / `z80full` | Asset-dependent | Faster full-suite signal; often convenient in emulator workflows | Suite-specific pass text or checksum |
| `z80flags` | Asset-dependent | Flag edge cases | Suite-specific pass/fail text |
| `z80ccf` | Spectrum-machine or raw adapter, depending on asset | Undocumented `SCF`/`CCF` X/Y flag behavior | Pass text, plus recorded CPU-revision assumption |
| `z80memptr` | Spectrum-machine or raw adapter, depending on asset | MEMPTR/WZ leakage into flags | Pass text or focused fail line |

The first acceptance set is:

```text
ZEXDOC + z80ccf + z80memptr
```

ZEXALL should follow, but it is slower and should not be the first harness
milestone.

## Harness Shape

Implemented executable:

```text
cpu_suite_runner
```

It currently has built-in `zexdoc` and `zexall` cases for the CP/M `.COM`
adapter. `compat/cpu-suites.json` documents the intended local asset contract;
parsing arbitrary manifest cases is still future work.

The runner takes one case name and produces one normalized result:

```text
PASS | FAIL | TIMEOUT | FROZEN | SKIP | HARNESS_ERROR
```

Example commands:

```bash
./build/cpu_suite_runner --case zexdoc
./build/cpu_suite_runner --case zexdoc --assets "$Z80_COMPAT_ASSETS"
ctest --test-dir build -R cpu_suite
```

CTest should register one test per case. Missing assets should return skip, not
failure.

## Manifest

Store case metadata in a manifest rather than expanding hard-coded cases
indefinitely. Current file:

```text
compat/cpu-suites.json
```

Suggested fields:

```json
{
  "name": "zexdoc",
  "adapter": "cpm_com",
  "asset": "cpu/zexdoc.com",
  "source_url": "https://...",
  "license": "verify-before-vendoring",
  "sha256": "optional-but-recommended",
  "entry": "0x0100",
  "timeout_instructions": 200000000,
  "timeout_tstates": 0,
  "expected_output": ["Z80 instruction exerciser", "OK"],
  "reject_output": ["ERROR", "FAILED"],
  "artifacts": ["console_log"]
}
```

Adapter-specific fields are allowed. Keep the schema small until real cases
force it to grow. The current runner treats this file as documentation; adding a
small parser is the next step before adding many asset-dependent cases.

## CP/M-Style Adapter

This is the cleanest path for original ZEX-style exercisers because it avoids
Spectrum ROM, tape loading, screen decoding, keyboard timing, and ULA behavior.

Minimum behavior:

- Load `.COM` image at `0x0100`.
- Set `PC = 0x0100`.
- Set a reasonable `SP`, for example high RAM below the BDOS shim.
- Provide a tiny CP/M-compatible trap for console output.
- Stop on CP/M termination or explicit timeout.

BDOS calls to support first:

| Call | Register convention | Harness behavior |
|---|---|---|
| `CALL 0x0005`, `C=0x02` | `E = char` | append one byte to console log |
| `CALL 0x0005`, `C=0x09` | `DE = "$"`-terminated string | append string to console log |
| `CALL 0x0005`, `C=0x00` or warm boot path | suite-dependent | terminate case if observed |

Implementation options:

- Intercept `PC == 0x0005` before executing the call target, emulate BDOS, then
  return to the caller by popping the return address.
- Put a trap opcode or tiny stub at `0x0005` only if that fits the current CPU
  stepping model cleanly.

The first option is preferable because it keeps the test binary unchanged and
does not depend on unsupported opcodes.

Oracles:

- Stream console output into `build/compat-artifacts/cpu/<case>.log`.
- Fail fast on known failure tokens.
- Pass only when the expected final summary appears.
- Timeout with last PC, last output lines, instruction count, and T-state count.

## Source Extraction Path

ZEXDOC/ZEXALL are not just opaque binaries: they are Z80 Exerciser variants that
have historically been distributed with source as part of YAZE/YAZE-AG-derived
emulator packages. ZEXDOC checks documented behavior; ZEXALL extends that to
undocumented flag bits 3 and 5. The tests work by running instruction groups
over generated input states and comparing accumulated 32-bit checksums against
known-good values from real hardware.

That makes a native runner possible, but there are two distinct levels:

### Level 1: Source-Built Binary

Build the upstream assembly source into `.COM` and run it through the existing
CP/M adapter.

This is the low-risk path:

- preserves the upstream test algorithm exactly;
- keeps our harness small;
- lets us vendor source only if the license permits it;
- avoids reimplementing ZEX's test generator incorrectly.

Recommended first step:

```text
upstream source -> assembler -> zexdoc.com -> cpu_suite_runner
```

### Level 2: Extracted Native Test Tables

Parse the upstream source into structured data:

- instruction group name;
- opcode bytes or opcode-generation pattern;
- register/memory input-state generator;
- expected checksum;
- documented-vs-undocumented flag mask;
- setup/teardown constraints.

Then run each group directly from C++ without CP/M or BDOS.

This would give better diagnostics because a failure could report the exact
instruction group and checksum before the whole exerciser finishes. But it is
also higher risk: the native extractor must reproduce ZEX's state generation and
CRC/checksum algorithm exactly. Any mismatch in the extractor becomes a false
CPU bug.

### Recommended Decision

Do both, in this order:

1. **Use source-built `.COM` as the authoritative oracle.** This gives immediate
   external coverage and validates the CPU against the original test.
2. **Only then extract native tables**, and require the native runner to agree
   with the source-built `.COM` on the same CPU before trusting its failures.

The source-built `.COM` remains the calibration oracle for the extracted runner.
If they disagree, assume the extractor is wrong until proven otherwise.

### Native Runner Shape

If we implement extraction, add a separate executable or mode:

```bash
./build/cpu_suite_runner --case zexdoc --mode extracted
```

Artifacts should include:

- group name;
- expected checksum;
- observed checksum;
- first divergent seed/input if available;
- generated opcode bytes;
- final register/memory state for the failing iteration.

The value of this mode is diagnosis, not replacing the upstream exerciser as the
first source of truth.

## Spectrum-Machine Adapter

Use this for suites distributed as Spectrum programs or tests that intentionally
exercise Spectrum-visible behavior.

Minimum behavior:

- Require `Z80_SPEC48_ROM`.
- Load asset from `Z80_COMPAT_ASSETS`.
- Support `.tap` first because current tape support already exists.
- Later support `.sna`/`.z80` snapshots if needed by the suites.
- Drive the machine with `DebugSession` using the same frame primitive as
  `spectrum_probe`.
- Capture screen text or normalized screen signatures.

Oracles, in order of preference:

1. Suite exposes text in screen memory that can be decoded reliably.
2. Suite reaches a known terminal PC/state.
3. Final screen rectangle hash matches a local golden.
4. Last-resort ASCII screen signature.

Avoid image OCR unless there is no better signal.

## Asset Layout

Use the conventions from [Test Assets](test-assets.md):

```text
$Z80_COMPAT_ASSETS/
  cpu/
    zexdoc.com
    zexall.com
    zexdoc.tap
    z80ccf.tap
    z80memptr.tap
```

The manifest should record source URL, license note, and optional SHA-256 for
each file. If redistribution is clearly permitted, we can consider vendoring
source or generated fixtures later; do not assume that from "available online."

## Diagnostics

Every failing run should write:

- console output or final screen dump;
- case metadata and asset hash;
- final PC/SP/AF/BC/DE/HL/IX/IY/I/R/IFF/IM;
- instruction count and T-state count;
- last N executed PCs or a hot-page summary;
- timeout/freeze reason.

For CPU-suite failures, the useful result is not just "failed"; it is the first
failing group/checksum line and enough register state to reproduce it in a
focused unit test.

## CTest Integration

Add CTest names like:

```text
cpu_suite_zexdoc
cpu_suite_zexall
cpu_suite_z80ccf
cpu_suite_z80memptr
```

Expected behavior:

- Clean checkout with no assets: tests report skipped.
- Assets present and passing: tests pass.
- Assets present and suite reports failure: tests fail.
- Harness bug or malformed manifest: `HARNESS_ERROR`, fail.

If CTest skip reporting is awkward for a custom binary, use a distinct exit code
and configure CTest `SKIP_RETURN_CODE`.

## Implementation Order

1. Define `compat/cpu-suites.json` with placeholder cases and local asset paths. **Done.**
2. Implement the CP/M-style adapter and get `ZEXDOC` to produce a console log. **Done.**
3. Add robust output oracles for `ZEXDOC`. **Initial token oracle done.**
4. Register `cpu_suite_zexdoc` with CTest and skip-on-missing-assets behavior. **Done.**
5. Add `ZEXALL` with a larger timeout. **Done as a built-in case.**
6. Add a manifest parser before adding many more cases.
7. Add the Spectrum-machine adapter only when a chosen suite asset requires it.
8. Add `z80ccf` and `z80memptr`; if they fail, turn each failure into a focused
   in-repo unit test before changing CPU behavior.
9. Document exact asset sources, hashes, and assumptions.

## Non-Goals

- Do not depend on the GUI debugger.
- Do not use commercial games to diagnose CPU correctness before these suites
  pass.
- Do not commit unclear-license binaries.
- Do not treat absence of assets as success; it is a skip with a clear message.
