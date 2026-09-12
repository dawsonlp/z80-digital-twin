from __future__ import annotations

from io import BytesIO
import json
from pathlib import Path
import tempfile
from typing import Any
import unittest

from lsp_z80.server import LspServer, MessageWriter, read_message, serve


def framed(message: dict[str, object]) -> bytes:
    payload = json.dumps(message, separators=(",", ":")).encode("utf-8")
    return f"Content-Length: {len(payload)}\r\n\r\n".encode("ascii") + payload


class ProtocolTests(unittest.TestCase):
    def test_message_round_trip(self) -> None:
        expected = {"jsonrpc": "2.0", "id": 7, "method": "example", "params": {"value": "λ"}}
        output = BytesIO()
        MessageWriter(output)(expected)
        output.seek(0)
        self.assertEqual(expected, read_message(output))

    def test_end_to_end_stdio_lifecycle(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root_uri = Path(directory).as_uri()
            messages = [
                {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"rootUri": root_uri}},
                {"jsonrpc": "2.0", "method": "initialized", "params": {}},
                {"jsonrpc": "2.0", "id": 2, "method": "shutdown", "params": {}},
                {"jsonrpc": "2.0", "method": "exit", "params": {}},
            ]
            stdin = BytesIO(b"".join(framed(message) for message in messages))
            stdout = BytesIO()
            self.assertEqual(0, serve(stdin, stdout))
            stdout.seek(0)
            responses = []
            while (message := read_message(stdout)) is not None:
                responses.append(message)
            self.assertTrue(any(message.get("id") == 1 and "result" in message for message in responses))
            self.assertTrue(any(message.get("id") == 2 and message.get("result", "missing") is None for message in responses))


class LspServerTests(unittest.TestCase):
    def test_initialize_open_query_and_rename(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            messages: list[dict[str, Any]] = []
            server = LspServer(messages.append)
            root_uri = Path(directory).as_uri()
            uri = (Path(directory) / "main.asm").as_uri()

            server.process({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"rootUri": root_uri, "initializationOptions": {"dialect": "pasmo"}}})
            initialize = next(message for message in messages if message.get("id") == 1)
            capabilities = initialize["result"]["capabilities"]
            self.assertTrue(capabilities["hoverProvider"])
            self.assertEqual("utf-16", capabilities["positionEncoding"])

            source = (
                "start:\n"
                "    LD A,0x2A\n"
                "    JP finish\n"
                "finish:\n"
                "    DB \"hello from lsp_z80\",0 ; visible comment\n"
                "    DB 1\n"
                "    DB 2\n"
                "\n"
                "after:\n"
                "    RET\n"
            )
            server.process({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {"uri": uri, "version": 1, "text": source}}})
            diagnostics = [message for message in messages if message.get("method") == "textDocument/publishDiagnostics" and message["params"]["uri"] == uri][-1]
            self.assertIn("pasmo.non-standard-hex", {item["code"] for item in diagnostics["params"]["diagnostics"]})

            server.process({"jsonrpc": "2.0", "id": 2, "method": "textDocument/definition", "params": {"textDocument": {"uri": uri}, "position": {"line": 2, "character": 9}}})
            definition = next(message for message in messages if message.get("id") == 2)
            self.assertEqual(3, definition["result"][0]["range"]["start"]["line"])

            server.process({"jsonrpc": "2.0", "id": 5, "method": "textDocument/hover", "params": {"textDocument": {"uri": uri}, "position": {"line": 2, "character": 9}}})
            hover = next(message for message in messages if message.get("id") == 5)
            hover_markdown = hover["result"]["contents"]["value"]
            self.assertIn("**finish** — symbol", hover_markdown)
            self.assertIn("`main.asm:4`", hover_markdown)
            self.assertIn("finish:", hover_markdown)
            self.assertIn('DB "hello from lsp_z80",0 ; visible comment', hover_markdown)
            self.assertIn("    DB 1", hover_markdown)
            self.assertIn("\n...\n", hover_markdown)
            self.assertNotIn("    DB 2", hover_markdown)
            self.assertNotIn("Pasmo", hover_markdown)

            server.process({"jsonrpc": "2.0", "id": 3, "method": "textDocument/rename", "params": {"textDocument": {"uri": uri}, "position": {"line": 2, "character": 9}, "newName": "done"}})
            rename = next(message for message in messages if message.get("id") == 3)
            self.assertEqual(2, len(rename["result"]["changes"][uri]))

            server.process({"jsonrpc": "2.0", "id": 4, "method": "textDocument/semanticTokens/full", "params": {"textDocument": {"uri": uri}}})
            tokens = next(message for message in messages if message.get("id") == 4)
            self.assertTrue(tokens["result"]["data"])

    def test_symbol_hover_preview_stops_at_return(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            messages: list[dict[str, Any]] = []
            server = LspServer(messages.append)
            root_uri = Path(directory).as_uri()
            uri = (Path(directory) / "main.asm").as_uri()
            source = "start:\n    nop ; retained\n    ret\n    db 1\nnext:\n    jp start\n"
            server.process({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"rootUri": root_uri}})
            server.process({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {"uri": uri, "version": 1, "text": source}}})

            server.process({"jsonrpc": "2.0", "id": 2, "method": "textDocument/hover", "params": {"textDocument": {"uri": uri}, "position": {"line": 5, "character": 8}}})
            hover = next(message for message in messages if message.get("id") == 2)
            hover_markdown = hover["result"]["contents"]["value"]
            self.assertIn("start:\n    nop ; retained\n    ret", hover_markdown)
            self.assertNotIn("db 1", hover_markdown)
            self.assertNotIn("...", hover_markdown)

    def test_symbol_hover_preview_does_not_stop_at_jump(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            messages: list[dict[str, Any]] = []
            server = LspServer(messages.append)
            root_uri = Path(directory).as_uri()
            uri = (Path(directory) / "main.asm").as_uri()
            source = "start:\n    jp elsewhere\n    db 1\n    db 2\nelsewhere:\n    jp start\n"
            server.process({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"rootUri": root_uri}})
            server.process({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {"uri": uri, "version": 1, "text": source}}})

            server.process({"jsonrpc": "2.0", "id": 2, "method": "textDocument/hover", "params": {"textDocument": {"uri": uri}, "position": {"line": 5, "character": 8}}})
            hover = next(message for message in messages if message.get("id") == 2)
            hover_markdown = hover["result"]["contents"]["value"]
            self.assertIn("start:\n    jp elsewhere\n    db 1\n...", hover_markdown)

    def test_non_pasmo_dialect_is_rejected(self) -> None:
        messages: list[dict[str, Any]] = []
        server = LspServer(messages.append)
        server.process({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"initializationOptions": {"dialect": "sjasmplus"}}})
        self.assertEqual(-32602, messages[0]["error"]["code"])

    def test_unknown_request_uses_method_not_found(self) -> None:
        messages: list[dict[str, Any]] = []
        server = LspServer(messages.append)
        server.process({"jsonrpc": "2.0", "id": 9, "method": "z80/notReal", "params": {}})
        self.assertEqual(-32601, messages[0]["error"]["code"])


if __name__ == "__main__":
    unittest.main()
