# Current development handoff

**Reviewed:** 15 September 2026.
**Branch:** `feature/spectrum-dev-loop`.
**Source baseline:** `b94fa00`, the address-metadata implementation following
checkpoint `a3dd462`, accompanied by this documentation refresh. These additions are unreleased; CMake remains 1.0.3.

## Start here

1. Read [current status](docs/reference/status.md) and
   [architecture](docs/developers/architecture.md).
2. Use [the Spectrum example](examples/spectrum-dev/README.md) for the forward
   development loop and [the debugger guide](docs/users/debugger.md) for analysis.
3. Read [fresh verification](docs/testers/documentation-refresh-verification.md)
   before relying on older test or GUI evidence.
4. Continue [address-metadata acceptance](docs/developers/address-metadata-checklist.md)
   before promoting the broader ROM reverse loop to delivered capability.

## Current behavior

The external Pasmo CLI builds binary, symbols and build-identity artifacts and
launches a fresh debugger after validation. The language server remains independent.
Spectrum viewer, debugger and probe share instruction/frame scheduling. Captured
instruction bytes and latest address evidence survive recent-history eviction;
byte counters and RO/X/SM/O properties describe independent facts.

`Clear analysis` clears observations while preserving machine state and protection.
`ResetCpu()` is a core API, not a separate UI control. The UI's **Restart program**
still cold-boots Spectrum ROM and clears RAM/analysis; rerun the build/run command
to reload a standalone program. Analysis remains in memory only.

## Outstanding work

The address checklist retains open acceptance for complete access accounting,
overflow handling, first/latest completion summaries, resource/overhead measurements
and remaining native interactions. The next product loop is exact-ROM-bound
save/reopen of evidence and annotations, then annotated range export and byte
verification. HALT/interrupt fidelity, contention and SDK packaging are separate
known gaps. See [roadmap](docs/developers/roadmap.md).

## Source and fixture boundaries

Preserve user work in `examples/spectrum-dev/main.asm`; automated workflow tests
use `tests/fixtures/spectrum-dev/`. Generated workspaces, local virtual environments,
Pasmo executables, ROMs and tapes are local prerequisites, not repository artifacts.
Use explicit paths or documented environment variables; do not assume an older
handoff's temporary executable still exists.

The [previous handoff](docs/archive/handoff-pre-2026-09-15.md) retains detailed
forward-loop and ROM-first history. [handover.md](handover.md) is also historical;
its branch state, paths and verification apply only to its dated sections.
