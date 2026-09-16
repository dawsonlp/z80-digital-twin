"""Fresh-process deterministic reporting; requires no ROM or assembler."""
import json
import pathlib
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="z80-transfer-test-") as folder:
    source = pathlib.Path(folder) / "capture.json"
    capture = {
        "format": "z80-transfer-capture", "version": 1,
        "source": "synthetic-cli-test", "limitations": "No stack history; no claim of hardware fidelity",
        "samples": [{
            "id": "call-1", "kind": "instruction", "sequence": "1",
            "start": 32768, "next_pc": 32771, "cycles": "17", "read_count": "3",
            "complete_capture": True, "bytes": [196, 3, 128], "revisions": [],
            "before": {"flags": 0, "b": None, "hl": None, "ix": None, "iy": None}
        }]
    }
    source.write_text(json.dumps(capture))
    before = source.read_bytes()
    command = [sys.argv[1], "transfers", "--source", str(source)]
    first = subprocess.run(command, check=True, capture_output=True).stdout
    second = subprocess.run(command, check=True, capture_output=True).stdout
    assert first == second and source.read_bytes() == before
    report = json.loads(first)
    assert report["occurrences"][0]["taken"] is True
    assert report["destination_sets_closed"] is False
    capture["samples"][0]["before"]["flags"] = None
    source.write_text(json.dumps(capture))
    report = json.loads(subprocess.run(command, check=True, capture_output=True).stdout)
    assert report["occurrences"][0]["taken"] is None
    capture["samples"][0]["bytes"] = [256]
    source.write_text(json.dumps(capture))
    invalid = subprocess.run(command, capture_output=True)
    assert invalid.returncode != 0 and not invalid.stdout
print("PASS: read-only transfer reporting across fresh processes")
