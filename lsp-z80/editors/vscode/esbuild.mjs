import * as esbuild from "esbuild";

const production = process.argv.includes("--production");
const watch = process.argv.includes("--watch");

const context = await esbuild.context({
  entryPoints: ["src/extension.ts"],
  bundle: true,
  external: ["vscode"],
  format: "cjs",
  minify: production,
  platform: "node",
  sourcemap: production ? false : "linked",
  sourcesContent: false,
  outfile: "dist/extension.js",
  logLevel: "info"
});

if (watch) {
  await context.watch();
} else {
  await context.rebuild();
  await context.dispose();
}
