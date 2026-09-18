"""Real Pasmo output -> changed-size live replacement, state adjustment, restore."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--pasmo", required=True)
parser.add_argument("--runner", required=True)
parser.add_argument("--disassemble", required=True)
args = parser.parse_args()
pasmo = shutil.which(args.pasmo)
if not pasmo:
    print("SKIP: Pasmo unavailable")
    raise SystemExit(77)
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="live patch with spaces ") as directory:
    output = Path(directory)
    for version in ("before", "after"):
        subprocess.run([pasmo, "--bin", str(root / "examples/live-patching" / f"{version}.asm"),
                        str(output / f"{version}.bin"), str(output / f"{version}.sym")], check=True)
    # Prove the byte-preserving editing baseline with the actual source tool.
    source = subprocess.run([args.disassemble, "--org", "0x8000", str(output / "before.bin")],
                            check=True, capture_output=True).stdout
    (output / "reconstructed.asm").write_bytes(source)
    subprocess.run([pasmo, "--bin", str(output / "reconstructed.asm"), str(output / "rebuilt.bin")], check=True)
    assert (output / "rebuilt.bin").read_bytes() == (output / "before.bin").read_bytes()
    subprocess.run([args.runner, str(output / "before.bin"), str(output / "after.bin")], check=True)
