# Z80 Language Support for Visual Studio Code

This extension is a thin Visual Studio Code client for the editor-independent
`lsp-z80` process. It contributes the `z80` language, basic
editing rules, conservative TextMate highlighting, and an LSP client. Language
analysis remains entirely in the separately installed server.

The extension automatically recognizes unambiguous Z80 names: `.z80`,
`.z80asm`, `.z80inc`, `*.z80.asm`, and `*.z80.inc`. It deliberately does not
claim `.asm`, `.inc`, or `.s` globally because those suffixes are shared by many
processor families. For an `.asm` project, select **Z80 Assembly** from the
language-mode item in VS Code's status bar, or make the choice explicit in that
project's `.vscode/settings.json`:

```json
{
  "files.associations": {
    "*.asm": "z80",
    "*.inc": "z80"
  }
}
```

That association is workspace-scoped and therefore does not reinterpret ARM,
x86, 68000, or other assembly projects elsewhere.

The `z80` language identity is separate from the assembler dialect. Dialect is
an explicit project setting; `pasmo` is currently the only implemented choice:

```json
{
  "lsp-z80.dialect": "pasmo"
}
```

This leaves room for additional dialects—or a future `auto` mode based only on
unambiguous source evidence—without assigning a different language identity to
every Z80 syntax. The extension does not guess a dialect from content today.

The current package deliberately targets Visual Studio Code 1.136 or later. It
does not claim compatibility with editor versions that are not part of the test
baseline.

## Install the server

Install `lsp-z80` so the executable is on the PATH inherited by Visual Studio
Code, or configure an absolute path:

```json
{
  "lsp-z80.server.path": "/absolute/path/to/lsp-z80"
}
```

The extension passes these project settings to the server:

```json
{
  "lsp-z80.includePaths": ["${workspaceFolder}/include"],
  "lsp-z80.predefinedSymbols": ["DEBUG", "TARGET_Z80"],
  "lsp-z80.diagnostics": {
    "pasmo.non-standard-hex": "warning"
  }
}
```

Changes to the executable path or arguments restart the client automatically;
**Z80: Restart Language Server** is also available for manual recovery. Ordinary
analysis settings are synchronized without a restart.

## Develop and package

```sh
npm ci
npm run compile
npm run vsix
```

Run the real Extension Host integration test against an installed server:

```sh
LSP_Z80_SERVER_PATH=/absolute/path/to/lsp-z80 npm test
```

The test creates a fresh temporary Z80 project, opens it through VS Code, and
checks diagnostics, completion, and definition lookup across the LSP boundary.

The extension bundle is written to `dist/extension.js`; the installable VSIX is
written to this directory. Open this directory in VS Code and press `F5` to run
the included sample in an Extension Development Host, or install the VSIX with
**Extensions: Install from VSIX...**.

## Boundary

The extension does not import the Python package, infer a repository layout, or
embed an assembler. It starts the configured command over stdio and communicates
only through LSP. The server can therefore be installed, versioned, and exercised
independently of Visual Studio Code.
