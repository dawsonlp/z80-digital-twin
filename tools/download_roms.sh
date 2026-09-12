#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Download local ZX Spectrum ROM images from an external Git repository.

Usage:
  tools/download_roms.sh [roms-git-url] [git-ref]

Environment:
  Z80_ROMS_REPO_URL   Git URL to use when no positional URL is supplied.
                      Defaults to spectrumforeveryone/zx-roms.
  Z80_ROMS_REPO_REF   Branch, tag, or commit-ish to check out.
  Z80_ROMS_DIR        Destination directory. Defaults to ./roms.
  FORCE=1             Overwrite existing destination files.

Examples:
  tools/download_roms.sh
  tools/download_roms.sh https://github.com/spectrumforeveryone/zx-roms.git
  Z80_ROMS_REPO_REF=main tools/download_roms.sh

ROMs are local test assets and are intentionally ignored by git.
USAGE
}

default_repo_url="https://github.com/spectrumforeveryone/zx-roms.git"
repo_url="${1:-${Z80_ROMS_REPO_URL:-$default_repo_url}}"
repo_ref="${2:-${Z80_ROMS_REPO_REF:-}}"
dest_dir="${Z80_ROMS_DIR:-roms}"

if [[ "$repo_url" == "-h" || "$repo_url" == "--help" ]]; then
  usage
  exit 0
fi

rom_names=(
  Interface1-v1.rom
  Interface1-v2.rom
  plus2a.rom
  plus2aesp.rom
  plus2esp.rom
  plus2fra.rom
  plus2uk.rom
  plus3.rom
  plus3espv40.rom
  plus3espv41.rom
  spec128esp-v1.rom
  spec128esp-v2.rom
  spec128uk.rom
  spec128uk_4.02.rom
  spec128uk_derby1.4.rom
  spec48-arabic-v1.rom
  spec48-arabic-v2.rom
  spec48-arabic-v31.rom
  spec48-beckman.rom
  spec48-prototype.rom
  spec48.rom
  zx80.rom
  zx81-550-kludge.rom
  zx81-550-original.rom
  zx81-622-improved.rom
  zx81-649-fixed.rom
)

tmp_dir="$(mktemp -d)"
cleanup() {
  rm -rf "$tmp_dir"
}
trap cleanup EXIT

clone_dir="$tmp_dir/roms-repo"

echo "Cloning ROM source repo..."
if [[ -n "$repo_ref" ]]; then
  git clone --depth 1 --branch "$repo_ref" "$repo_url" "$clone_dir"
else
  git clone --depth 1 "$repo_url" "$clone_dir"
fi

mkdir -p "$dest_dir"

copied=0
skipped=0
missing=0

for rom_name in "${rom_names[@]}"; do
  src_path="$(find "$clone_dir" -type f -name "$rom_name" -print -quit)"
  dest_path="$dest_dir/$rom_name"

  if [[ -z "$src_path" ]]; then
    echo "missing: $rom_name" >&2
    missing=$((missing + 1))
    continue
  fi

  if [[ -e "$dest_path" && "${FORCE:-0}" != "1" ]]; then
    echo "exists:  $dest_path"
    skipped=$((skipped + 1))
    continue
  fi

  cp "$src_path" "$dest_path"
  echo "copied:  $dest_path"
  copied=$((copied + 1))
done

echo
echo "ROM download complete: copied=$copied skipped=$skipped missing=$missing"

if [[ -f "$dest_dir/spec48.rom" ]]; then
  echo "48K ROM ready: $dest_dir/spec48.rom"
  echo "Optional: export Z80_SPEC48_ROM=$dest_dir/spec48.rom"
fi

if [[ "$missing" -ne 0 ]]; then
  echo "Some expected ROM filenames were not found in the source repo." >&2
  exit 1
fi
