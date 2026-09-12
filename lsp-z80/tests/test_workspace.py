from __future__ import annotations

from pathlib import Path
import tempfile
from typing import Any, cast
import unittest

from lsp_z80.analysis import Span
from lsp_z80.pasmo import PasmoDialect
from lsp_z80.workspace import Workspace, span_to_range, uri_to_path, utf16_to_index, utf16_units


class WorkspaceTests(unittest.TestCase):
    def test_cross_file_definition_references_diagnostics_and_rename(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            main = root / "main.asm"
            definitions = root / "defs.inc"
            main.write_text('    INCLUDE "defs.inc"\nstart:\n    JP target\n', encoding="utf-8")
            definitions.write_text("target:\n    RET\n", encoding="utf-8")

            workspace = Workspace(PasmoDialect())
            workspace.configure([root.as_uri()])
            main_uri = main.as_uri()

            self.assertFalse(any(item["code"] == "pasmo.undefined-symbol" for item in workspace.diagnostics_for(main_uri)))
            locations = workspace.definitions_at(main_uri, 2, 8)
            self.assertEqual(definitions.resolve(), uri_to_path(cast(str, locations[0]["uri"])))
            include_location = workspace.definitions_at(main_uri, 0, 14)
            self.assertEqual(definitions.resolve(), uri_to_path(cast(str, include_location[0]["uri"])))
            references = workspace.references_at(main_uri, 2, 8, True)
            self.assertEqual(2, len(references))

            edit = workspace.rename_at(main_uri, 2, 8, "destination")
            self.assertIsNotNone(edit)
            assert edit is not None
            changes = cast(dict[str, Any], edit["changes"])
            self.assertEqual({main.resolve(), definitions.resolve()}, {uri_to_path(uri) for uri in changes})

    def test_missing_include_and_undefined_symbol_are_reported(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "main.asm"
            source.write_text('    INCLUDE "missing.inc"\n    JP nowhere\n', encoding="utf-8")
            workspace = Workspace(PasmoDialect())
            workspace.configure([root.as_uri()])

            codes = {item["code"] for item in workspace.diagnostics_for(source.as_uri())}
            self.assertIn("pasmo.include-not-found", codes)
            self.assertIn("pasmo.undefined-symbol", codes)

    def test_include_cycles_are_reported(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = root / "first.asm"
            second = root / "second.inc"
            first.write_text('    INCLUDE "second.inc"\n', encoding="utf-8")
            second.write_text('    INCLUDE "first.asm"\n', encoding="utf-8")
            workspace = Workspace(PasmoDialect())
            workspace.configure([root.as_uri()])

            first_codes = {item["code"] for item in workspace.diagnostics_for(first.as_uri())}
            second_codes = {item["code"] for item in workspace.diagnostics_for(second.as_uri())}
            self.assertIn("pasmo.include-cycle", first_codes)
            self.assertIn("pasmo.include-cycle", second_codes)

    def test_rescan_removes_deleted_closed_documents(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "old.asm"
            source.write_text("old_symbol:\n", encoding="utf-8")
            workspace = Workspace(PasmoDialect())
            workspace.configure([root.as_uri()])
            self.assertTrue(workspace.workspace_symbols("old_symbol"))

            source.unlink()
            workspace.load_workspace_files()
            self.assertFalse(workspace.workspace_symbols("old_symbol"))

    def test_rescan_does_not_duplicate_open_document_through_path_alias(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            real_root = root / "real"
            alias_root = root / "alias"
            real_root.mkdir()
            alias_root.symlink_to(real_root, target_is_directory=True)
            source = real_root / "main.asm"
            text = "target:\n    JP target\n"
            source.write_text(text, encoding="utf-8")

            workspace = Workspace(PasmoDialect())
            workspace.configure([real_root.as_uri()])
            alias_uri = (alias_root / "main.asm").as_uri()
            workspace.open_document(alias_uri, text, 1)
            workspace.load_workspace_files()

            locations = workspace.definitions_at(alias_uri, 1, 8)
            self.assertEqual(1, len(locations))

    def test_diagnostic_severity_can_be_changed_or_disabled(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "main.asm"
            source.write_text("    LD A,0x2A\n    JP absent\n", encoding="utf-8")
            workspace = Workspace(PasmoDialect())
            workspace.configure(
                [root.as_uri()],
                {"diagnostics": {"pasmo.non-standard-hex": "hint", "pasmo.undefined-symbol": "off"}},
            )

            diagnostics = workspace.diagnostics_for(source.as_uri())
            self.assertEqual(4, next(item["severity"] for item in diagnostics if item["code"] == "pasmo.non-standard-hex"))
            self.assertFalse(any(item["code"] == "pasmo.undefined-symbol" for item in diagnostics))

    def test_utf16_position_conversion(self) -> None:
        line = "😀label"
        self.assertEqual(7, utf16_units(line))
        self.assertEqual(1, utf16_to_index(line, 2))
        converted = span_to_range(line, Span(0, 1, 6))
        self.assertEqual(2, converted["start"]["character"])
        self.assertEqual(7, converted["end"]["character"])


if __name__ == "__main__":
    unittest.main()
