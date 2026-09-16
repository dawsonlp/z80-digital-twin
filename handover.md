# Z80 Digital Twin handover (historical)

> Superseded as a continuation guide on 15 September 2026 by
> [handoff.md](handoff.md). Commit state, paths, next steps and test results below
> describe their original dates, not the current working tree.

## Current continuation — 14 September 2026

The next increment is now implemented on **`feature/spectrum-dev-loop`**, based
on `98cf7fb`. The changes are in the working tree; no commit or push has been
made for this increment. The older sections below describe the preceding
disassembly investigation and retain its historical context.

Start with [the Spectrum example](examples/spectrum-dev/README.md), the
[development plan](docs/developers/spectrum-development-loop-plan.md), and the
[verification record](docs/testers/spectrum-development-loop-verification.md).
VS Code editing/completion/navigation, real Pasmo build artifacts, validated
standalone Spectrum loading, symbols, visible red-to-green rebuilds and
failed-build blocking have been exercised. Final checks: 33 CTest passes,
2 external CPU-suite skips, 18 LSP test passes, plus CLI/GUI checks.

The local configured workspace is `build/spectrum-dev.code-workspace`.
Pasmo is available at `build/tools/pasmo-0.5.5/pasmo`, the local language-server
executable at `lsp-z80/.venv/bin/lsp-z80`, and the current GUI at
`build/z80_debugger`. These are generated local prerequisites, not committed
artifacts. The workspace's terminal environment supplies the tool and ROM
paths; recreate a task terminal if it reports a stale environment.

Runtime-informed disassembly is still the next product stage. No architectural
question blocked the first loop. Live reload, BASIC-dependent startup and
timing/HALT corrections remain deferred. Reset currently cold-boots the ROM;
restart a standalone program with the build/run task.

## Historical handover — 12 September 2026

Updated **12 September 2026**. This document captures the current development
direction, work completed in this conversation, and the shortest route back to
productive work. Source files and Git state were checked on this date; the test
results below are from the earlier implementation run, not a fresh test run.

## Start here

1. Run `git status --short` and inspect the existing diff before editing. HEAD
   is `928ef03` (`Ignore local ROM directory`); CMake project version is 1.0.3.
   Substantial work is **uncommitted**, including untracked source and tests.
2. Read [Disassembly Verification](docs/testers/disassembly-verification.md),
   then the decoder, source renderer and test harness listed below.
3. Restore an external Pasmo 0.5.5 executable and run the first two verification
   gates. Pasmo is currently absent from PATH. The earlier temporary build at
   `/private/tmp/pasmo-0.5.5/` and reports under
   `/private/tmp/z80-state-review-20260908/` are no longer available.
4. Continue with small failing fixtures and targeted corrections. Keep CPU
   execution unchanged within the current disassembly investigation.

## Product intent and user decisions

The project is becoming a workbench for **understanding existing Z80 binaries
and writing new Z80 assembly programs**. The intended eventual loop is:

**Edit → assemble → deploy → execute → observe → refine → save → export →
reassemble → verify.**

There are already a C++23 CPU, a Spectrum 48K machine, a headless debugger core,
an ImGui debugger, and an independent Python language server. A complete
source-to-running-emulator workflow does not yet exist.

Preserve these decisions:

- **Use execution as the reference for this investigation.** The user explicitly
  asked us to assume the CPU is correct for the purpose of analyzing display
  disagreements, citing successful complex Spectrum games. We changed display
  and export behavior, not CPU execution. This is an investigation boundary,
  not proof of every hardware opcode or timing behavior.
- **Pasmo is the first assembler/dialect.** The new verification harness is
  specifically pinned to 0.5.5. Source output uses lowercase and `$` hexadecimal.
- **Keep ordinary CLI boundaries.** Use an external assembler and inspectable
  files. The LSP remains independently executable and testable; source editing
  must not require a running emulator. Do not embed an assembler or introduce a
  build daemon merely to connect these pieces.
- Preserve distinct claims: assembler encodings, decoded text, actual execution,
  user annotations, and byte-exact reconstruction are not interchangeable facts.
- Preserve existing work. Do not reset the tree, stage everything indiscriminately,
  or treat untracked files as disposable generated output.

