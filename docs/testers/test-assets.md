# Test Assets

**Audience:** testers running ROM, tape, and compatibility cases.
**Purpose:** define local-only asset conventions.
**Last reviewed:** 2026-06-09.

ROMs, commercial games, and game-derived goldens are not committed to this
repository.

## Environment Variables

- `Z80_SPEC48_ROM`: path to a ZX Spectrum 48K ROM.
- `Z80_COMPAT_ASSETS`: directory containing local compatibility test assets.
- `Z80_COMPAT_GOLDENS`: directory containing local screen/audio/reference
  goldens.
- `Z80_ROMS_REPO_URL`: optional Git URL used by
  `tools/download_roms.sh` to populate the ignored local `roms/` directory.
  Defaults to `https://github.com/spectrumforeveryone/zx-roms.git`.
- `Z80_ROMS_DIR`: optional ROM download destination. Defaults to `roms/`.

Suggested layout:

```text
$REPO_ROOT/
  roms/
    spec48.rom

$Z80_COMPAT_ASSETS/
  cpu/
    zexdoc.com
    zexall.com
    zexdoc.tap
    z80ccf.tap
    z80memptr.tap
  loaders/
    underwurlde.tzx
    arkanoid.tzx
  demos/
  games/
```

To populate local ROMs from an external repository:

```sh
tools/download_roms.sh
export Z80_SPEC48_ROM=roms/spec48.rom
```

The `roms/` directory is ignored by git. Do not commit ROM images.

## Skip Semantics

Tests that require external assets should report `SKIP` when the asset is
absent. Missing copyrighted files should not make a clean checkout fail.

## Artifact Output

Generated logs, screenshots, hashes, and audio captures should be written under
`build/`, for example `build/compat-artifacts/`.

## Adding A Case

When the compatibility manifest runner exists, each case should declare:

- asset path relative to `Z80_COMPAT_ASSETS`;
- required ROM;
- feature gates such as `floating_bus`, `contention`, or `tzx_flow_blocks`;
- run budget and input script;
- expected oracle: screen signature, milestone PC, no-freeze state, or parsed
  pass/fail text.
