"""Pasmo syntax analysis.

This module intentionally implements language facts and no LSP transport.
It is a tolerant source analyzer: incomplete source should yield useful
results, while definite mistakes yield diagnostics.
"""

from __future__ import annotations

from dataclasses import dataclass
import re

from .analysis import (
    Analysis,
    Completion,
    Diagnostic,
    FoldingRegion,
    Hover,
    Include,
    Reference,
    SemanticToken,
    Span,
    Symbol,
)


_CANONICAL_HEX = re.compile(r"\$[0-9A-Fa-f]+\Z")
_TOKEN = re.compile(
    r'''(?P<string>"(?:\\.|[^"\\])*"|'(?:''|[^'])*')
      | (?P<hex>\$[0-9A-Fa-f]+|\#[0-9A-Fa-f]+|&[Hh]?[0-9A-Fa-f]+|0[xX][0-9A-Fa-f]+|[0-9][0-9A-Fa-f$]*[Hh])
      | (?P<binary>%[01]+|0[bB][01]+|[01]+[bB])
      | (?P<number>[0-9]+)
      | (?P<identifier>[A-Za-z_?@.][A-Za-z0-9_?@.$]*)
      | (?P<operator><<|>>|<=|>=|<>|==|!=|&&|\|\||[,:()+\-*/%&|^~=<>\[\]])
    ''',
    re.VERBOSE,
)

_IDENTIFIER = re.compile(r"[A-Za-z_?@.][A-Za-z0-9_?@.$]*\Z")

REGISTERS_8 = frozenset({"A", "B", "C", "D", "E", "H", "L", "I", "R"})
REGISTERS_16 = frozenset({"AF", "AF'", "BC", "DE", "HL", "SP", "IX", "IY"})
CONDITIONS = frozenset({"NZ", "Z", "NC", "C", "PO", "PE", "P", "M"})
JR_CONDITIONS = frozenset({"NZ", "Z", "NC", "C"})

INSTRUCTION_INFO: dict[str, str] = {
    "ADC": "Add with carry",
    "ADD": "Add",
    "AND": "Bitwise AND with the accumulator",
    "BIT": "Test a bit",
    "CALL": "Call a subroutine",
    "CCF": "Complement carry flag",
    "CP": "Compare with the accumulator",
    "CPD": "Compare and decrement",
    "CPDR": "Compare, decrement, and repeat",
    "CPI": "Compare and increment",
    "CPIR": "Compare, increment, and repeat",
    "CPL": "Complement accumulator",
    "DAA": "Decimal adjust accumulator",
    "DEC": "Decrement",
    "DI": "Disable interrupts",
    "DJNZ": "Decrement B and jump when nonzero",
    "EI": "Enable interrupts",
    "EX": "Exchange registers",
    "EXX": "Exchange the general-purpose register sets",
    "HALT": "Halt until an interrupt or reset",
    "IM": "Select interrupt mode",
    "IN": "Read from an input port",
    "INC": "Increment",
    "IND": "Input and decrement",
    "INDR": "Input, decrement, and repeat",
    "INI": "Input and increment",
    "INIR": "Input, increment, and repeat",
    "JP": "Absolute jump",
    "JR": "Relative jump",
    "LD": "Load or store a value",
    "LDD": "Load and decrement",
    "LDDR": "Load, decrement, and repeat",
    "LDI": "Load and increment",
    "LDIR": "Load, increment, and repeat",
    "NEG": "Negate accumulator",
    "NOP": "No operation",
    "OR": "Bitwise OR with the accumulator",
    "OTDR": "Output, decrement, and repeat",
    "OTIR": "Output, increment, and repeat",
    "OUT": "Write to an output port",
    "OUTD": "Output and decrement",
    "OUTI": "Output and increment",
    "POP": "Pop a register pair from the stack",
    "PUSH": "Push a register pair onto the stack",
    "RES": "Reset a bit",
    "RET": "Return from a subroutine",
    "RETI": "Return from interrupt",
    "RETN": "Return from non-maskable interrupt",
    "RL": "Rotate left through carry",
    "RLA": "Rotate accumulator left through carry",
    "RLC": "Rotate left circular",
    "RLCA": "Rotate accumulator left circular",
    "RLD": "Rotate nibbles left between A and (HL)",
    "RR": "Rotate right through carry",
    "RRA": "Rotate accumulator right through carry",
    "RRC": "Rotate right circular",
    "RRCA": "Rotate accumulator right circular",
    "RRD": "Rotate nibbles right between A and (HL)",
    "RST": "Call a restart vector",
    "SBC": "Subtract with carry",
    "SCF": "Set carry flag",
    "SET": "Set a bit",
    "SLA": "Shift left arithmetic",
    "SRA": "Shift right arithmetic",
    "SRL": "Shift right logical",
    "SUB": "Subtract from the accumulator",
    "XOR": "Bitwise exclusive OR with the accumulator",
}

