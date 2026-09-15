"""Real Pasmo workflow tests. No GUI dependency; absent Pasmo skips explicitly."""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/fixtures/spectrum-dev"
spec = importlib.util.spec_from_file_location("spectrum_dev", ROOT / "tools/spectrum_dev.py")
dev = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dev)
PASMO = RUNNER = None


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="spectrum project with spaces ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ("main.asm", "palette.inc", "spectrum-project.json"):
            shutil.copy(FIXTURE / name, self.root / name)
        self.project = self.root / "spectrum-project.json"

    def build(self):
        return dev.build(self.project, PASMO)

    def config(self, **changes):
        cfg = json.loads(self.project.read_text())
        cfg.update(changes)
        dev.write_json(self.project, cfg)

    def test_edit_build_execute_two_colors(self):
        hashes = []
        for color in (2, 4):
            source = (FIXTURE / "main.asm").read_text()
            (self.root / "main.asm").write_text(source.replace("color equ red", f"color equ {color}"))
            manifest = self.build()
            _, record = dev.verified_build(self.project)
            hashes.append(record["artifacts"]["program.bin"]["sha256"])
            symbols = json.loads((manifest.parent / "program.debug.sym").read_text())
            self.assertEqual({s["name"] for s in symbols["symbols"]}, {"start", "idle"})
            result = subprocess.run([RUNNER, str(manifest.parent / "program.bin"), str(color)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotEqual(*hashes)

    def test_failed_build_blocks_old_binary(self):
        self.build()
        (self.root / "main.asm").write_text("this is invalid assembly\n")
        with self.assertRaises(dev.BuildError):
            self.build()
        with self.assertRaisesRegex(dev.BuildError, "last build failed"):
            dev.verified_build(self.project)

    def test_stale_include_config_and_tampered_artifact(self):
        self.build()
        (self.root / "palette.inc").write_text("red equ 6\ngreen equ 4\n")
        with self.assertRaisesRegex(dev.BuildError, "input changed"):
            dev.verified_build(self.project)
        manifest = self.build()
        binary = manifest.parent / "program.bin"
        binary.write_bytes(binary.read_bytes() + b"\x00")
        with self.assertRaisesRegex(dev.BuildError, "artifact changed"):
            dev.verified_build(self.project)
        self.build()
        self.config(stack="0xfe00")
        with self.assertRaisesRegex(dev.BuildError, "configuration changed"):
            dev.verified_build(self.project)

    def test_include_search_and_shadowing(self):
        (self.root / "includes").mkdir()
        (self.root / "palette.inc").rename(self.root / "includes/palette.inc")
        self.config(include_paths=["includes"])
        self.build()
        dev.verified_build(self.project)
        (self.root / "palette.inc").write_text("color equ 5\n")
        with self.assertRaisesRegex(dev.BuildError, "input changed"):
            dev.verified_build(self.project)

    def test_incbin_dependency(self):
        source = self.root / "main.asm"
        source.write_text(source.read_text() + '\nincbin "pixels.bin"\n')
        (self.root / "pixels.bin").write_bytes(b"\x12\x34")
        self.build()
        (self.root / "pixels.bin").write_bytes(b"\x56\x78")
        with self.assertRaisesRegex(dev.BuildError, "input changed"):
            dev.verified_build(self.project)

    def test_bad_origin_entry_stack(self):
        for change, message in (({"origin": "0x8001"}, "emitted range"),
                                ({"entry": "missing"}, "entry symbol"),
                                ({"entry": "0x9000"}, "entry must"),
                                ({"stack": "0x8010"}, "overlaps")):
            with self.subTest(change=change):
                shutil.copy(FIXTURE / "spectrum-project.json", self.project)
                self.config(**change)
                with self.assertRaisesRegex(dev.BuildError, message):
                    self.build()

    def test_missing_tool_rom_and_bad_symbols(self):
        with self.assertRaisesRegex(dev.BuildError, "executable not found"):
            dev.build(self.project, str(self.root / "missing-pasmo"))
        self.build()
        with self.assertRaisesRegex(dev.BuildError, "Spectrum ROM"):
            dev.run(self.project, RUNNER, str(self.root / "missing.rom"))
        bad = self.root / "bad.sym"
        bad.write_text("not a symbol\n")
        with self.assertRaisesRegex(dev.BuildError, "invalid Pasmo symbol"):
            dev.symbols_from_listing(bad, "", "start", 0x8000, 21)

    def test_run_subprocess_arguments(self):
        self.build()
        rom = self.root / "test.rom"
        rom.write_bytes(bytes(16384))
        # Boundary spy: verify that launch receives distinct arguments, symbols,
        # entry, stack and auto-run even when all paths contain spaces.
        spy = self.root / "debugger spy"
        spy.write_text(f"#!{sys.executable}\nimport json, pathlib, sys\n"
                       f"pathlib.Path({str(self.root / 'args.json')!r}).write_text(json.dumps(sys.argv[1:]))\n")
        spy.chmod(0o755)
        self.assertEqual(dev.run(self.project, str(spy), str(rom)), 0)
        args = json.loads((self.root / "args.json").read_text())
        self.assertIn("--start", args)
        self.assertEqual(args[args.index("--entry") + 1], "32768")
        self.assertEqual(Path(args[args.index("--spectrum") + 1]).resolve(), rom.resolve())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--pasmo", default="pasmo")
    parser.add_argument("--runner", required=True)
    args = parser.parse_args()
    PASMO = shutil.which(args.pasmo)
    RUNNER = str(Path(args.runner).resolve())
    if not PASMO:
        print("SKIP: Pasmo unavailable")
        sys.exit(77)
    unittest.main(argv=[sys.argv[0]], verbosity=2)
