# Live replacement and explicit state adjustment

This example exercises the first live-patching increment. Automatic symbol-based
suggestions, editor attachment and durable checkpoint files are not implemented.

From the repository root, assemble both versions with the configured Pasmo:

```sh
mkdir -p build/live-patching
build/tools/pasmo-0.5.5/pasmo --bin examples/live-patching/before.asm build/live-patching/before.bin
build/tools/pasmo-0.5.5/pasmo --bin examples/live-patching/after.asm build/live-patching/after.bin
```

The local tool path above is the repository's exercised setup; use your Pasmo
executable if installed elsewhere. Labels unused by the assembler are deliberate
continuation anchors and can produce unused-symbol warnings.

With a user-supplied Spectrum ROM, launch and execute just the CALL:

```sh
build/z80_debugger build/live-patching/before.bin --spectrum roms/spec48.rom \
  --org 0x8000 --entry 0x8000 --sp 0xff00 --steps 1 --live-update
```

The paused PC should be `$8010`, SP `$FEFE`, and the little-endian word at SP
should be `$8003`. The CALL has already happened; this is live state to preserve.

In **Live code update**:

1. Enter `build/live-patching/after.bin` as the assembled binary, with load address
   `8000`. Relative paths use the debugger's working directory.
2. Select **Set PC**, value `8012`, to continue at the moved `draw` instruction.
3. Select **Set memory word**, address `FEFE`, value `8004`, to update the saved
   continuation. Leave SP unchanged. Optionally set HL to a scenario value.
4. Choose **Review update**. Inspect the byte differences and selected state changes.
   Review does not modify the machine. Editing fields/files afterward requires
   another review; Apply uses the captured candidate.
5. Choose **Save checkpoint and apply reviewed update**. The machine stays paused.
6. Use the main **Step** twice: NOP, then RET. PC should become `$8004`, SP `$FF00`.
7. Choose **Restore checkpoint** in the update panel. PC returns to `$8010`, SP
   `$FEFE`, and the saved word and old code return to their pre-update values.

Repeating while leaving the memory-word option unchecked retains `$8003` exactly;
there is no hidden relocation. That intentionally different experiment may execute
the wrong continuation in the new code. The saved checkpoint remains available.

For a state-only experiment, leave the binary path empty and select explicit PC,
HL or word edits. Other registers remain editable in the Registers panel while
paused. A later edit to CPU, memory or device state invalidates an existing review.

The entire selected binary range is replaced, including its initialized data.
Unrelated memory and vacated bytes outside that range are preserved. This panel
does not yet infer data ownership, move structures, merge self-modifications or
carry old symbols onto the new image. The checkpoint retains the old analysis;
save valuable annotations to disk before experimenting.

Only the latest checkpoint is retained, in this process. Save checkpoint or a
subsequent successful apply replaces it. Closing the debugger loses it. Restore
recovers machine state and debugger control definitions and starts a fresh
transient analysis epoch; it does not restore the old execution-history queue.

Headless reproduction:

```sh
ctest --test-dir build -R live_patch --output-on-failure
```

The external-Pasmo test verifies unchanged disassembly reassembly, then executes
the assembled before/after bytes through the same checkpoint/patch controller.
The core test also covers partial-frame Spectrum continuation, EI/HALT state,
stale/invalid reviews and injected apply/rollback failures. Native button-level
acceptance remains separately recorded in the development checklist.
