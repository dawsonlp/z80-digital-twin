import * as assert from "node:assert/strict";
import * as path from "node:path";
import * as vscode from "vscode";

async function waitFor<T>(read: () => T | undefined, timeoutMilliseconds = 10_000): Promise<T> {
  const deadline = Date.now() + timeoutMilliseconds;
  while (Date.now() < deadline) {
    const value = read();
    if (value !== undefined) {
      return value;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`Timed out after ${timeoutMilliseconds}ms`);
}

suite("Z80 language client", () => {
  test("activates the real server and exposes language features", async () => {
    const workspace = process.env.LSP_Z80_TEST_WORKSPACE;
    assert.ok(workspace, "test workspace path was not supplied");
    const uri = vscode.Uri.file(path.join(workspace, "main.asm"));
    const document = await vscode.workspace.openTextDocument(uri);
    await vscode.window.showTextDocument(document);

    assert.equal(document.languageId, "z80");

    const diagnostics = await waitFor(() => {
      const current = vscode.languages.getDiagnostics(uri);
      return current.some((item) => item.code === "pasmo.non-standard-hex") ? current : undefined;
    });
    assert.ok(diagnostics.some((item) => item.code === "pasmo.non-standard-hex"));

    await vscode.workspace
      .getConfiguration("lsp-z80", uri)
      .update("diagnostics", { "pasmo.non-standard-hex": "off" }, vscode.ConfigurationTarget.Workspace);
    await waitFor(() => {
      const current = vscode.languages.getDiagnostics(uri);
      return current.some((item) => item.code === "pasmo.non-standard-hex") ? undefined : current;
    });

    const completions = await vscode.commands.executeCommand<vscode.CompletionList>(
      "vscode.executeCompletionItemProvider",
      uri,
      new vscode.Position(3, 6)
    );
    assert.ok(completions.items.some((item) => item.label === "ld"));
    assert.ok(!completions.items.some((item) => item.label === "LD"));

    const definitions = await vscode.commands.executeCommand<Array<vscode.Location | vscode.LocationLink>>(
      "vscode.executeDefinitionProvider",
      uri,
      new vscode.Position(5, 8)
    );
    assert.equal(definitions.length, 1);

    const hovers = await vscode.commands.executeCommand<vscode.Hover[]>(
      "vscode.executeHoverProvider",
      uri,
      new vscode.Position(5, 8)
    );
    const hoverText = hovers
      .flatMap((hover) => hover.contents)
      .map((content) => typeof content === "string" ? content : content.value)
      .join("\n");
    assert.ok(hoverText.includes("**print** — symbol"));
    assert.ok(hoverText.includes("print:"));
    assert.ok(!hoverText.includes("Pasmo symbol"));

    const messageHovers = await vscode.commands.executeCommand<vscode.Hover[]>(
      "vscode.executeHoverProvider",
      uri,
      new vscode.Position(4, 12)
    );
    const messageHoverText = messageHovers
      .flatMap((hover) => hover.contents)
      .map((content) => typeof content === "string" ? content : content.value)
      .join("\n");
    assert.ok(messageHoverText.includes("comments remain visible in hover previews"));
    assert.ok(messageHoverText.includes("    db 13,10"));
    assert.ok(messageHoverText.includes("\n...\n"));
    assert.ok(!messageHoverText.includes("not shown in the three-line preview"));
  });
});