DIRECTIVE_INFO: dict[str, str] = {
    "ALIGN": "Advance to an aligned address",
    "ASSERT": "Require an assembly-time condition",
    "DB": "Define bytes (alias of DEFB)",
    "DEFB": "Define bytes",
    "DEFL": "Define or replace a symbol value",
    "DEFM": "Define message bytes",
    "DEFS": "Reserve or fill bytes",
    "DEFW": "Define little-endian words",
    "DS": "Reserve or fill bytes (alias of DEFS)",
    "DW": "Define words (alias of DEFW)",
    "ELSE": "Begin the alternate conditional branch",
    "END": "End assembly",
    "ENDIF": "End conditional assembly",
    "ENDM": "End a macro",
    "ENDP": "End a procedure scope",
    "ENDR": "End a repeat block",
    "EQU": "Define a constant",
    "ERROR": "Emit an assembler error",
    "EXITM": "Exit a macro expansion",
    "IF": "Begin conditional assembly",
    "IFDEF": "Assemble when a symbol is defined",
    "IFNDEF": "Assemble when a symbol is not defined",
    "INCBIN": "Include binary data",
    "INCLUDE": "Include assembly source",
    "IRP": "Repeat a block over arguments",
    "LOCAL": "Declare macro-local symbols",
    "MACRO": "Define a macro",
    "ORG": "Set the assembly origin",
    "PHASE": "Temporarily change logical addresses",
    "DEPHASE": "End a PHASE region",
    "PROC": "Begin a procedure scope",
    "PUBLIC": "Export symbols",
    "REPT": "Repeat a block",
    "WARNING": "Emit an assembler warning",
}

_ZERO_OPERAND = frozenset(
    {
        "CCF", "CPD", "CPDR", "CPI", "CPIR", "CPL", "DAA", "DI", "EI", "EXX",
        "HALT", "IND", "INDR", "INI", "INIR", "LDD", "LDDR", "LDI", "LDIR", "NEG",
        "NOP", "OTDR", "OTIR", "OUTD", "OUTI", "RETI", "RETN", "RLA", "RLCA",
        "RLD", "RRA", "RRCA", "RRD", "SCF",
    }
)

_ONE_ALU = frozenset({"AND", "CP", "OR", "SUB", "XOR"})
_ONE_SHIFT = frozenset({"RL", "RLC", "RR", "RRC", "SLA", "SRA", "SRL"})
_BLOCK_START = {"IF": "ENDIF", "IFDEF": "ENDIF", "IFNDEF": "ENDIF", "MACRO": "ENDM", "PROC": "ENDP", "REPT": "ENDR", "IRP": "ENDR"}
_BLOCK_END = frozenset(_BLOCK_START.values())


@dataclass(frozen=True, slots=True)
class _Lexeme:
    kind: str
    text: str
    start: int
    end: int


def _strip_comment(line: str) -> tuple[str, int | None]:
    quote: str | None = None
    escaped = False
    for index, char in enumerate(line):
        if escaped:
            escaped = False
            continue
        if char == "\\" and quote == '"':
            escaped = True
            continue
        if quote:
            if char == quote:
                if quote == "'" and index + 1 < len(line) and line[index + 1] == "'":
                    continue
                quote = None
            continue
        if char in {'"', "'"}:
            quote = char
        elif char == ";":
            return line[:index], index
    return line, None


