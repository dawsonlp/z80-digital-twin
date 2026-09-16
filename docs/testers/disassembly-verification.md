# Assembly and disassembly verification

The development loop has three checks. The first two are executable gates;
canonical-source stability is deferred. The original export implementation did
not change CPU execution; later instruction-control work is documented in
[architecture](../developers/architecture.md).

**Last reviewed:** 2026-09-15.

## Contracts

With fixed Pasmo version, options and origin, let `A` assemble source and `D`
produce source from a binary range.

1. **Assembler encoding correctness:** `A(source) == independently specified bytes`.
   Expected bytes in `tests/fixtures/assembly/documented.json` come from the
   documented encodings in [Zilog UM008011-0816](https://www.zilog.com/docs/z80/um0080.pdf),
   using the named instruction groups/descriptions recorded with each case.
   They are explicit fixture values, never generated from our decoder or the
   assembler under test. Seven fixtures exercise 8/16-bit loads, arithmetic,
   rotates/bits, control flow, I/O/control/block instructions, and labels.
   This is sampled encoding evidence, not exhaustive assembler certification or
   proof of CPU behavior, timing, undocumented instructions, or macro semantics.
2. **Disassembly is a section of assembly:** `A(D(binary)) == binary`, byte for
   byte, including output length. The harness uses the golden bytes and sweeps
   all opcode values in the base, CB, ED, DD, FD, DDCB and FDCB families. It also
   checks displacement extremes, aliases, redundant prefixes, wrapping branch
   targets, truncated instructions, long/all-prefix ranges, and seeded random
   binaries. Golden canonical instructions must emit mnemonics, not `defb`, so
   replacing all disassembly with byte directives cannot pass this gate.
3. **Restricted retraction / canonical stability (deferred):** let `C` be the
   sources produced by `D` on the supported binary domain. Check `D(A(c)) == c`
   for `c` in `C`, equivalently `D(A(D(b))) == D(b)`. Under deterministic `D`
   and a fixed context, this follows mathematically from check 2. A separate
   implementation check will help catch nondeterminism, changing settings and
   unstable rendering as symbol/annotation support is added. It does not require
   recovery of original comments, names, macros or formatting.

For original source `s`, checks 1 and 2 together give
`A(D(A(s))) == A(s)` on the verified cases. Round-trip agreement alone cannot
establish correctness: assembler and disassembler could share the same mistake.

## Supported domain and source output

`z80_disassemble` is a headless CLI backed by the debugger's decoder and a small
Pasmo source renderer. It accepts a contiguous, non-wrapping range of
**1..65535 bytes**, with explicit origin and `origin + size <= 65536`.
It writes lowercase source using `$` hexadecimal, an `org`, and instructions.
Relative branches normally use source-location expressions such as `jr $-2`.

This is linear reconstruction, not code/data discovery. It neither claims that
every decoded byte will execute nor recovers original source structure. Symbol
imports into this exporter, annotation persistence and complete reverse-engineering
projects remain separate work. External assembly, debugger-symbol conversion and
fresh-process deployment now exist in the [Spectrum workflow](../../examples/spectrum-dev/README.md).

Byte directives preserve exceptional encodings, with a reason and, where
complete, the decoder's display as a comment:

- alternate ED encodings that a mnemonic would normalize;
- redundant or ignored index prefixes;
- indexed CB forms without a selected byte-exact Pasmo spelling;
- flags-only input and zero-output forms rejected by the selected spelling;
- relative branches crossing the address boundary that Pasmo rejects;
- incomplete instructions at the end of the input.

The current CPU mappings remain the reference for the debugger display. In
particular, `ED 76` displays the CPU's `SLL (HL)` behavior, and `ED 7E` its NOP
behavior. Indexed CB bit operations with register codes other than 6 display
the current CPU's register-only behavior; rotate/shift forms show the memory
and register destinations. The exporter retains these exceptional encodings as
bytes. These display choices do **not** assert a hardware instruction encoding
or promise that assembling the displayed mnemonic reproduces those bytes.

The bounded decoder reports incomplete input explicitly and retains lengths
beyond four bytes. `Instruction.bytes` remains a four-byte display preview;
source export reads the complete original range. The changed length type is
also used by coverage and the UI. Current instruction control uses the CPU-owned
bounded stepping API, separately from the decoder/export limits.

### Observed Pasmo 0.5.5 limits

During development, a source containing only `org $8000` emitted two bytes;
a source emitting exactly 65536 bytes produced an empty raw output file.
The exporter rejects both sizes rather than claiming a round trip. The range
checks are explicit failures with no source on stdout. Supporting a complete
64 KB capture will require a validated assembler change or a separately defined
segmented-artifact contract, not silent truncation.

## Run

Use the external [Pasmo 0.5.5 release](https://pasmo.speccy.org/) in `--bin` mode.
Tests invoke the executable directly, without a shell or embedded assembler.
There are no downloads during CMake configuration or testing.

```sh
cmake -S . -B build -DZ80_BUILD_UI=OFF \
  -DZ80_PASMO_EXECUTABLE=/absolute/path/to/pasmo
cmake --build build -j
ctest --test-dir build -R 'disassembl' --output-on-failure
```

CTest names:

- `disassembly_assembler`: independent encoding samples;
- `disassembly_section`: byte-preserving source reconstruction;
- `disassembler_test`: decoded display, length, branch-target and bounded-input
  regressions, with no assembler dependency.

The external gates require Python 3.9+ and skip with exit code 77 if Pasmo is
absent. A present executable reporting another version fails pending explicit
validation. If Python is absent, CMake reports that the external gates are not
registered. Skips or missing registrations are not verification success.

Manual workflow:

```sh
pasmo --bin program.asm program.bin
./build/z80_disassemble --org 0x8000 program.bin > reconstructed.asm
pasmo --bin reconstructed.asm reconstructed.bin
cmp program.bin reconstructed.bin
```

Set the origin to the actual assembly origin. A raw binary does not carry it.

### Reproduce the locally validated tool build

Official source archive:
`https://pasmo.speccy.org/bin/pasmo-0.5.5.tar.gz`

SHA-256:
`c83ff23e06b26ab5de05efaf13d9ebaf485d43f9a3a1ed50bba17be5a87918ac`

Download and verify that archive, unpack it, then run `./configure` and
`make -j` in its directory. Point CMake at the resulting `pasmo` executable;
system-wide installation is unnecessary. The first local verification used
that source release on macOS; other platforms have not been established by
this run.

## Failure-driven iteration

Each gate writes to `build/disassembly-artifacts/<gate>/`: original inputs,
generated source, expected/actual binaries as applicable, stdout/stderr, and
`report.json`. Reports include the assembler path, banner, executable hash,
fixture hash, case byte counts and hashes; section reports also identify the
disassembler executable and count byte-directive lines. Byte mismatches report
the first differing offset and both lengths. Outputs from a previous assembly
are removed before invoking Pasmo, and a missing executable writes a SKIP report.

When a case fails, preserve the input, reduce it to the smallest useful fixture,
classify it as an assembler encoding, display, length, syntax or preservation
problem, and fix that layer. Never regenerate expected bytes from the failing
implementation merely to make the test pass. A byte-directive fallback needs an
explicit encoding/format reason; ordinary supported instructions should keep
readable mnemonics. Add the canonical-stability gate after these first two
contracts are established for the desired release domain.
