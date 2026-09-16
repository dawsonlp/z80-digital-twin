"""Dependency-free JSON-RPC and Language Server Protocol adapter."""

from __future__ import annotations

import json
from pathlib import Path
import re
import sys
from typing import BinaryIO, Callable

from . import __version__
from .analysis import Completion
from .pasmo import PasmoDialect
from .workspace import Workspace, span_to_range, uri_to_path, utf16_to_index, utf16_units


JSON = dict[str, object]

_PREVIEW_LABEL = re.compile(r"^\s*[A-Za-z_?@.][A-Za-z0-9_?@.$]*\s*:")
_PREVIEW_TERMINATOR = re.compile(
    r"^\s*(?:[A-Za-z_?@.][A-Za-z0-9_?@.$]*\s*:\s*)?"
    r"ret(?:i|n)?\b",
    re.IGNORECASE,
)


def _symbol_preview(text: str, start_line: int, maximum_lines: int = 3) -> str:
    """Return a small, source-faithful definition block for hover markdown."""
    lines = text.splitlines()
    if not 0 <= start_line < len(lines):
        return ""

    preview: list[str] = []
    ended = False
    index = start_line
    while index < len(lines) and len(preview) < maximum_lines:
        line = lines[index].rstrip()
        if index > start_line and (not line.strip() or _PREVIEW_LABEL.match(line)):
            ended = True
            break
        preview.append(line)
        index += 1
        if _PREVIEW_TERMINATOR.match(line):
            ended = True
            break

    if not ended and index < len(lines):
        next_line = lines[index]
        if next_line.strip() and not _PREVIEW_LABEL.match(next_line):
            preview.append("...")
    return "\n".join(preview)


class InvalidParams(ValueError):
    pass


class MethodNotFound(LookupError):
    pass


def read_message(stream: BinaryIO) -> JSON | None:
    """Read one Content-Length framed JSON-RPC message."""
    headers: dict[str, str] = {}
    while True:
        line = stream.readline()
        if not line:
            return None
        if line in {b"\r\n", b"\n"}:
            break
        try:
            name, value = line.decode("ascii").split(":", 1)
        except (UnicodeDecodeError, ValueError) as error:
            raise ValueError("Malformed JSON-RPC header") from error
        headers[name.strip().lower()] = value.strip()
    try:
        length = int(headers["content-length"])
    except (KeyError, ValueError) as error:
        raise ValueError("Missing or invalid Content-Length header") from error
    if length < 0:
        raise ValueError("Negative Content-Length")
    payload = stream.read(length)
    if len(payload) != length:
        raise ValueError("Unexpected end of JSON-RPC payload")
    message = json.loads(payload.decode("utf-8"))
    if not isinstance(message, dict):
        raise ValueError("JSON-RPC message must be an object")
    return message


class MessageWriter:
    def __init__(self, stream: BinaryIO) -> None:
        self.stream = stream

    def __call__(self, message: JSON) -> None:
        payload = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self.stream.write(f"Content-Length: {len(payload)}\r\n\r\n".encode("ascii"))
        self.stream.write(payload)
        self.stream.flush()