## The three verification contracts

Fix the assembler version/options, origin and supported input domain. Let `A`
assemble source and `D` disassemble bytes into source.

| Check | Meaning | Status |
|---|---|---|
| Independent assembler correctness | `A(s)` equals explicitly specified expected bytes | Implemented, sampled corpus |
| Disassembly is a section of assembly | `A(D(b)) = b`, including exact length | Implemented, opcode sweeps and edge cases |
| Retraction on canonical sources | For `C = image(D)`, `D(A(c)) = c` for `c ∈ C` | Deliberately deferred |

The user's sequencing was: **get the first two correct first, then worry about
the third**. Do not claim the third gate exists yet.

For original source, `A(D(A(s))) = A(s)` preserves its assembled bytes. It does
not reconstruct the original comments, labels, formatting or macro structure.
If `D` is deterministic and check 2 holds in a fixed context, check 3 follows
mathematically. A separate practical stability check can still catch changes in
rendering or context. A full `D(A(s)) = s` requirement over arbitrary source is
impossible because assembly discards information.

## What we implemented

### Decoder and display corrections

The CPU has dispatch tables; the disassembler independently decodes opcode bit
patterns. The disagreement was present when the disassembler was introduced in
June 2026, rather than arising from a recent CPU modification. The CPU's `ED 76`
mapping dates to the initial June 2025 commit.

Current corrections:

- `ED 76` displays `SLL (HL)`, following the current CPU handler; `ED 7E`
  displays `NOP`, following `ED_NOP`. Previously they displayed `IM 1`/`IM 2`.
- Indexed CB rotate/shift instructions display the additional register
  destination where present, e.g. `DD CB 01 00` → `RLC (IX+0x01), B`.
- Indexed CB BIT/RES/SET forms with register codes other than 6 display the
  CPU's current register-only behavior. Do not assume these use the same
  memory-plus-register path as indexed rotate/shift instructions.
- Relative branch targets account for preceding prefixes. At `$8000`,
  `DD 18 00` now targets `$8003`, not `$8002`.
- Repeated prefixes no longer cause the reported length to clamp at four bytes.
  `Instruction.length` is now `uint32_t`; `Instruction.bytes` is still only a
  **four-byte preview**. Export uses the complete input range.
- `Decode` accepts an available-byte bound and reports incomplete input through
  `Instruction.complete`. Truncated instructions and a whole memory image of
  prefixes terminate without reading beyond the supplied range.
- Coverage iteration and the UI byte-preview loop were adjusted for the length
  type. The CPU stepping API and debugger prefix-step guard were not changed.

These display choices describe this executor. They do not redefine Pasmo's
instruction encodings. Export retains exceptional bytes rather than assembling
a mnemonic that would produce a different encoding.

### Source export and tests

`z80_disassemble --org 0x8000 program.bin` writes Pasmo source to stdout.
It is linear reconstruction of a binary range, not automatic code/data analysis,
source-symbol recovery or full project export.

Ordinary instructions emit mnemonics. Exceptional encodings emit `defb` with a
reason and, where available, a display comment. Exceptions include alternate
ED encodings, redundant/ignored prefixes, unsupported Pasmo spellings, some
wrapping relative branches, and incomplete input.

The independent assembler corpus contains seven fixtures, 127 instruction
examples and 270 expected bytes, grounded in Zilog's documented encodings.
Expected bytes must never be regenerated from our decoder or the assembler
under test just to obtain a pass.

The section gate covers 32 cases / 100,737 bytes: opcode-family sweeps,
displacement extremes, exceptional encodings, prefixes, truncated input and
seeded random data. Golden canonical instructions must emit mnemonics, so an
implementation that turns everything into `defb` cannot pass the gate.

### Supported range and observed assembler limits

The first export domain is **1–65,535 bytes**, contiguous and non-wrapping,
with explicit origin and `origin + size <= 65536`.

The earlier tests observed that Pasmo 0.5.5 emits two bytes for an `org`-only
source and an empty binary for an exactly 65,536-byte image. Both sizes are
explicitly rejected by export. Some relative branches across `$0000` also need
raw bytes because Pasmo rejects their source spelling. These are recorded
limits; do not silently loosen the checks or silently truncate output.

