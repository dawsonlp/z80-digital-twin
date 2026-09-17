# ROM control-flow experiments — 16 September 2026

Sixteen bounded runs recorded 914 executed instructions from the local 48K ROM
and supplied RAM wrappers. All stop conditions were reached. A fresh replay
produced identical captures, reports, listings and manifest: 49 files per run.
This is experimental evidence for selected paths, not a complete ROM analysis.

ROM SHA-256:
`d55daa439b673b0e3f5897f99ac37ecb45f974d1862b4dadb85dec34af99cb42`.

## Setup and capture boundary

The harness boots the existing Spectrum machine for 200 frames, then copies its
memory into a fresh CPU-only instance for each experiment. It does **not** resume
the booted CPU or its device/time/interrupt state. Each case explicitly supplies
its entry, register/memory changes, instruction budget and stop condition. A RAM
CALL wrapper at `$8000`, initial SP `$FF00`, IY `$5C3A` and ROM write protection
are common. Inputs and alternate registers otherwise start from the fresh CPU's
initial state. These are controlled internal-entry experiments, not authentic
BASIC invocations. Routine names are orientation labels from the existing plan.

Captured instruction bytes and successor PCs come from the existing instruction
history. Data-access count changes identify CPU reads, committed writes and
refused writes. The harness accepts at most one read and one write per address
per instruction; it uses metadata event sequence to select the pre-write or
post-write byte for a read. Ambiguous repeated accesses abort capture. This covers
EX (SP),HL in these runs without claiming hardware bus timing. The harness changes
no production capture hooks, runtime identities, CPU implementation or analyzer.

Captures and reconstructed listings stay local because they contain ROM bytes.
Current outputs are `/private/tmp/z80-rom-sites`, with replay in
`/private/tmp/z80-rom-sites-replay`.

## What was actually observed

| Site/path | Controlled observation | Current automatic analysis |
| --- | --- | --- |
| `$1FF5 → $1FC3` | The ROM calls `$2530`, pops its caller continuation at `$1FC6`, then either RET Z at `$1FC7 → $8003` or JP (HL) at `$1FC8 → $1FF8`, selected by FLAGS bit 7. | Branches and ordinary stack matches recovered. Full register-return/caller-skip classification is lost at BIT `(IY+1)` in `$2530`. |
| BEEPER `$03F0/$03F4` | For initial H=1, DE=1 and L=0,1,2,3, both indirect jump sites reach `$03D4,$03D3,$03D2,$03D1`, respectively. | Destinations recorded correctly; target provenance remains partial. Earlier unsupported instructions clear the IX derivation. |
| Printing `$15F2` | Loads output pointer through boot-derived CURCHL, then CALL `$162C → $09F4`. | Pointer-read ancestry and a preserved caller continuation are recovered; initial memory provenance remains partial. |
| Input `$15E6` | Advances two bytes into the same channel record, joins printing at `$15F7`, then CALL `$162C → $10A8`. | Same preservation finding, distinct pointer reads and destination. Shared code does not imply identical use. |
| Channel selection `$1621`, C='K'/'S' | Executes ROM table search, falls through `$162B → $162C`, then jumps to `$1634` or `$1642`. Both paths join at `$1646` and jump from `$164A → $0D4D`. | Edges are recorded. AND/CP/SCF and indexed bit operations interrupt higher-level lineage. This path reaches `$162C` without the CALL at `$15FB`. |
| USR tail `$34B6` | ROM pushes `$2D2B`, pushes supplied BC=`$9000`, and RET at `$34BB` dispatches there. A supplied RAM RET then reaches `$2D2B`. | PUSH/RET dispatch and subsequent consumption of the prepared continuation are recognized. Argument conversion at `$34B3` was not exercised. |
| Calculator tail `$338E` | Offsets 0,2,4 read ROM words at `$32D7,$32D9,$32DB`, producing RET targets `$368F,$343C,$33A1`. | PUSH/RET dispatch and table-read ancestry recognized. Target origins remain partial because image-backed constants are not an input to these tactics. Handler bodies for offsets 0 and 2 were not executed. |
| Error tail `$0053` | Pops the wrapper continuation, reads supplied error byte 5, loads SP from boot-derived ERR_SP to `$FF50`, then jumps `$16C5`. | Explicit SP replacement recorded; indexed LD at `$0055` breaks earlier lineage. No saved-SP restoration is claimed. |
| CLEAR tail `$1EE0` | Pops supplied values, sets SP to `$9000`, pushes BC, stores new SP, exchanges DE/HL and jumps `$8003` with SP=`$8FFE`. | Changed SP and jump recorded. LD (HL),n at `$1EE2` breaks original continuation provenance; this is not reported as a normal restored-stack return. |