class LspServer:
    def __init__(self, send: Callable[[JSON], None]) -> None:
        self.send = send
        self.dialect = PasmoDialect()
        self.workspace = Workspace(self.dialect)
        self.initialized = False
        self.shutdown_requested = False
        self.exit_requested = False

    def process(self, message: JSON) -> None:
        method = message.get("method")
        if not isinstance(method, str):
            if "id" in message:
                self._error(message.get("id"), -32600, "Invalid Request")
            return
        params = message.get("params", {})
        if not isinstance(params, dict):
            params = {}
        request_id = message.get("id")
        is_request = "id" in message
        try:
            result = self._dispatch(method, params)
        except MethodNotFound as error:
            if is_request:
                self._error(request_id, -32601, str(error))
        except InvalidParams as error:
            if is_request:
                self._error(request_id, -32602, str(error))
        except Exception as error:  # Keep a malformed document/request from killing the server.
            print(f"lsp-z80: {method} failed: {error}", file=sys.stderr)
            if is_request:
                self._error(request_id, -32603, "Internal error")
        else:
            if is_request:
                self.send({"jsonrpc": "2.0", "id": request_id, "result": result})

    def _dispatch(self, method: str, params: JSON) -> object:
        if method == "initialize":
            return self._initialize(params)
        if method == "initialized":
            self.initialized = True
            self.publish_diagnostics()
            return None
        if method == "shutdown":
            self.shutdown_requested = True
            return None
        if method == "exit":
            self.exit_requested = True
            return None
        if method == "$/cancelRequest":
            return None
        if method == "textDocument/didOpen":
            self._did_open(params)
            return None
        if method == "textDocument/didChange":
            self._did_change(params)
            return None
        if method == "textDocument/didClose":
            self._did_close(params)
            return None
        if method == "textDocument/didSave":
            self.workspace.reanalyze()
            self.publish_diagnostics()
            return None
        if method in {"workspace/didChangeWatchedFiles", "workspace/didChangeWorkspaceFolders"}:
            self.workspace.load_workspace_files()
            self.publish_diagnostics()
            return None
        if method == "workspace/didChangeConfiguration":
            self._did_change_configuration(params)
            return None
        if method == "textDocument/completion":
            return self._completion(params)
        if method == "textDocument/hover":
            return self._hover(params)
        if method == "textDocument/definition":
            uri, line, character = self._text_position(params)
            return self.workspace.definitions_at(uri, line, character)
        if method == "textDocument/references":
            uri, line, character = self._text_position(params)
            context = params.get("context", {})
            include_declaration = isinstance(context, dict) and bool(context.get("includeDeclaration", False))
            return self.workspace.references_at(uri, line, character, include_declaration)
        if method == "textDocument/documentHighlight":
            return self._document_highlights(params)
        if method == "textDocument/documentSymbol":
            return self.workspace.document_symbols(self._text_uri(params))
        if method == "workspace/symbol":
            query = params.get("query", "")
            return self.workspace.workspace_symbols(query if isinstance(query, str) else "")
        if method == "textDocument/prepareRename":
            uri, line, character = self._text_position(params)
            name = self.workspace.symbol_at(uri, line, character)
            if name is None or len(self.workspace.definitions_at(uri, line, character)) != 1:
                return None
            document = self.workspace.documents.get(uri)
            if document is None:
                return None
            spans = [symbol.span for symbol in document.analysis.symbols if symbol.name == name]
            spans.extend(reference.span for reference in document.analysis.references if reference.name == name)
            span = next((candidate for candidate in spans if candidate.contains(line, character)), None)
            return {"range": span_to_range(document.text, span), "placeholder": name} if span else None
        if method == "textDocument/rename":
            uri, line, character = self._text_position(params)
            new_name = params.get("newName")
            if not isinstance(new_name, str):
                raise InvalidParams("newName must be a string")
            return self.workspace.rename_at(uri, line, character, new_name)
        if method == "textDocument/semanticTokens/full":
            return self._semantic_tokens(params)
        if method == "textDocument/foldingRange":
            document = self.workspace.documents.get(self._text_uri(params))
            return [] if document is None else [
                {"startLine": fold.start_line, "endLine": fold.end_line, "kind": fold.kind}
                for fold in document.analysis.folds
            ]
        if method.startswith("$/"):
            return None
        raise MethodNotFound(f"Method not supported: {method}")

    def _initialize(self, params: JSON) -> JSON:
        options = params.get("initializationOptions", {})
        if not isinstance(options, dict):
            options = {}
        requested_dialect = options.get("dialect", "pasmo")
        if requested_dialect != "pasmo":
            raise InvalidParams("Only the 'pasmo' dialect is supported")
        roots: list[str] = []
        folders = params.get("workspaceFolders")
        if isinstance(folders, list):
            roots.extend(
                folder["uri"] for folder in folders
                if isinstance(folder, dict) and isinstance(folder.get("uri"), str)
            )
        root_uri = params.get("rootUri")
        if not roots and isinstance(root_uri, str):
            roots.append(root_uri)
        if not roots:
            roots.append(Path.cwd().resolve().as_uri())
        self.workspace.configure(roots, options)
        return {
            "capabilities": {
                "positionEncoding": "utf-16",
                "textDocumentSync": {"openClose": True, "change": 1, "save": {"includeText": False}},
                "completionProvider": {"resolveProvider": False, "triggerCharacters": [".", "@", "$", ","]},
                "hoverProvider": True,
                "definitionProvider": True,
                "referencesProvider": True,
                "documentHighlightProvider": True,
                "documentSymbolProvider": True,
                "workspaceSymbolProvider": True,
                "renameProvider": {"prepareProvider": True},
                "foldingRangeProvider": True,
                "semanticTokensProvider": {
                    "legend": {"tokenTypes": list(self.dialect.token_types), "tokenModifiers": list(self.dialect.token_modifiers)},
                    "full": True,
                },
            },
            "serverInfo": {"name": "lsp-z80", "version": __version__},
        }

    def _did_open(self, params: JSON) -> None:
        document = params.get("textDocument")
        if not isinstance(document, dict) or not isinstance(document.get("uri"), str) or not isinstance(document.get("text"), str):
            raise InvalidParams("didOpen requires textDocument.uri and textDocument.text")
        version = document.get("version")
        self.workspace.open_document(document["uri"], document["text"], version if isinstance(version, int) else None)
        self.publish_diagnostics()

    def _did_change(self, params: JSON) -> None:
        document = params.get("textDocument")
        changes = params.get("contentChanges")
        if not isinstance(document, dict) or not isinstance(document.get("uri"), str) or not isinstance(changes, list) or not changes:
            raise InvalidParams("didChange requires a document URI and at least one full-content change")
        change = changes[-1]
        if not isinstance(change, dict) or not isinstance(change.get("text"), str):
            raise InvalidParams("Only full document synchronization is supported")
        version = document.get("version")
        self.workspace.change_document(document["uri"], change["text"], version if isinstance(version, int) else None)
        self.publish_diagnostics()

    def _did_close(self, params: JSON) -> None:
        uri = self._text_uri(params)
        self.workspace.close_document(uri)
        self.send({"jsonrpc": "2.0", "method": "textDocument/publishDiagnostics", "params": {"uri": uri, "diagnostics": []}})
        self.publish_diagnostics()

    def _did_change_configuration(self, params: JSON) -> None:
        settings = params.get("settings", {})
        if isinstance(settings, dict) and isinstance(settings.get("lsp-z80"), dict):
            settings = settings["lsp-z80"]
        if not isinstance(settings, dict):
            settings = {}
        root_uris = [root.as_uri() for root in self.workspace.roots]
        self.workspace.configure(root_uris, settings)
        self.publish_diagnostics()

    def _completion(self, params: JSON) -> JSON:
        uri, line, character = self._text_position(params)
        document = self.workspace.documents.get(uri)
        if document is None:
            return {"isIncomplete": False, "items": []}
        completions = self.dialect.completions(document.analysis, document.text, line, character)
        completions.extend(Completion(symbol.name, 6, "Workspace Pasmo symbol") for symbol in self.workspace.completion_symbols())
        unique = {completion.label: completion for completion in completions}
        return {
            "isIncomplete": False,
            "items": [
                {
                    "label": item.label,
                    "kind": item.kind,
                    "detail": item.detail,
                    **({"insertText": item.insert_text} if item.insert_text else {}),
                }
                for item in sorted(unique.values(), key=lambda value: value.label.upper())
            ],
        }

    def _hover(self, params: JSON) -> JSON | None:
        uri, line, character = self._text_position(params)
        document = self.workspace.documents.get(uri)
        if document is None:
            return None
        hover = self.dialect.hover(document.analysis, document.text, line, character)
        if hover:
            return {"contents": {"kind": "markdown", "value": hover.markdown}, "range": span_to_range(document.text, hover.span)}
        name = self.workspace.symbol_at(uri, line, character)
        if name is None:
            return None
        definitions = self.workspace.definition_entries(name)
        if not definitions:
            return None
        definition_uri, symbol = definitions[0]
        definition_document = self.workspace.document(definition_uri)
        if definition_document is None:
            return {"contents": {"kind": "markdown", "value": f"**{name}** — symbol"}}

        preview = _symbol_preview(definition_document.text, symbol.span.line) or name
        try:
            definition_path = uri_to_path(definition_uri)
            display_path = definition_path.name
            for root in self.workspace.roots:
                if definition_path.is_relative_to(root):
                    display_path = str(definition_path.relative_to(root))
                    break
        except ValueError:
            display_path = definition_uri
        qualifier = f"First definition of {len(definitions)}" if len(definitions) > 1 else "Definition"
        fence = "````" if "```" in preview else "```"
        markdown = (
            f"**{name}** — symbol\n\n"
            f"{qualifier}: `{display_path}:{symbol.span.line + 1}`\n\n"
            f"{fence}z80\n{preview}\n{fence}"
        )
        return {"contents": {"kind": "markdown", "value": markdown}}

    def _document_highlights(self, params: JSON) -> list[JSON]:
        uri, line, character = self._text_position(params)
        name = self.workspace.symbol_at(uri, line, character)
        document = self.workspace.documents.get(uri)
        if name is None or document is None:
            return []
        highlights: list[JSON] = [
            {"range": span_to_range(document.text, symbol.span), "kind": 3}
            for symbol in document.analysis.symbols if symbol.name == name
        ]
        highlights.extend(
            {"range": span_to_range(document.text, reference.span), "kind": 2}
            for reference in document.analysis.references if reference.name == name
        )
        return highlights

    def _semantic_tokens(self, params: JSON) -> JSON:
        document = self.workspace.documents.get(self._text_uri(params))
        if document is None:
            return {"data": []}
        type_indexes = {name: index for index, name in enumerate(self.dialect.token_types)}
        modifier_indexes = {name: index for index, name in enumerate(self.dialect.token_modifiers)}
        data: list[int] = []
        previous_line = 0
        previous_start = 0
        seen: set[tuple[int, int, int]] = set()
        for token in sorted(document.analysis.tokens, key=lambda item: (item.span.line, item.span.start, item.span.end)):
            key = (token.span.line, token.span.start, token.span.end)
            if key in seen or token.span.end <= token.span.start:
                continue
            seen.add(key)
            lines = document.text.splitlines()
            line_text = lines[token.span.line] if 0 <= token.span.line < len(lines) else ""
            token_start = utf16_units(line_text[: token.span.start])
            token_length = utf16_units(line_text[token.span.start : token.span.end])
            delta_line = token.span.line - previous_line
            delta_start = token_start - previous_start if delta_line == 0 else token_start
            modifiers = sum(1 << modifier_indexes[name] for name in token.modifiers if name in modifier_indexes)
            data.extend([delta_line, delta_start, token_length, type_indexes[token.token_type], modifiers])
            previous_line = token.span.line
            previous_start = token_start
        return {"data": data}

    def publish_diagnostics(self) -> None:
        for uri, diagnostics in self.workspace.all_diagnostics().items():
            document = self.workspace.documents.get(uri)
            params: JSON = {"uri": uri, "diagnostics": diagnostics}
            if document is not None and document.version is not None:
                params["version"] = document.version
            self.send({"jsonrpc": "2.0", "method": "textDocument/publishDiagnostics", "params": params})

    @staticmethod
    def _text_uri(params: JSON) -> str:
        document = params.get("textDocument")
        if not isinstance(document, dict) or not isinstance(document.get("uri"), str):
            raise InvalidParams("textDocument.uri is required")
        return document["uri"]

    def _text_position(self, params: JSON) -> tuple[str, int, int]:
        uri = self._text_uri(params)
        position = params.get("position")
        if not isinstance(position, dict):
            raise InvalidParams("position.line and position.character are required")
        line = position.get("line")
        character = position.get("character")
        if not isinstance(line, int) or not isinstance(character, int):
            raise InvalidParams("position.line and position.character are required")
        document = self.workspace.documents.get(uri)
        if document is not None:
            lines = document.text.splitlines()
            if 0 <= line < len(lines):
                character = utf16_to_index(lines[line], character)
        return uri, line, character

    def _error(self, request_id: object, code: int, message: str) -> None:
        self.send({"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}})


def serve(stdin: BinaryIO | None = None, stdout: BinaryIO | None = None) -> int:
    input_stream = stdin or sys.stdin.buffer
    output_stream = stdout or sys.stdout.buffer
    server = LspServer(MessageWriter(output_stream))
    while not server.exit_requested:
        try:
            message = read_message(input_stream)
        except (ValueError, UnicodeError, json.JSONDecodeError) as error:
            print(f"lsp-z80: protocol error: {error}", file=sys.stderr)
            return 1
        if message is None:
            break
        server.process(message)
    return 0 if server.shutdown_requested else 1