## File map

| File / directory | Role |
|---|---|
| [disassembler.cpp](debugger/disasm/disassembler.cpp), [header](debugger/disasm/disassembler.h) | Instruction decoding, displayed text, branch targets, bounded lengths |
| [pasmo_source.cpp](debugger/disasm/pasmo_source.cpp), [header](debugger/disasm/pasmo_source.h) | `DisassemblePasmo`, source spelling and explicit byte-preservation exceptions |
| [tools/disassemble/main.cpp](tools/disassemble/main.cpp) | CLI input/origin validation and source output |
| [tests/disassembler_test.cpp](tests/disassembler_test.cpp) | Display, length, target and incomplete-input regressions |
| [tests/assembly_roundtrip.py](tests/assembly_roundtrip.py) | Real Pasmo subprocesses, independent and section gates, artifact reports |
| [documented.json](tests/fixtures/assembly/documented.json) | Explicit assembly source, expected bytes and reference metadata |
| [CMakeLists.txt](CMakeLists.txt) | CLI target and CTest integration, `Z80_PASMO_EXECUTABLE` |
| [debug_session.cpp](debugger/exec/debug_session.cpp) | Execution control, coverage, SMC and blocked-write observation |
| [symbol_table.h](debugger/symbols/symbol_table.h) | Existing typed symbols/descriptions and JSON `.sym` persistence |
| [lsp-z80/README.md](lsp-z80/README.md) | Independent Pasmo LSP; nested VS Code client has its own build/tests |

## Recreate verification

Requirements: CMake 3.20+, C++23 compiler; Python 3.9+ for the round-trip harness.
The independent LSP package requires Python 3.14+.

