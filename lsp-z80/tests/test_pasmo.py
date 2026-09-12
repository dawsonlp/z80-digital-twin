from __future__ import annotations

import unittest

from lsp_z80.pasmo import PasmoDialect


class PasmoDialectTests(unittest.TestCase):
    def setUp(self) -> None:
        self.dialect = PasmoDialect()

    def test_canonical_and_compatible_hexadecimal_forms(self) -> None:
        analysis = self.dialect.analyze(
            "file:///hex.asm",
            "\n".join(
                [
                    "    ld a,$2a",
                    "    ld b,0x2a",
                    "    ld c,02Ah",
                    "    ld d,#2A",
                    "    ld e,&H2A",
                    "    defw $",
                ]
            ),
        )

        warnings = [diagnostic for diagnostic in analysis.diagnostics if diagnostic.code == "pasmo.non-standard-hex"]
        self.assertEqual(4, len(warnings))
        self.assertTrue(all("$2a" in warning.message for warning in warnings))
        self.assertFalse(any(diagnostic.span.line in {0, 5} for diagnostic in warnings))

    def test_valid_and_invalid_z80_operands(self) -> None:
        analysis = self.dialect.analyze(
            "file:///instructions.asm",
            "\n".join(
                [
                    "start:",
                    "    LD A,(HL)",
                    "    ADD IX,SP",
                    "    BIT 7,(IY+$2)",
                    "    JP NZ,start",
                    "    RET NZ",
                    "    LD A,($8000)",
                    "    LD IX,A",
                    "    IM 3",
                ]
            ),
        )

        invalid = [diagnostic for diagnostic in analysis.diagnostics if diagnostic.code == "z80.invalid-operands"]
        self.assertEqual([7, 8], [diagnostic.span.line for diagnostic in invalid])

        register_tokens = [token for token in analysis.tokens if token.token_type == "register"]
        self.assertTrue(register_tokens)

    def test_symbols_references_folds_and_unknown_operations(self) -> None:
        analysis = self.dialect.analyze(
            "file:///symbols.asm",
            "\n".join(
                [
                    "VALUE EQU $2A",
                    "start:",
                    "    LD A,VALUE",
                    "    IF VALUE",
                    "    JP start",
                    "    ENDIF",
                    "    NOTREAL A",
                ]
            ),
        )

        self.assertEqual({"VALUE", "start"}, {symbol.name for symbol in analysis.symbols})
        self.assertEqual({"VALUE", "start"}, {reference.name for reference in analysis.references})
        self.assertEqual([(3, 5)], [(fold.start_line, fold.end_line) for fold in analysis.folds])
        self.assertTrue(any(diagnostic.code == "pasmo.unknown-operation" for diagnostic in analysis.diagnostics))

    def test_completion_and_hover_are_useful_on_incomplete_source(self) -> None:
        text = "target:\n    ld a,$2a\n    j"
        analysis = self.dialect.analyze("file:///completion.asm", text)
        labels = {item.label for item in self.dialect.completions(analysis, text, 2, 5)}
        self.assertIn("jp", labels)
        self.assertIn("jr", labels)
        self.assertNotIn("JP", labels)
        self.assertIn("a", {item.label for item in self.dialect.completions(analysis, text, 1, 7)})

        hover = self.dialect.hover(analysis, text, 1, 5)
        self.assertIsNotNone(hover)
        assert hover is not None
        self.assertIn("**ld**", hover.markdown)
        self.assertIn("Load or store", hover.markdown)


if __name__ == "__main__":
    unittest.main()
