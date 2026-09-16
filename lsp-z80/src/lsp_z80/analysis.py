"""Dialect-neutral analysis values shared by the server and workspace.

The LSP adapter intentionally does not know how Pasmo is parsed.  Future
dialects only need to produce these small, immutable values.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Protocol


@dataclass(frozen=True, slots=True)
class Span:
    line: int
    start: int
    end: int

    def contains(self, line: int, character: int) -> bool:
        return self.line == line and self.start <= character < self.end

    def to_range(self) -> dict[str, object]:
        return {
            "start": {"line": self.line, "character": self.start},
            "end": {"line": self.line, "character": self.end},
        }


@dataclass(frozen=True, slots=True)
class Diagnostic:
    span: Span
    code: str
    message: str
    severity: int = 2  # LSP Warning

    def to_lsp(self) -> dict[str, object]:
        return {
            "range": self.span.to_range(),
            "severity": self.severity,
            "code": self.code,
            "source": "lsp-z80",
            "message": self.message,
        }


@dataclass(frozen=True, slots=True)
class Symbol:
    name: str
    span: Span
    kind: int
    value: int | None = None
    container: str | None = None


@dataclass(frozen=True, slots=True)
class Reference:
    name: str
    span: Span


@dataclass(frozen=True, slots=True)
class SemanticToken:
    span: Span
    token_type: str
    modifiers: tuple[str, ...] = ()


@dataclass(frozen=True, slots=True)
class Include:
    path: str
    span: Span


@dataclass(frozen=True, slots=True)
class FoldingRegion:
    start_line: int
    end_line: int
    kind: str = "region"


@dataclass(slots=True)
class Analysis:
    diagnostics: list[Diagnostic] = field(default_factory=list)
    symbols: list[Symbol] = field(default_factory=list)
    references: list[Reference] = field(default_factory=list)
    tokens: list[SemanticToken] = field(default_factory=list)
    includes: list[Include] = field(default_factory=list)
    folds: list[FoldingRegion] = field(default_factory=list)


@dataclass(frozen=True, slots=True)
class Completion:
    label: str
    kind: int
    detail: str
    insert_text: str | None = None


@dataclass(frozen=True, slots=True)
class Hover:
    markdown: str
    span: Span


class Dialect(Protocol):
    """The complete language-specific boundary used by the LSP core."""

    name: str
    file_extensions: frozenset[str]
    token_types: tuple[str, ...]
    token_modifiers: tuple[str, ...]

    def analyze(self, uri: str, text: str) -> Analysis: ...

    def completions(self, analysis: Analysis, text: str, line: int, character: int) -> list[Completion]: ...

    def hover(self, analysis: Analysis, text: str, line: int, character: int) -> Hover | None: ...