Obtain the official Pasmo 0.5.5 source archive and verify its SHA-256 before
building with `./configure` and `make -j` in the extracted directory. A local
build is sufficient; no system-wide installation is required. Source URL and
checksum are in [the verification guide](docs/testers/disassembly-verification.md#reproduce-the-locally-validated-tool-build).
CMake/tests do not automatically download or install Pasmo.

Replace the executable placeholder below with the actual absolute path:

```sh
cmake -S . -B build -DZ80_BUILD_UI=OFF -DCMAKE_BUILD_TYPE=Debug \
  -DZ80_PASMO_EXECUTABLE=/absolute/path/to/pasmo
cmake --build build -j
ctest --test-dir build -R 'disassembl' --output-on-failure
ctest --test-dir build --output-on-failure
```

CTest names are `disassembly_assembler`, `disassembly_section` and
`disassembler_test`. The first two require Pasmo: missing executable means
**skip 77**, and another version means failure pending explicit validation.
If Python is absent, those gates are not registered. Check registration and
skip output; a superficially green CTest summary is insufficient.

For actual ROM integration, set `Z80_SPEC48_ROM` to a valid local 48K ROM before
CTest. The ROM tests can print SKIP while returning success if the ROM is not
found. External CPU suites require `Z80_COMPAT_ASSETS`. Do not infer that those
suites ran from the overall pass percentage.

LSP checks, independently of CMake:

```sh
cd lsp-z80
PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=src python3 -m unittest discover -s tests -v
```

Manual byte round trip (origin must match the original assembly):

```sh
/absolute/path/to/pasmo --bin program.asm program.bin
./build/z80_disassemble --org 0x8000 program.bin > reconstructed.asm
/absolute/path/to/pasmo --bin reconstructed.asm reconstructed.bin
cmp program.bin reconstructed.bin
```

The harness retains input/source/output files, process stdout/stderr and JSON
reports under `build/disassembly-artifacts/{assembler,section}/`. Reports record
tool/fixture hashes, case sizes, outcomes and byte-directive counts. Failure
messages locate the first byte mismatch and report both lengths.

### Previous evidence, not current revalidation

- Fresh headless Debug build succeeded; **31 C++/integration tests passed**,
  including both new gates and ROM-backed Spectrum tests. Two external CPU
  suites, ZEXDOC/ZEXALL, skipped because assets were not configured.
- Independent assembler gate: 7 fixtures / 127 instruction examples passed.
- Section gate: 32 cases passed. Missing-tool skip and wrong-version failure
  paths were also exercised.
- Earlier project assessment: 18 Python LSP tests passed. GUI and live VS Code
  integration were not exercised in that assessment or the disassembly run.
- The temporary executable/reports used then are now missing. Recreate them;
  do not cite this historical evidence as a fresh run on subsequent changes.

## What needs doing next

### Immediate continuation: establish and strengthen disassembly

1. **Reproduce the baseline** using the commands above. Preserve generated
   artifacts and distinguish pass, skip and failure. Resolve failures before
   broadening the product scope.
2. **Review and expand the first two contracts.** Reduce any new failure to a
   useful fixture and fix the responsible layer: encoding oracle, displayed
   operands/targets, instruction boundaries, Pasmo spelling or byte preservation.
   The existing independent corpus is sampled, not exhaustive. Add independently
   justified encodings and operand-boundary cases; require a reason for raw-byte
   fallback rather than masking an ordinary decoding defect.
3. **Check display/execution alignment separately where needed.** Byte agreement
   does not establish that a displayed instruction explains CPU behavior. Keep
   current execution as the reference within this scope; any hardware-conformance
   proposal belongs in an explicitly separate investigation.
4. **Then add canonical stability** to the harness, retaining first and second
   source outputs and comparing them under fixed context. Keep a separate named
   outcome for this third gate. Do not require recovery of original source text.
5. **Address full-memory export explicitly if needed.** Reproduce the selected
   assembler's size limits before choosing a validated assembler change or a
   defined segmented format. This is not solved by the present CLI.

The handover does not authorize silently combining all of these with a major
runtime rewrite. The latest focused development priority is disassembly and
its verification; choose a bounded next change.

### Subsequent workbench milestones

The broader [enhanced roadmap](docs/developers/enhanced-roadmap.md) remains a
requirements draft, not a list of delivered features. Useful next increments
after the disassembly baseline are:

- External build orchestration with identifiable binary/symbol artifacts and
  recorded assembler/version/configuration/source hashes. A raw `.bin` is not
  a relocatable object file. The proposed build manifest is not implemented.
- A symbol adapter: Pasmo's symbol output and the debugger's JSON `.sym` format
  are separate formats despite the shared extension.
- Validated deployment into an existing emulator instance: bounds, protection,
  overlap, build identity, stale evidence and explicit entry/register state.
  `LoadProgram` is only a byte-copy primitive; it can stop at the address boundary.
- Durable binary captures, labels, annotations and session recovery. Existing
  symbol save/load is not a full project/session model.
- Execution counts, observed control flow, frame-loop evidence and temporal
  mutable-code analysis. Current coverage records flags, not frequencies; SMC
  tracks writes to previously executed bytes, not all write-then-execute cases.

The [architecture review](docs/developers/source-architecture-feedback.md) also
records unresolved duplicated Spectrum runtime/frame drivers, HALT/time and
whole-instruction stepping concerns, and public policy/install limitations.
Those matter before depending on detailed timing evidence or live deployment,
but were deliberately not repaired during the display investigation. Its old
executor/display observations must be read alongside the corrections above.

## Working-tree ownership and documentation precedence

The disassembly work changed `CMakeLists.txt`, the decoder/header, coverage/UI
length loops, disassembler tests, README/docs indexes and testing documentation.
It added the Pasmo renderer, CLI, Python harness, JSON fixtures and verification
guide. These changes are still unstaged/uncommitted at this handover.

Pre-existing work also includes the untracked `lsp-z80/` import, enhanced roadmap,
architecture review, register-aliasing test, ROM-download script, `.idea/`, and
changes to `.gitignore`, CMake, README/docs, roadmap and test-asset documentation.
Some tracked files therefore contain contributions from both efforts. Inspect
hunks and untracked contents before staging; do not discard or scoop up unrelated
changes. No commit or push was made in this work.

Use current source plus [Disassembly Verification](docs/testers/disassembly-verification.md)
for this feature's implemented contract. Older status/roadmap documents may say
source export is entirely future work, or retain pre-fix display observations.
This small linear export exists; the larger annotated reconstruction and live
development workflow still does not.
