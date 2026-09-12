"""Workspace state and cross-file semantics for the language server."""

from __future__ import annotations

from dataclasses import dataclass, field
import os
from pathlib import Path
from typing import Any, Iterable
from urllib.parse import unquote, urlparse

from .analysis import Analysis, Diagnostic, Dialect, Span, Symbol


def uri_to_path(uri: str) -> Path:
    parsed = urlparse(uri)
    if parsed.scheme != "file":
        raise ValueError(f"Only file URIs are supported: {uri}")
    path = unquote(parsed.path)
    if parsed.netloc:
        path = f"//{parsed.netloc}{path}"
    return Path(path).resolve()


def utf16_units(text: str) -> int:
    return len(text.encode("utf-16-le")) // 2


def utf16_to_index(text: str, units: int) -> int:
    if units <= 0:
        return 0
    consumed = 0
    for index, character in enumerate(text):
        width = 2 if ord(character) > 0xFFFF else 1
        if consumed + width > units:
            return index
        consumed += width
        if consumed == units:
            return index + 1
    return len(text)


def span_to_range(text: str, span: Span) -> dict[str, Any]:
    lines = text.splitlines()
    line_text = lines[span.line] if 0 <= span.line < len(lines) else ""
    return {
        "start": {"line": span.line, "character": utf16_units(line_text[: span.start])},
        "end": {"line": span.line, "character": utf16_units(line_text[: span.end])},
    }


@dataclass(slots=True)
class Document:
    uri: str
    text: str
    version: int | None
    open: bool
    analysis: Analysis = field(default_factory=Analysis)


