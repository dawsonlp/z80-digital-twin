import { access, mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import * as os from "node:os";
import * as path from "node:path";
import { runTests } from "@vscode/test-electron";

async function localVsCodeExecutable(): Promise<string | undefined> {
  const configured = process.env.VSCODE_EXECUTABLE_PATH;
  if (configured) {
    return configured;
  }
  if (process.platform !== "darwin") {
    return undefined;
  }
  const candidate = "/Applications/Visual Studio Code.app/Contents/MacOS/Code";
  try {
    await access(candidate);
    return candidate;
  } catch {
    return undefined;
  }
}

async function main(): Promise<void> {
  // Codex and VS Code integrated terminals may themselves be extension hosts.
  // A child Code process must run as the application, not as a Node process.
  delete process.env.ELECTRON_RUN_AS_NODE;
  delete process.env.VSCODE_ESM_ENTRYPOINT;

  const serverPath = process.env.LSP_Z80_SERVER_PATH;
  if (!serverPath) {
    throw new Error("LSP_Z80_SERVER_PATH must name the lsp-z80 executable under test");
  }

  const extensionDevelopmentPath = path.resolve(__dirname, "../../");
  const extensionTestsPath = path.resolve(__dirname, "suite/index");
  const workspacePath = await mkdtemp(path.join(os.tmpdir(), "lsp-z80-vscode-test-"));
  process.env.LSP_Z80_TEST_WORKSPACE = workspacePath;

  try {
    await mkdir(path.join(workspacePath, ".vscode"));
    await writeFile(
      path.join(workspacePath, ".vscode", "settings.json"),
      JSON.stringify({
        "lsp-z80.server.path": serverPath,
        "files.associations": { "*.asm": "z80" }
      }, null, 2),
      "utf8"
    );
    await writeFile(
      path.join(workspacePath, "main.asm"),
      "org $8000\n\nstart:\n    ld a,2Ah\n    ld hl,message\n    jp print\n\nprint:\n    ret\n\nmessage:\n    db \"hello from lsp_z80\",0 ; comments remain visible in hover previews\n    db 13,10\n    db 0\n    db \"not shown in the three-line preview\"\n",
      "utf8"
    );

    await runTests({
      vscodeExecutablePath: await localVsCodeExecutable(),
      extensionDevelopmentPath,
      extensionTestsPath,
      launchArgs: [workspacePath, "--disable-extensions"]
    });
  } finally {
    await rm(workspacePath, { recursive: true, force: true });
  }
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
