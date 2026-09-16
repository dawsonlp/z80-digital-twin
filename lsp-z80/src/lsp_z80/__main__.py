"""Command-line entry point for lsp-z80."""

from __future__ import annotations

import argparse

from . import __version__
from .server import serve


def main() -> int:
    parser = argparse.ArgumentParser(description="Pasmo Z80 language server")
    parser.add_argument("--version", action="version", version=f"lsp-z80 {__version__}")
    parser.parse_args()
    return serve()


if __name__ == "__main__":
    raise SystemExit(main())