def _lex(text: str) -> list[_Lexeme]:
    return [_Lexeme(match.lastgroup or "", match.group(), match.start(), match.end()) for match in _TOKEN.finditer(text)]


def _canonical_hex(text: str) -> str:
    cleaned = text.replace("$", "")
    if cleaned.startswith("#"):
        digits = cleaned[1:]
    elif cleaned[:2].lower() == "&h":
        digits = cleaned[2:]
    elif cleaned.startswith("&"):
        digits = cleaned[1:]
    elif cleaned[:2].lower() == "0x":
        digits = cleaned[2:]
    elif cleaned[-1:].lower() == "h":
        digits = cleaned[:-1]
    else:
        digits = cleaned
    digits = digits.lstrip("0") or "0"
    return "$" + digits.lower()


def _parse_integer(text: str) -> int | None:
    compact = text.replace("$", "")
    try:
        if text.startswith("$"):
            return int(text[1:].replace("$", ""), 16)
        if compact.startswith("#"):
            return int(compact[1:], 16)
        if compact[:2].lower() == "&h":
            return int(compact[2:], 16)
        if compact.startswith("&"):
            return int(compact[1:], 16)
        if compact[:2].lower() == "0x":
            return int(compact[2:], 16)
        if compact[-1:].lower() == "h":
            return int(compact[:-1], 16)
        if compact.startswith("%"):
            return int(compact[1:], 2)
        if compact[:2].lower() == "0b":
            return int(compact[2:], 2)
        if compact[-1:].lower() == "b" and set(compact[:-1]) <= {"0", "1"}:
            return int(compact[:-1], 2)
        if compact.isdigit():
            return int(compact, 10)
    except ValueError:
        return None
    return None


def _split_operands(text: str) -> list[str]:
    if not text.strip():
        return []
    operands: list[str] = []
    start = 0
    depth = 0
    quote: str | None = None
    escaped = False
    for index, char in enumerate(text):
        if escaped:
            escaped = False
            continue
        if char == "\\" and quote == '"':
            escaped = True
            continue
        if quote:
            if char == quote:
                quote = None
            continue
        if char in {'"', "'"}:
            quote = char
        elif char in "([":
            depth += 1
        elif char in ")]":
            depth = max(0, depth - 1)
        elif char == "," and depth == 0:
            operands.append(text[start:index].strip())
            start = index + 1
    operands.append(text[start:].strip())
    return operands


def _compact(operand: str) -> str:
    return re.sub(r"\s+", "", operand).upper()


def _is_expression(operand: str) -> bool:
    value = _compact(operand)
    return bool(value) and value not in REGISTERS_8 | REGISTERS_16 | CONDITIONS and not (value.startswith("(") and value.endswith(")"))


def _is_indexed(value: str) -> bool:
    value = _compact(value)
    return bool(re.fullmatch(r"\((IX|IY)(?:[+-].+)?\)", value))


def _is_alu8(operand: str) -> bool:
    value = _compact(operand)
    return value in REGISTERS_8 - {"I", "R"} or value == "(HL)" or _is_indexed(value) or _is_expression(operand)


def _is_rotate_target(operand: str) -> bool:
    value = _compact(operand)
    return value in REGISTERS_8 - {"I", "R"} or value == "(HL)" or _is_indexed(value)


def _is_number_in(operand: str, allowed: set[int]) -> bool:
    parsed = _parse_integer(_compact(operand))
    return parsed is None or parsed in allowed