class Workspace:
    def __init__(self, dialect: Dialect) -> None:
        self.dialect = dialect
        self.documents: dict[str, Document] = {}
        self.roots: list[Path] = []
        self.include_paths: list[Path] = []
        self.predefined_symbols: set[str] = set()
        self.diagnostic_settings: dict[str, int | None] = {}

    def document(self, uri: str) -> Document | None:
        document = self.documents.get(uri)
        if document is not None:
            return document
        try:
            return self.documents.get(uri_to_path(uri).as_uri())
        except ValueError:
            return None

    def configure(self, root_uris: Iterable[str], options: dict[str, object] | None = None) -> None:
        self.roots = self._valid_paths(root_uris)
        options = options or {}
        include_values = options.get("includePaths", [])
        if isinstance(include_values, list):
            self.include_paths = [Path(value).expanduser().resolve() for value in include_values if isinstance(value, str)]
        predefined = options.get("predefinedSymbols", [])
        if isinstance(predefined, list):
            self.predefined_symbols = {value for value in predefined if isinstance(value, str)}
        diagnostics = options.get("diagnostics", {})
        if isinstance(diagnostics, dict):
            self.diagnostic_settings = self._parse_diagnostic_settings(diagnostics)
        self.load_workspace_files()

    @staticmethod
    def _valid_paths(uris: Iterable[str]) -> list[Path]:
        paths: list[Path] = []
        for uri in uris:
            try:
                path = uri_to_path(uri)
            except ValueError:
                continue
            if path.is_dir() and path not in paths:
                paths.append(path)
        return paths

    @staticmethod
    def _parse_diagnostic_settings(values: dict[object, object]) -> dict[str, int | None]:
        severities = {"error": 1, "warning": 2, "information": 3, "hint": 4, "off": None}
        settings: dict[str, int | None] = {}
        for key, value in values.items():
            if isinstance(key, str) and isinstance(value, str) and value.lower() in severities:
                settings[key] = severities[value.lower()]
            elif isinstance(key, str) and isinstance(value, int) and value in {1, 2, 3, 4}:
                settings[key] = value
        return settings

    def load_workspace_files(self) -> None:
        self.documents = {uri: document for uri, document in self.documents.items() if document.open}
        open_paths: set[Path] = set()
        for document in self.documents.values():
            try:
                open_paths.add(uri_to_path(document.uri))
            except ValueError:
                continue
        for root in self.roots:
            for directory, subdirectories, filenames in os.walk(root):
                subdirectories[:] = [
                    name for name in subdirectories
                    if name not in {".git", ".hg", ".svn", ".venv", "venv", "node_modules", "build", "dist", "__pycache__"}
                ]
                for filename in filenames:
                    path = Path(directory, filename)
                    if path.suffix.lower() not in self.dialect.file_extensions:
                        continue
                    resolved_path = path.resolve()
                    if resolved_path in open_paths:
                        continue
                    uri = resolved_path.as_uri()
                    if uri in self.documents and self.documents[uri].open:
                        continue
                    try:
                        text = path.read_text(encoding="utf-8")
                    except (OSError, UnicodeError):
                        continue
                    self.documents[uri] = Document(uri, text, None, False)
        self.reanalyze()

    def open_document(self, uri: str, text: str, version: int | None) -> None:
        try:
            normalized = uri_to_path(uri).as_uri()
        except ValueError:
            normalized = uri
        if normalized != uri:
            self.documents.pop(normalized, None)
        self.documents[uri] = Document(uri, text, version, True)
        self.reanalyze()

    def change_document(self, uri: str, text: str, version: int | None) -> None:
        document = self.document(uri)
        if document is None:
            self.open_document(uri, text, version)
            return
        document.text = text
        document.version = version
        document.open = True
        self.reanalyze()

    def close_document(self, uri: str) -> None:
        try:
            path = uri_to_path(uri)
            text = path.read_text(encoding="utf-8")
        except (ValueError, OSError, UnicodeError):
            self.documents.pop(uri, None)
        else:
            self.documents[uri] = Document(uri, text, None, False)
        self.reanalyze()

    def reanalyze(self) -> None:
        for document in self.documents.values():
            document.analysis = self.dialect.analyze(document.uri, document.text)
        self._load_included_files()

    def _load_included_files(self) -> None:
        pending = list(self.documents.values())
        visited = set(self.documents)
        while pending:
            document = pending.pop()
            for include in document.analysis.includes:
                resolved = self.resolve_include(document.uri, include.path)
                if resolved is None:
                    continue
                uri = resolved.as_uri()
                if uri in visited:
                    continue
                try:
                    text = resolved.read_text(encoding="utf-8")
                except (OSError, UnicodeError):
                    continue
                child = Document(uri, text, None, False, self.dialect.analyze(uri, text))
                self.documents[uri] = child
                visited.add(uri)
                pending.append(child)

    def resolve_include(self, source_uri: str, include: str) -> Path | None:
        try:
            source_parent = uri_to_path(source_uri).parent
        except ValueError:
            return None
        candidate_paths = [source_parent / include, *(root / include for root in self.include_paths)]
        for candidate in candidate_paths:
            resolved = candidate.resolve()
            if resolved.is_file() and self._is_allowed(resolved):
                return resolved
        return None

    def _is_allowed(self, path: Path) -> bool:
        allowed_roots = [*self.roots, *self.include_paths]
        return any(path == root or path.is_relative_to(root) for root in allowed_roots)

    def _definitions(self) -> dict[str, list[tuple[str, Symbol]]]:
        definitions: dict[str, list[tuple[str, Symbol]]] = {}
        for uri, document in self.documents.items():
            for symbol in document.analysis.symbols:
                definitions.setdefault(symbol.name, []).append((uri, symbol))
        return definitions

    def definition_entries(self, name: str) -> list[tuple[str, Symbol]]:
        return self._definitions().get(name, [])

    def _include_graph(self) -> dict[Path, set[Path]]:
        graph: dict[Path, set[Path]] = {}
        for document in self.documents.values():
            try:
                source = uri_to_path(document.uri)
            except ValueError:
                continue
            edges = graph.setdefault(source, set())
            for include in document.analysis.includes:
                target = self.resolve_include(document.uri, include.path)
                if target is not None:
                    edges.add(target)
        return graph

    @staticmethod
    def _reachable(graph: dict[Path, set[Path]], start: Path, goal: Path) -> bool:
        pending = [start]
        visited: set[Path] = set()
        while pending:
            current = pending.pop()
            if current == goal:
                return True
            if current in visited:
                continue
            visited.add(current)
            pending.extend(graph.get(current, ()))
        return False

    def diagnostics_for(self, uri: str) -> list[dict[str, object]]:
        document = self.document(uri)
        if document is None:
            return []
        diagnostics = list(document.analysis.diagnostics)
        definitions = self._definitions()
        include_graph = self._include_graph()
        try:
            source_path = uri_to_path(document.uri)
        except ValueError:
            source_path = None

        for include in document.analysis.includes:
            target = self.resolve_include(document.uri, include.path)
            if target is None:
                diagnostics.append(Diagnostic(include.span, "pasmo.include-not-found", f"Included file '{include.path}' was not found in an approved source root.", 1))
            elif source_path is not None and self._reachable(include_graph, target, source_path):
                diagnostics.append(Diagnostic(include.span, "pasmo.include-cycle", f"Including '{include.path}' creates an include cycle.", 1))

        for reference in document.analysis.references:
            if reference.name not in definitions and reference.name not in self.predefined_symbols:
                diagnostics.append(Diagnostic(reference.span, "pasmo.undefined-symbol", f"Undefined symbol '{reference.name}'.", 1))

        for symbol in document.analysis.symbols:
            locations = definitions.get(symbol.name, [])
            if len(locations) > 1 and not any(
                diagnostic.code == "pasmo.duplicate-symbol" and diagnostic.span == symbol.span for diagnostic in diagnostics
            ):
                diagnostics.append(Diagnostic(symbol.span, "pasmo.duplicate-symbol", f"Duplicate workspace symbol '{symbol.name}'.", 1))

        lsp_diagnostics: list[dict[str, object]] = []
        for diagnostic in diagnostics:
            if diagnostic.code in self.diagnostic_settings:
                severity = self.diagnostic_settings[diagnostic.code]
                if severity is None:
                    continue
                diagnostic = Diagnostic(diagnostic.span, diagnostic.code, diagnostic.message, severity)
            item = diagnostic.to_lsp()
            item["range"] = span_to_range(document.text, diagnostic.span)
            lsp_diagnostics.append(item)
        return lsp_diagnostics

    def all_diagnostics(self) -> dict[str, list[dict[str, object]]]:
        return {uri: self.diagnostics_for(uri) for uri in self.documents}

    def symbol_at(self, uri: str, line: int, character: int) -> str | None:
        document = self.document(uri)
        if document is None:
            return None
        for symbol in document.analysis.symbols:
            if symbol.span.contains(line, character):
                return symbol.name
        for reference in document.analysis.references:
            if reference.span.contains(line, character):
                return reference.name
        return None

    def definitions_at(self, uri: str, line: int, character: int) -> list[dict[str, object]]:
        document = self.document(uri)
        if document is not None:
            for include in document.analysis.includes:
                if include.span.contains(line, character):
                    target = self.resolve_include(document.uri, include.path)
                    return [] if target is None else [self._location(target.as_uri(), Span(0, 0, 0))]
        name = self.symbol_at(uri, line, character)
        if name is None:
            return []
        return [self._location(def_uri, symbol.span) for def_uri, symbol in self._definitions().get(name, [])]

    def references_at(self, uri: str, line: int, character: int, include_declaration: bool) -> list[dict[str, object]]:
        name = self.symbol_at(uri, line, character)
        if name is None:
            return []
        locations: list[dict[str, object]] = []
        if include_declaration:
            locations.extend(self._location(def_uri, symbol.span) for def_uri, symbol in self._definitions().get(name, []))
        for ref_uri, document in self.documents.items():
            locations.extend(self._location(ref_uri, reference.span) for reference in document.analysis.references if reference.name == name)
        return locations

    def rename_at(self, uri: str, line: int, character: int, new_name: str) -> dict[str, object] | None:
        name = self.symbol_at(uri, line, character)
        if name is None or not new_name or not new_name.replace("$", "_").replace(".", "_").replace("@", "_").replace("?", "_").isidentifier():
            return None
        definitions = self._definitions().get(name, [])
        if len(definitions) != 1 or (new_name in self._definitions() and new_name != name):
            return None
        changes: dict[str, list[dict[str, object]]] = {}
        def_uri, symbol = definitions[0]
        changes.setdefault(def_uri, []).append({"range": self._range(def_uri, symbol.span), "newText": new_name})
        for ref_uri, document in self.documents.items():
            for reference in document.analysis.references:
                if reference.name == name:
                    changes.setdefault(ref_uri, []).append({"range": self._range(ref_uri, reference.span), "newText": new_name})
        return {"changes": changes}

    def document_symbols(self, uri: str) -> list[dict[str, object]]:
        document = self.document(uri)
        if document is None:
            return []
        return [
            {
                "name": symbol.name,
                "kind": symbol.kind,
                "range": span_to_range(document.text, symbol.span),
                "selectionRange": span_to_range(document.text, symbol.span),
                **({"detail": f"${symbol.value:X}"} if symbol.value is not None else {}),
            }
            for symbol in document.analysis.symbols
        ]

    def workspace_symbols(self, query: str) -> list[dict[str, object]]:
        lowered = query.lower()
        results: list[dict[str, object]] = []
        for uri, document in self.documents.items():
            for symbol in document.analysis.symbols:
                if lowered and lowered not in symbol.name.lower():
                    continue
                results.append({"name": symbol.name, "kind": symbol.kind, "location": self._location(uri, symbol.span)})
        return results

    def completion_symbols(self) -> list[Symbol]:
        return [symbol for document in self.documents.values() for symbol in document.analysis.symbols]

    def _range(self, uri: str, span: Span) -> dict[str, object]:
        document = self.document(uri)
        return span.to_range() if document is None else span_to_range(document.text, span)

    def _location(self, uri: str, span: Span) -> dict[str, object]:
        return {"uri": uri, "range": self._range(uri, span)}
