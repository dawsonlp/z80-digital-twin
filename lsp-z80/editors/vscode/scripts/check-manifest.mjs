import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";

const manifest = JSON.parse(await readFile(new URL("../package.json", import.meta.url), "utf8"));
const languages = manifest.contributes?.languages ?? [];
const z80 = languages.find((language) => language.id === "z80");

assert(z80, "package.json must contribute the z80 language");
assert.deepEqual(z80.extensions, [".z80", ".z80asm", ".z80inc"]);
assert.deepEqual(z80.filenamePatterns, ["*.z80.asm", "*.z80.inc"]);
assert.ok(!z80.extensions.includes(".asm"), "ambiguous .asm files must not be claimed globally");
assert.equal(manifest.main, "./dist/extension.js");
assert.equal(manifest.extensionKind?.[0], "workspace");
assert.equal(manifest.capabilities?.virtualWorkspaces, false);
assert.equal(manifest.contributes.configuration.properties["lsp-z80.server.path"].scope, "machine-overridable");
assert.deepEqual(manifest.contributes.configuration.properties["lsp-z80.dialect"].enum, ["pasmo"]);

console.log("Extension manifest checks passed.");