def _valid_ld(left: str, right: str) -> bool:
    a, b = _compact(left), _compact(right)
    r8 = REGISTERS_8 - {"I", "R"}
    if a in r8 and (b in r8 or b == "(HL)" or _is_indexed(b) or _is_expression(right)):
        return True
    if (a == "(HL)" or _is_indexed(a)) and (b in r8 or _is_expression(right)):
        return b not in {"I", "R"}
    if a == "A" and b in {"(BC)", "(DE)", "I", "R"}:
        return True
    if a == "A" and b.startswith("(") and b.endswith(")") and b not in {"(HL)", "(SP)", "(C)"} and not _is_indexed(b):
        return True
    if a in {"(BC)", "(DE)", "I", "R"} and b == "A":
        return True
    if a in {"BC", "DE", "HL", "SP", "IX", "IY"} and _is_expression(right):
        return True
    if a in {"BC", "DE", "HL", "SP", "IX", "IY"} and b.startswith("(") and b.endswith(")"):
        return b not in {"(BC)", "(DE)", "(HL)", "(SP)", "(C)"} and not _is_indexed(b)
    if a.startswith("(") and a.endswith(")") and b in {"A", "BC", "DE", "HL", "SP", "IX", "IY"}:
        return a not in {"(HL)", "(SP)", "(C)"} and not _is_indexed(a)
    if a == "SP" and b in {"HL", "IX", "IY"}:
        return True
    return False


def _valid_instruction(mnemonic: str, operands: list[str]) -> bool:
    if mnemonic in _ZERO_OPERAND:
        return not operands
    if mnemonic in _ONE_ALU:
        return len(operands) == 1 and _is_alu8(operands[0])
    if mnemonic in _ONE_SHIFT:
        return len(operands) == 1 and _is_rotate_target(operands[0])
    if mnemonic == "LD":
        return len(operands) == 2 and _valid_ld(*operands)
    if mnemonic in {"INC", "DEC"}:
        return len(operands) == 1 and (_is_rotate_target(operands[0]) or _compact(operands[0]) in REGISTERS_16)
    if mnemonic in {"BIT", "RES", "SET"}:
        return len(operands) == 2 and _is_number_in(operands[0], set(range(8))) and _is_rotate_target(operands[1])
    if mnemonic in {"ADD", "ADC", "SBC"}:
        if len(operands) == 1:
            return mnemonic in {"ADC", "ADD", "SBC"} and _is_alu8(operands[0])
        if len(operands) != 2:
            return False
        left, right = _compact(operands[0]), _compact(operands[1])
        if left == "A":
            return _is_alu8(operands[1])
        if left == "HL" and mnemonic in {"ADC", "ADD", "SBC"}:
            return right in {"BC", "DE", "HL", "SP"}
        if left == "IX" and mnemonic == "ADD":
            return right in {"BC", "DE", "IX", "SP"}
        if left == "IY" and mnemonic == "ADD":
            return right in {"BC", "DE", "IY", "SP"}
        return False
    if mnemonic == "CALL":
        return (len(operands) == 1 and _is_expression(operands[0])) or (
            len(operands) == 2 and _compact(operands[0]) in CONDITIONS and _is_expression(operands[1])
        )
    if mnemonic == "JP":
        return (len(operands) == 1 and (_is_expression(operands[0]) or _compact(operands[0]) in {"(HL)", "(IX)", "(IY)"})) or (
            len(operands) == 2 and _compact(operands[0]) in CONDITIONS and _is_expression(operands[1])
        )
    if mnemonic == "JR":
        return (len(operands) == 1 and _is_expression(operands[0])) or (
            len(operands) == 2 and _compact(operands[0]) in JR_CONDITIONS and _is_expression(operands[1])
        )
    if mnemonic == "DJNZ":
        return len(operands) == 1 and _is_expression(operands[0])
    if mnemonic == "RET":
        return not operands or (len(operands) == 1 and _compact(operands[0]) in CONDITIONS)
    if mnemonic == "RST":
        return len(operands) == 1 and _is_number_in(operands[0], {0, 8, 16, 24, 32, 40, 48, 56})
    if mnemonic == "IM":
        return len(operands) == 1 and _is_number_in(operands[0], {0, 1, 2})
    if mnemonic in {"PUSH", "POP"}:
        return len(operands) == 1 and _compact(operands[0]) in {"AF", "BC", "DE", "HL", "IX", "IY"}
    if mnemonic == "EX":
        if len(operands) != 2:
            return False
        pair = (_compact(operands[0]), _compact(operands[1]))
        return pair in {("AF", "AF'"), ("DE", "HL"), ("(SP)", "HL"), ("(SP)", "IX"), ("(SP)", "IY")}
    if mnemonic == "IN":
        if len(operands) != 2:
            return False
        left, right = _compact(operands[0]), _compact(operands[1])
        return (left == "A" and right.startswith("(") and right.endswith(")")) or (left in REGISTERS_8 - {"I", "R"} and right == "(C)")
    if mnemonic == "OUT":
        if len(operands) != 2:
            return False
        left, right = _compact(operands[0]), _compact(operands[1])
        return (left == "(C)" and right in REGISTERS_8 - {"I", "R"}) or (left.startswith("(") and left.endswith(")") and right == "A")
    return True


