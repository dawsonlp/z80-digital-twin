#!/usr/bin/env python3
"""External Pasmo build and fresh Spectrum launch; Python 3.9+, stdlib only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


class BuildError(Exception):
    pass


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def integer(value):
    if isinstance(value, bool) or not isinstance(value, (str, int)):
        raise BuildError("addresses must be integer values or numeric strings")
    try:
        return int(value, 0) if isinstance(value, str) else int(value)
    except (TypeError, ValueError):
        raise BuildError(f"invalid integer: {value!r}") from None


def project(path):
    path = Path(path).resolve()
    cfg = json.loads(path.read_text())
    required = {"source", "origin", "entry", "stack", "stack_reserve"}
    allowed = required | {"target", "include_paths", "defines"}
    if not required <= cfg.keys() or cfg.keys() - allowed:
        raise BuildError(f"project requires {sorted(required)}; allowed fields: {sorted(allowed)}")
    if cfg.get("target", "spectrum48") != "spectrum48":
        raise BuildError("only spectrum48 is supported")
    root = path.parent
    includes = [(root / p).resolve() for p in cfg.get("include_paths", [])]
    for directory in includes:
        if not directory.is_dir():
            raise BuildError(f"include directory missing: {directory}")
    return path, cfg, root, includes


def executable(command):
    found = shutil.which(command)
    if not found:
        raise BuildError(f"executable not found: {command}")
    return str(Path(found).resolve())


def resolve_input(name, root, includes):
    # Pasmo 0.5.5 searches its working directory, then -I directories in order.
    for directory in [root, *includes]:
        candidate = directory / name
        if candidate.is_file():
            return candidate.resolve()
    raise BuildError(f"input not found: {name}")


def listing_inputs(stdout, stderr, root, includes):
    names = re.findall(r"^Loading file: (.*) in \d+$", stderr, re.M)
    names += re.findall(r"^\t\tINCBIN (.+)$", stdout, re.M)
    if not names:
        raise BuildError("Pasmo did not report its input files")
    return {name: {"path": str(resolve_input(name, root, includes)),
                   "sha256": sha(resolve_input(name, root, includes))}
            for name in sorted(set(names))}


def validate_launch(size, launch):
    origin, entry, stack, reserve = (launch[k] for k in
                                    ("origin", "entry", "stack", "stack_reserve"))
    if not 0x4000 <= origin <= 0xFFFF or not 1 <= size <= 0x10000 - origin:
        raise BuildError("program must fit entirely in Spectrum RAM ($4000..$FFFF)")
    if not origin <= entry < origin + size:
        raise BuildError("entry must be inside the loaded program")
    if not 0 <= stack <= 0xFFFF or reserve < 2 or stack - reserve < 0x4000:
        raise BuildError("initial stack reserve must fit in writable Spectrum RAM")
    if stack - reserve < origin + size and stack > origin:
        raise BuildError("initial stack reserve overlaps the program")


def symbols_from_listing(sym_path, listing, entry_name, origin, size):
    values = {}
    for line in sym_path.read_text().splitlines():
        match = re.fullmatch(r"(\S+)\s+EQU\s+([0-9A-F]+)H", line)
        if not match or match[1] in values:
            raise BuildError(f"invalid Pasmo symbol line: {line}")
        values[match[1]] = int(match[2], 16)
    labels = {name: int(address, 16) for address, name in
              re.findall(r"^([0-9A-F]{4}):\t\tlabel (\S+)$", listing, re.M)}
    if isinstance(entry_name, str) and entry_name in values:
        entry = values[entry_name]
    else:
        try:
            entry = integer(entry_name)
        except BuildError:
            raise BuildError(f"entry symbol not found: {entry_name}") from None
    imported, omitted, used = [], [], set()
    # The debugger stores one name per address. Prefer the chosen entry alias.
    for name in sorted(values, key=lambda n: (n != entry_name, n)):
        address = values[name]
        if (labels.get(name) != address or not origin <= address < origin + size
                or address in used):
            omitted.append(name)
            continue
        used.add(address)
        imported.append({"address": address, "name": name, "type": "LABEL",
                         "description": "Pasmo label; no inferred semantic type"})
    return entry, imported, omitted


def assemble(pasmo, options, source, dest, root):
    command = [pasmo, *options, source, str(dest / "program.bin"),
               str(dest / "program.pasmo.sym")]
    result = subprocess.run(command, cwd=root, capture_output=True, text=True)
    (dest / "assembler.stdout.txt").write_text(result.stdout)
    (dest / "assembler.stderr.txt").write_text(result.stderr)
    if result.returncode:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        raise BuildError(f"Pasmo exited {result.returncode}; diagnostics: {dest}")
    return result


def build(path, pasmo_command):
    path = Path(path).resolve()
    output = path.parent / "build"
    output.mkdir(exist_ok=True)
    # This marker prevents a separately requested run after any failed build.
    marker = output / "last-build.json"
    write_json(marker, {"success": False})
    config_hash = sha(path)
    path, cfg, root, includes = project(path)
    pasmo = executable(pasmo_command)
    assembler_hash = sha(pasmo)
    version = subprocess.run([pasmo], capture_output=True, text=True)
    banner = version.stdout + version.stderr
    if "Pasmo v. 0.5.5 " not in banner:
        raise BuildError("this workflow requires validated Pasmo 0.5.5")
    options = ["-v", "-d", "--bin"]
    for directory in includes:
        options += ["-I", str(directory)]
    for define in cfg.get("defines", []):
        if not isinstance(define, str):
            raise BuildError("defines must be Pasmo NAME=value strings")
        options += ["--equ", define]
    attempt = Path(tempfile.mkdtemp(prefix="attempt-", dir=output))
    # Pasmo owns dependency interpretation, including macro-expanded INCBIN.
    # Discover first, hash all observed inputs, then build and check stability.
    discovery = assemble(pasmo, options, cfg["source"], attempt, root)
    inputs = listing_inputs(discovery.stdout, discovery.stderr, root, includes)
    first_binary, first_symbols = sha(attempt / "program.bin"), sha(attempt / "program.pasmo.sym")
    actual = assemble(pasmo, options, cfg["source"], attempt, root)
    if (inputs != listing_inputs(actual.stdout, actual.stderr, root, includes)
            or config_hash != sha(path) or assembler_hash != sha(pasmo)
            or first_binary != sha(attempt / "program.bin")
            or first_symbols != sha(attempt / "program.pasmo.sym")):
        raise BuildError("build inputs or outputs changed during assembly; rebuild")
    binary = attempt / "program.bin"
    size = binary.stat().st_size
    origin = integer(cfg["origin"])
    emitted = re.findall(r"^Emiting raw binary from ([0-9A-F]+) to ([0-9A-F]+)$",
                         actual.stdout, re.M)
    if len(emitted) != 1 or tuple(int(n, 16) for n in emitted[0]) != (origin, origin + size - 1):
        raise BuildError("Pasmo's emitted range does not match the configured origin and binary size")
    entry, symbols, omitted = symbols_from_listing(
        attempt / "program.pasmo.sym", actual.stdout, cfg["entry"], origin, size)
    launch = {"origin": origin, "entry": entry, "stack": integer(cfg["stack"]),
              "stack_reserve": integer(cfg["stack_reserve"])}
    validate_launch(size, launch)
    write_json(attempt / "program.debug.sym", {"version": "1.0", "program": cfg["source"],
                                              "symbols": symbols})
    artifacts = {p.name: {"sha256": sha(p), "size": p.stat().st_size}
                 for p in attempt.iterdir() if p.is_file()}
    manifest = {"version": 1, "target": "spectrum48", "project": str(path),
                "config_sha256": config_hash, "inputs": inputs,
                "assembler": {"path": pasmo, "banner": banner, "sha256": assembler_hash,
                              "options": options},
                "launch": launch, "artifacts": artifacts, "omitted_symbols": omitted}
    write_json(attempt / "program.build.json", manifest)
    # Publish one pointer atomically; every artifact in the set is already final.
    manifest_path = attempt / "program.build.json"
    write_json(output / "last-build.tmp", {"success": True, "manifest": str(manifest_path),
                                          "sha256": sha(manifest_path)})
    (output / "last-build.tmp").replace(marker)
    print(actual.stderr, end="", file=sys.stderr)
    print(f"Built {size} bytes at ${origin:04X}; entry ${entry:04X}")
    print(f"Binary SHA-256: {sha(binary)}\nArtifacts: {attempt}")
    if omitted:
        print("Not imported (constants, outside image or aliases): " + ", ".join(omitted))
    return manifest_path


def verified_build(path):
    path, cfg, root, includes = project(path)
    marker = json.loads((root / "build/last-build.json").read_text())
    if not marker.get("success"):
        raise BuildError("last build failed or is incomplete; rebuild before running")
    manifest_path = Path(marker["manifest"])
    if sha(manifest_path) != marker["sha256"]:
        raise BuildError("build record changed; rebuild")
    record = json.loads(manifest_path.read_text())
    if record["project"] != str(path) or record["config_sha256"] != sha(path):
        raise BuildError("project configuration changed; rebuild")
    for name, expected in record["inputs"].items():
        actual = resolve_input(name, root, includes)
        if str(actual) != expected["path"] or sha(actual) != expected["sha256"]:
            raise BuildError(f"input changed: {name}; rebuild")
    for name, expected in record["artifacts"].items():
        if sha(manifest_path.parent / name) != expected["sha256"]:
            raise BuildError(f"artifact changed: {name}; rebuild")
    validate_launch(record["artifacts"]["program.bin"]["size"], record["launch"])
    return manifest_path, record


def run(path, debugger_command, rom, paused=False):
    manifest_path, record = verified_build(path)
    debugger = executable(debugger_command)
    if not rom or not Path(rom).is_file() or Path(rom).stat().st_size != 16384:
        raise BuildError("set Z80_SPEC48_ROM or --rom to an exact 16384-byte Spectrum ROM")
    directory = manifest_path.parent
    launch = record["launch"]
    command = [debugger, str(directory / "program.bin"), "--spectrum", str(Path(rom).resolve()),
               "--sym", str(directory / "program.debug.sym"), "--org", str(launch["origin"]),
               "--entry", str(launch["entry"]), "--sp", str(launch["stack"]),
               "--stack-reserve", str(launch["stack_reserve"])]
    if not paused:
        command.append("--start")
    print(f"Running build {record['artifacts']['program.bin']['sha256']}", flush=True)
    # Keep debugger layout/runtime files beside generated artifacts, not sources.
    return subprocess.run(command, cwd=directory.parent).returncode


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["build", "run", "build-run"])
    parser.add_argument("--project", type=Path, default=Path("spectrum-project.json"))
    parser.add_argument("--pasmo", default=os.environ.get("Z80_PASMO", "pasmo"))
    parser.add_argument("--debugger", default=os.environ.get("Z80_DEBUGGER", "z80_debugger"))
    parser.add_argument("--rom", default=os.environ.get("Z80_SPEC48_ROM"))
    parser.add_argument("--paused", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.action in ("build", "build-run"):
            build(args.project, args.pasmo)
        if args.action in ("run", "build-run"):
            return run(args.project, args.debugger, args.rom, args.paused)
        return 0
    except (BuildError, OSError, ValueError, KeyError, TypeError) as error:
        print(f"Spectrum: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
