"""Independent encoding oracle and A(D(b)) == b, using an external Pasmo process."""

import argparse
import hashlib
import json
from pathlib import Path
import random
import shutil
import subprocess
import sys


def run(command):
    return subprocess.run(command, capture_output=True, timeout=20)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def assemble(pasmo, source, base):
    asm = base.with_suffix(".asm")
    binary = base.with_suffix(".bin")
    asm.write_text(source)
    # Never let a stale output make a failed invocation look successful.
    if binary.exists():
        binary.unlink()
    result = run([pasmo, "--bin", str(asm), str(binary)])
    base.with_suffix(".stdout").write_bytes(result.stdout)
    base.with_suffix(".stderr").write_bytes(result.stderr)
    if result.returncode or not binary.exists():
        raise AssertionError(f"Pasmo failed for {asm}: {result.stderr.decode(errors='replace')}")
    return binary.read_bytes()


def equal_bytes(actual, expected, name):
    if actual != expected:
        first = next((i for i, (a, b) in enumerate(zip(actual, expected)) if a != b),
                     min(len(actual), len(expected)))
        raise AssertionError(f"{name}: first mismatch at offset {first:#x}; "
                             f"actual length={len(actual)}, expected length={len(expected)}")


def binary_cases():
    # All opcode values in each family, plus operand/displacement boundaries.
    # These exercise preservation, not an independent correctness oracle.
    for family in ((), (0xCB,), (0xED,), (0xDD,), (0xFD,)):
        data = b"".join(bytes((*family, op, 0x80, 0x7F, 0, 0, 0, 0)) for op in range(256))
        yield "sweep_" + (bytes(family).hex() or "base"), 0x8000, data
    for prefix in (0xDD, 0xFD):
        data = bytes(v for d in (0, 127, 128, 255) for op in range(256)
                     for v in (prefix, 0xCB, d, op))
        yield f"sweep_{prefix:02x}cb", 0x8000, data
    yield "aliases_prefixes", 0x8000, bytes.fromhex(
        "ed76 ed7e ed4c ed55 ed66 ed63 3412 ed6b 7856 ed00 "
        "dd1800 dddd210090 fddd00 dded44 ddcb0100 fdcbff80")
    yield "relative_wrap_low", 0, bytes.fromhex("1880 10fe 387f")
    yield "relative_wrap_high", 0xFFFA, bytes.fromhex("187f 1080 28fe")
    for suffix in ("dd", "ed", "cb", "ddcb", "ddcb80", "21", "2134", "c3", "ed43ff"):
        data = bytes.fromhex(suffix)
        yield "truncated_" + suffix, 65536 - len(data), data
    yield "all_prefixes", 0, bytes([0xDD]) * 65535
    yield "long_instruction", 0, bytes([0xDD]) * 300 + bytes.fromhex("210090")
    rng = random.Random(0x280)  # fixed seed, reproducible bytes
    for i in range(4):
        yield f"random_{i}", i * 0x1000, rng.randbytes(4096)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gate", choices=("assembler", "section"), required=True)
    parser.add_argument("--pasmo", default="pasmo")
    parser.add_argument("--disassembler", required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    args = parser.parse_args()
    args.artifacts = args.artifacts.resolve()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    pasmo = shutil.which(args.pasmo)
    if not pasmo:
        (args.artifacts / "report.json").write_text(json.dumps(
            {"gate": args.gate, "status": "SKIP", "reason": "Pasmo absent"}, indent=2) + "\n")
        print("SKIP: Pasmo absent; configure Z80_PASMO_EXECUTABLE")
        return 77
    report = {"gate": args.gate, "pasmo": str(Path(pasmo).resolve()),
              "cases": [], "status": "FAIL"}
    try:
        report["pasmo_sha256"] = sha(Path(pasmo).read_bytes())
        banner = run([pasmo])
        version = (banner.stdout + banner.stderr).decode(errors="replace")
        report["version"] = version
        if "Pasmo v. 0.5.5 " not in version:
            raise AssertionError("This corpus is pinned to Pasmo 0.5.5; validate a new version explicitly")
        fixture = Path(__file__).parent / "fixtures/assembly/documented.json"
        corpus = json.loads(fixture.read_text())
        report["fixture_sha256"] = sha(fixture.read_bytes())
        if args.gate == "assembler":
            for case in corpus["cases"]:
                source = f"org ${case['origin']:04x}\n{case['source']}\n"
                actual = assemble(pasmo, source, args.artifacts / case["name"])
                expected = bytes.fromhex(case["hex"])
                (args.artifacts / case["name"]).with_suffix(".expected.bin").write_bytes(expected)
                equal_bytes(actual, expected, case["name"])
                report["cases"].append({"name": case["name"], "bytes": len(actual), "sha256": sha(actual)})
        else:
            report["disassembler_sha256"] = sha(Path(args.disassembler).read_bytes())
            cases = [("golden_" + c["name"], c["origin"], bytes.fromhex(c["hex"]))
                     for c in corpus["cases"]]
            cases.extend(binary_cases())
            for name, origin, data in cases:
                base = args.artifacts / name
                input_path = base.with_suffix(".input.bin")
                input_path.write_bytes(data)
                result = run([args.disassembler, "--org", hex(origin), str(input_path)])
                base.with_suffix(".decode.stderr").write_bytes(result.stderr)
                if result.returncode:
                    raise AssertionError(f"Disassembler failed for {name}: {result.stderr!r}")
                source = result.stdout.decode()
                if name.startswith("golden_") and "defb " in source:
                    raise AssertionError(f"{name}: documented canonical instructions must emit mnemonics")
                actual = assemble(pasmo, source, base)
                equal_bytes(actual, data, name)
                report["cases"].append({"name": name, "bytes": len(data), "sha256": sha(data),
                                        "byte_directive_lines": source.count("    defb ")})
            # Bounds errors must be explicit, not successful truncated output.
            bad = args.artifacts / "oversize.input.bin"
            bad.write_bytes(b"\x00\x00")
            for origin in ("0xffff", "0x10000", "-1", "nonsense"):
                result = run([args.disassembler, "--org", origin, str(bad)])
                if result.returncode == 0 or result.stdout:
                    raise AssertionError(f"invalid input accepted: origin={origin}")
            for data in (b"", bytes(65536)):
                bad.write_bytes(data)
                result = run([args.disassembler, "--org", "0", str(bad)])
                if result.returncode == 0 or result.stdout:
                    raise AssertionError("binary accepted outside the selected Pasmo size domain")
        report["status"] = "PASS"
        print(f"PASS {args.gate}: {len(report['cases'])} cases; artifacts: {args.artifacts}")
        return 0
    except (AssertionError, OSError, subprocess.TimeoutExpired) as error:
        report["error"] = str(error)
        print(f"FAIL {args.gate}: {error}", file=sys.stderr)
        return 1
    finally:
        (args.artifacts / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    sys.exit(main())