def _word_at(text: str, line: int, character: int) -> tuple[str, Span] | None:
    lines = text.splitlines()
    if line < 0 or line >= len(lines):
        return None
    for token in _lex(_strip_comment(lines[line])[0]):
        if token.start <= character < token.end and token.kind in {"identifier", "hex", "binary", "number"}:
            return token.text, Span(line, token.start, token.end)
    return None


class PasmoDialect:
    name = "pasmo"
    file_extensions = frozenset({".asm", ".z80", ".inc", ".s"})
    token_types = ("comment", "string", "number", "operator", "keyword", "function", "variable", "label", "register")
    token_modifiers = ("declaration", "definition", "readonly", "deprecated", "inactive")

    def analyze(self, uri: str, text: str) -> Analysis:
        del uri  # Reserved for dialects with URI-dependent semantics.
        result = Analysis()
        lines = text.splitlines()
        macro_names = self._macro_names(lines)
        definitions: dict[str, list[Symbol]] = {}
        blocks: list[tuple[str, int, str]] = []

        for line_number, raw_line in enumerate(lines):
            code, comment_start = _strip_comment(raw_line)
            if comment_start is not None:
                result.tokens.append(SemanticToken(Span(line_number, comment_start, len(raw_line)), "comment"))
            lexemes = _lex(code)
            if not lexemes:
                continue

            for token in lexemes:
                span = Span(line_number, token.start, token.end)
                if token.kind == "string":
                    result.tokens.append(SemanticToken(span, "string"))
                elif token.kind in {"hex", "binary", "number"}:
                    result.tokens.append(SemanticToken(span, "number"))
                    if token.kind == "hex" and not _CANONICAL_HEX.fullmatch(token.text):
                        result.diagnostics.append(
                            Diagnostic(
                                span,
                                "pasmo.non-standard-hex",
                                f"Non-standard hexadecimal literal '{token.text}'; use '{_canonical_hex(token.text)}'.",
                            )
                        )
                elif token.kind == "operator":
                    result.tokens.append(SemanticToken(span, "operator"))

            self._check_delimiters(code, line_number, result)
            label, statement_index = self._label_and_statement(lexemes)
            if label is not None:
                label_token, symbol_kind, value = label
                span = Span(line_number, label_token.start, label_token.end)
                symbol = Symbol(label_token.text, span, symbol_kind, value)
                result.symbols.append(symbol)
                definitions.setdefault(symbol.name, []).append(symbol)
                result.tokens.append(SemanticToken(span, "label" if symbol_kind == 13 else "variable", ("declaration", "definition")))

            if statement_index >= len(lexemes):
                continue
            operation = lexemes[statement_index]
            if operation.kind != "identifier":
                continue
            op = operation.text.upper()
            op_span = Span(line_number, operation.start, operation.end)
            operand_text = code[operation.end:]
            operands = _split_operands(operand_text)

            if op in INSTRUCTION_INFO:
                result.tokens.append(SemanticToken(op_span, "function"))
                if not _valid_instruction(op, operands):
                    shown = ", ".join(operands) if operands else "no operands"
                    result.diagnostics.append(
                        Diagnostic(op_span, "z80.invalid-operands", f"Invalid operands for {op.lower()}: {shown}.", 1)
                    )
            elif op in DIRECTIVE_INFO:
                result.tokens.append(SemanticToken(op_span, "keyword"))
                self._handle_directive(op, operands, lexemes[statement_index + 1 :], line_number, result, blocks)
            elif op in macro_names:
                result.tokens.append(SemanticToken(op_span, "function"))
                result.references.append(Reference(operation.text, op_span))
            else:
                result.diagnostics.append(
                    Diagnostic(op_span, "pasmo.unknown-operation", f"Unknown Pasmo instruction, directive, or macro '{operation.text}'.", 1)
                )

            definition_span = label[0] if label is not None else None
            if op not in {"INCLUDE", "INCBIN", "LOCAL", "MACRO", "PUBLIC"}:
                self._collect_references(lexemes[statement_index + 1 :], line_number, definition_span, result)

        for name, symbols in definitions.items():
            if len(symbols) > 1:
                for duplicate in symbols[1:]:
                    result.diagnostics.append(Diagnostic(duplicate.span, "pasmo.duplicate-symbol", f"Duplicate symbol '{name}'.", 1))

        for opener, start_line, expected in blocks:
            result.diagnostics.append(
                Diagnostic(Span(start_line, 0, len(lines[start_line]) if start_line < len(lines) else 0), "pasmo.unclosed-block", f"{opener} block is missing {expected}.", 1)
            )
        return result

    @staticmethod
    def _macro_names(lines: list[str]) -> set[str]:
        names: set[str] = set()
        for raw_line in lines:
            code, _ = _strip_comment(raw_line)
            tokens = _lex(code)
            if len(tokens) >= 2 and tokens[0].kind == "identifier" and tokens[1].kind == "identifier" and tokens[1].text.upper() == "MACRO":
                names.add(tokens[0].text.upper())
        return names

    @staticmethod
    def _label_and_statement(lexemes: list[_Lexeme]) -> tuple[tuple[_Lexeme, int, int | None] | None, int]:
        first = lexemes[0]
        if first.kind != "identifier":
            return None, 0
        if len(lexemes) >= 2 and lexemes[1].text == ":":
            return (first, 13, None), 2
        second = lexemes[1].text.upper() if len(lexemes) >= 2 and lexemes[1].kind == "identifier" else ""
        if second in {"EQU", "DEFL"}:
            value = _parse_integer(lexemes[2].text) if len(lexemes) >= 3 else None
            return (first, 14, value), 1
        if second == "MACRO":
            return (first, 12, None), 1
        first_is_operation = first.text.upper() in INSTRUCTION_INFO or first.text.upper() in DIRECTIVE_INFO
        if first.start == 0 and not first_is_operation:
            if len(lexemes) == 1:
                return (first, 13, None), 1
            if second in INSTRUCTION_INFO or second in DIRECTIVE_INFO:
                return (first, 13, None), 1
        return None, 0

    @staticmethod
    def _check_delimiters(code: str, line: int, result: Analysis) -> None:
        depth = 0
        for index, char in enumerate(code):
            if char == "(":
                depth += 1
            elif char == ")":
                if depth == 0:
                    result.diagnostics.append(Diagnostic(Span(line, index, index + 1), "pasmo.unmatched-delimiter", "Unmatched closing parenthesis.", 1))
                else:
                    depth -= 1
        if depth:
            result.diagnostics.append(Diagnostic(Span(line, len(code), len(code)), "pasmo.unmatched-delimiter", "Unclosed parenthesis.", 1))

    @staticmethod
    def _handle_directive(
        operation: str,
        operands: list[str],
        operand_tokens: list[_Lexeme],
        line: int,
        result: Analysis,
        blocks: list[tuple[str, int, str]],
    ) -> None:
        if operation == "INCLUDE":
            if operands and len(operands) == 1:
                raw = operands[0]
                path = raw[1:-1] if len(raw) >= 2 and raw[0] in {'"', "'"} and raw[-1] == raw[0] else raw
                start = operand_tokens[0].start if operand_tokens else 0
                end = operand_tokens[-1].end if operand_tokens else start
                result.includes.append(Include(path, Span(line, start, end)))
            else:
                result.diagnostics.append(Diagnostic(Span(line, 0, 0), "pasmo.invalid-include", "INCLUDE requires exactly one file name.", 1))
        if operation in _BLOCK_START:
            blocks.append((operation, line, _BLOCK_START[operation]))
        elif operation in _BLOCK_END:
            if not blocks or blocks[-1][2] != operation:
                result.diagnostics.append(Diagnostic(Span(line, 0, len(operation)), "pasmo.unmatched-block-end", f"{operation} has no matching block start.", 1))
            else:
                _, start_line, _ = blocks.pop()
                if line > start_line:
                    result.folds.append(FoldingRegion(start_line, line, "region"))

    @staticmethod
    def _collect_references(
        tokens: list[_Lexeme],
        line: int,
        definition_token: _Lexeme | None,
        result: Analysis,
    ) -> None:
        reserved = REGISTERS_8 | REGISTERS_16 | CONDITIONS | set(INSTRUCTION_INFO) | set(DIRECTIVE_INFO)
        for token in tokens:
            if token.kind != "identifier" or token is definition_token:
                continue
            span = Span(line, token.start, token.end)
            if token.text.upper() in REGISTERS_8 | REGISTERS_16:
                result.tokens.append(SemanticToken(span, "register"))
                continue
            if token.text.upper() in CONDITIONS:
                result.tokens.append(SemanticToken(span, "keyword"))
                continue
            if token.text.upper() in reserved:
                continue
            result.references.append(Reference(token.text, span))
            result.tokens.append(SemanticToken(span, "variable"))

    @staticmethod
    def completions(analysis: Analysis, text: str, line: int, character: int) -> list[Completion]:
        prefix = ""
        lines = text.splitlines()
        if 0 <= line < len(lines):
            before = lines[line][:character]
            match = re.search(r"[A-Za-z_?@.][A-Za-z0-9_?@.$]*$", before)
            prefix = match.group().upper() if match else ""
        choices: list[Completion] = []
        choices.extend(Completion(name.lower(), 14, f"Z80 instruction — {detail}") for name, detail in INSTRUCTION_INFO.items())
        choices.extend(Completion(name.lower(), 14, f"Pasmo directive — {detail}") for name, detail in DIRECTIVE_INFO.items())
        choices.extend(Completion(name.lower(), 6, "Z80 register") for name in sorted(REGISTERS_8 | REGISTERS_16))
        choices.extend(Completion(symbol.name, 6, "Pasmo symbol") for symbol in analysis.symbols)
        unique = {item.label: item for item in choices if not prefix or item.label.upper().startswith(prefix)}
        return sorted(unique.values(), key=lambda item: item.label.upper())

    @staticmethod
    def hover(analysis: Analysis, text: str, line: int, character: int) -> Hover | None:
        found = _word_at(text, line, character)
        if found is None:
            return None
        word, span = found
        upper = word.upper()
        if upper in INSTRUCTION_INFO:
            return Hover(f"**{upper.lower()}** — Z80 instruction\n\n{INSTRUCTION_INFO[upper]}.", span)
        if upper in DIRECTIVE_INFO:
            return Hover(f"**{upper.lower()}** — Pasmo directive\n\n{DIRECTIVE_INFO[upper]}.", span)
        if upper in REGISTERS_8:
            return Hover(f"**{upper.lower()}** — Z80 8-bit register.", span)
        if upper in REGISTERS_16:
            return Hover(f"**{upper.lower()}** — Z80 16-bit register or register pair.", span)
        if _CANONICAL_HEX.fullmatch(word):
            value = _parse_integer(word)
            assert value is not None
            return Hover(f"**{word}** — canonical hexadecimal literal ({value} decimal).", span)
        if re.fullmatch(_TOKEN.pattern, word, re.VERBOSE):
            value = _parse_integer(word)
            if value is not None and any(char in word.lower() for char in ("x", "h", "#", "&")):
                return Hover(f"**{word}** — non-standard Pasmo hexadecimal literal\n\nCanonical form: `{_canonical_hex(word)}`", span)
        return None
