import * as os from "node:os";
import * as path from "node:path";
import * as vscode from "vscode";
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  State
} from "vscode-languageclient/node";

const CLIENT_ID = "lsp-z80";
const CONFIGURATION_SECTION = "lsp-z80";

let client: LanguageClient | undefined;
let watcher: vscode.FileSystemWatcher | undefined;

function firstWorkspaceFolder(): vscode.WorkspaceFolder | undefined {
  return vscode.workspace.workspaceFolders?.[0];
}

function expandValue(value: string, workspaceFolder: vscode.WorkspaceFolder | undefined): string {
  let expanded = value;
  if (expanded === "~") {
    expanded = os.homedir();
  } else if (expanded.startsWith("~/") || expanded.startsWith("~\\")) {
    expanded = path.join(os.homedir(), expanded.slice(2));
  }
  if (workspaceFolder) {
    expanded = expanded.replaceAll("${workspaceFolder}", workspaceFolder.uri.fsPath);
  }
  return expanded;
}

function resolveIncludePath(value: string, workspaceFolder: vscode.WorkspaceFolder | undefined): string {
  const expanded = expandValue(value, workspaceFolder);
  if (path.isAbsolute(expanded) || !workspaceFolder) {
    return expanded;
  }
  return path.resolve(workspaceFolder.uri.fsPath, expanded);
}

function initializationOptions(): Record<string, unknown> {
  const folder = firstWorkspaceFolder();
  const configuration = vscode.workspace.getConfiguration(CONFIGURATION_SECTION, folder?.uri);
  const includePaths = configuration
    .get<string[]>("includePaths", [])
    .map((value) => resolveIncludePath(value, folder));

  return {
    dialect: configuration.get<string>("dialect", "pasmo"),
    includePaths,
    predefinedSymbols: configuration.get<string[]>("predefinedSymbols", []),
    diagnostics: configuration.get<Record<string, string>>("diagnostics", {})
  };
}

function serverOptions(): ServerOptions {
  const folder = firstWorkspaceFolder();
  const configuration = vscode.workspace.getConfiguration(CONFIGURATION_SECTION, folder?.uri);
  const command = expandValue(configuration.get<string>("server.path", "lsp-z80"), folder);
  const args = configuration
    .get<string[]>("server.arguments", [])
    .map((value) => expandValue(value, folder));

  return {
    command,
    args,
    options: folder ? { cwd: folder.uri.fsPath } : undefined
  };
}

function createClient(): LanguageClient {
  watcher = vscode.workspace.createFileSystemWatcher("**/*.{asm,z80,inc,s}");
  const clientOptions: LanguageClientOptions = {
    documentSelector: [
      { scheme: "file", language: "z80" }
    ],
    initializationOptions: initializationOptions(),
    synchronize: {
      fileEvents: watcher
    }
  };

  return new LanguageClient(
    CLIENT_ID,
    "Z80 Language Server",
    serverOptions(),
    clientOptions
  );
}

async function startClient(): Promise<void> {
  if (client && client.state !== State.Stopped) {
    return;
  }
  client = createClient();
  await client.start();
}

async function stopClient(): Promise<void> {
  const activeClient = client;
  client = undefined;
  if (activeClient && activeClient.state !== State.Stopped) {
    await activeClient.stop();
  }
  watcher?.dispose();
  watcher = undefined;
}

async function restartClient(): Promise<void> {
  await stopClient();
  await startClient();
}

async function configurationChanged(event: vscode.ConfigurationChangeEvent): Promise<void> {
  if (!event.affectsConfiguration(CONFIGURATION_SECTION)) {
    return;
  }
  if (
    event.affectsConfiguration(`${CONFIGURATION_SECTION}.server.path`) ||
    event.affectsConfiguration(`${CONFIGURATION_SECTION}.server.arguments`) ||
    event.affectsConfiguration(`${CONFIGURATION_SECTION}.dialect`)
  ) {
    await restartClient();
    return;
  }
  if (client?.state === State.Running) {
    await client.sendNotification("workspace/didChangeConfiguration", {
      settings: { [CONFIGURATION_SECTION]: initializationOptions() }
    });
  }
}

export async function activate(context: vscode.ExtensionContext): Promise<void> {
  context.subscriptions.push(
    vscode.commands.registerCommand("lsp-z80.restartServer", restartClient),
    vscode.commands.registerCommand("lsp-z80.showOutput", () => client?.outputChannel.show()),
    vscode.workspace.onDidChangeConfiguration((event) => {
      void configurationChanged(event);
    })
  );

  await startClient();
}

export async function deactivate(): Promise<void> {
  await stopClient();
}
