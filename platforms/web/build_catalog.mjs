import { build } from "esbuild";
import { mkdir, copyFile } from "node:fs/promises";
import { spawnSync } from "node:child_process";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const [flatc, output] = process.argv.slice(2);
if (!flatc || !output) throw new Error("Usage: build_catalog.mjs <host-flatc> <output-directory>");
await mkdir(output, { recursive: true });
const source = join(here, "../../include/schemas/runtime/asset_catalog.fbs");
const result = spawnSync(flatc, ["--ts", "-o", output, source], { stdio: "inherit" });
if (result.status !== 0) throw new Error("Cannot generate the Web catalog accessor");
await copyFile(join(here, "catalog.ts"), join(output, "catalog.ts"));
await build({ entryPoints: [join(output, "catalog.ts")], outfile: join(output, "catalog.mjs"),
    bundle: true, format: "esm", platform: "browser", target: "es2022", minify: true,
    nodePaths: [join(here, "node_modules")], legalComments: "eof" });