The BEEPER instruction sequence explains the tested mapping: it complements the
original L in A, masks its low two bits into C, and adds BC to base IX `$03D1`.
The shifted L is separately used by the loop. That explanation comes from
inspection of executed instructions; the current value tactic does not recover
the whole chain automatically. The four destinations join the same loop at
`$03D4`, rather than demonstrating four independent functions.

## One RET instruction, two consecutive uses

The calculator offset-4 run is especially useful. The ROM replaces the stack's
wrapper continuation with `$3365` using EX (SP),HL, then pushes table target
`$33A1`. The first execution of RET at `$33A1` therefore reaches itself:

1. `$33A1 → $33A1`, SP `$FEFC → $FEFE`: consumes the pushed dispatch target.
2. `$33A1 → $3365`, SP `$FEFE → $FF00`: consumes the replacement continuation.

Both executions have identical instruction bytes and address, but different
stack evidence. The site report retains two variants: `pushed_target_ret` and
`substituted_continuation_transfer`. The latter refers to the supplied wrapper's
slot; this experiment does not establish the full calculator calling convention.
It does demonstrate why a site cannot acquire one permanent “return” meaning.

## What these runs suggest next

The immediate bottleneck is instruction-effect coverage, not lack of a more
speculative function model. BIT/CP/SCF affect flags yet currently discard all
value lineage. The BEEPER's IX history is also lost across unrelated unsupported
operations. Indexed loads/stores and LD (HL),n break two stack-rebuilding paths.

The next useful implementation would model these concrete effects and invalidate
only dependencies they can affect, subject to captured memory/device boundaries.
The existing conservative stop is correct until those preconditions are modeled.
ROM-backed constant provenance would separately need explicit image binding;
merely embedding a ROM hash in the producer's source label is not that contract.
The shared paths and the dual-use RET also provide real inputs for later block
and overlapping-routine analysis. No production tactic was changed in this pass.

## Reproduce

From the repository root, with the documented ROM available locally:

```sh
cmake --build build --target z80_analyze -j 4
c++ -O2 -Wall -Wextra -Wpedantic -std=c++23 \
  -Idebugger/exec -Idebugger/disasm -Idebugger/symbols \
  -Idebugger/analysis -Imachine -Isrc \
  tools/experiments/rom_control_flow.cpp \
  build/libz80_debugger_core.a build/libz80_cpu.a \
  -o /private/tmp/rom_control_flow
/private/tmp/rom_control_flow spec48.rom /private/tmp/z80-rom-sites
/private/tmp/rom_control_flow spec48.rom /private/tmp/z80-rom-sites-replay
python3 tools/experiments/verify_rom_control_flow.py \
  /private/tmp/z80-rom-sites /private/tmp/z80-rom-sites-replay
```

The harness enforces the ROM identity and emits per-case capture JSON, analysis
JSON, executed assembly listing and a setup/stop manifest. The verifier checks
concrete paths and replay identity; it does not require current unsupported-op
limitations to remain, so future analysis improvements can use the same runs.
