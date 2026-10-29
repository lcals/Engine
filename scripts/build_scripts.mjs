import fs from "node:fs";
import path from "node:path";
import { createRequire } from "node:module";

// 所有输出写 stderr，供编辑器与 MCP 共用，stdout 仅留给宿主协议。
function fail(message) {
    console.error(message);
    process.exitCode = 1;
}

function requireDirectory(directory) {
    if (!fs.lstatSync(directory).isDirectory()) {
        throw new Error(`Expected directory: ${directory}`);
    }
}

function copyResources(source, destination) {
    for (const item of fs.readdirSync(source, { withFileTypes: true })) {
        const input = path.join(source, item.name);
        const output = path.join(destination, item.name);
        if (item.isSymbolicLink()) {
            throw new Error(`Asset symlinks are not supported: ${input}`);
        }
        if (item.isDirectory()) {
            fs.mkdirSync(output, { recursive: true });
            copyResources(input, output);
        } else if (item.isFile() && !item.name.endsWith(".ts") && item.name !== ".gitkeep") {
            if (fs.existsSync(output)) {
                throw new Error(`Source asset conflicts with generated output: ${input}`);
            }
            fs.copyFileSync(input, output);
        }
    }
}

let staging;
let lock;
try {
    const arguments_ = process.argv.slice(2);
    if (arguments_.length !== 2 || arguments_[0] !== "--project") {
        throw new Error("Usage: node build_scripts.mjs --project <project-directory>");
    }
    const project = path.resolve(arguments_[1]);
    for (const name of ["settings", "assets", "cache"]) {
        requireDirectory(path.join(project, name));
    }
    const settings = path.join(project, "settings");
    const cache = path.join(project, "cache");
    const lockPath = path.join(cache, ".script-build-lock");
    fs.mkdirSync(lockPath);
    lock = lockPath;
    const require = createRequire(path.join(settings, "package.json"));
    const ts = require("typescript");
    const configPath = path.join(settings, "tsconfig.json");
    const config = ts.readConfigFile(configPath, ts.sys.readFile);
    if (config.error) {
        throw new Error(ts.flattenDiagnosticMessageText(config.error.messageText, "\n"));
    }
    const parsed = ts.parseJsonConfigFileContent(config.config, ts.sys, settings);
    staging = fs.mkdtempSync(path.join(cache, ".script-build-"));
    const options = {
        ...parsed.options,
        target: ts.ScriptTarget.ES2022,
        module: ts.ModuleKind.ES2022,
        rootDir: path.join(project, "assets"),
        outDir: staging,
        noEmitOnError: true,
        incremental: false,
        composite: false,
        declaration: false,
        emitDeclarationOnly: false,
        noEmit: false,
        outFile: undefined,
        declarationDir: undefined,
        tsBuildInfoFile: undefined,
    };
    const program = ts.createProgram(parsed.fileNames, options);
    let diagnostics = [...parsed.errors, ...ts.getPreEmitDiagnostics(program)];
    if (diagnostics.length === 0) {
        diagnostics = program.emit().diagnostics;
    }
    if (diagnostics.length !== 0) {
        const host = {
            getCanonicalFileName: (name) => name,
            getCurrentDirectory: () => project,
            getNewLine: () => "\n",
        };
        throw new Error(ts.formatDiagnostics(diagnostics, host));
    }
    copyResources(path.join(project, "assets"), staging);
    if (!fs.statSync(path.join(staging, "scripts", "game.js")).isFile()) {
        throw new Error("Missing gameplay entry: assets/scripts/game.ts");
    }
    const output = path.join(cache, "assets");
    const backup = path.join(cache, ".script-build-previous");
    if (fs.existsSync(backup)) {
        throw new Error("An interrupted build backup exists; restore or remove cache/.script-build-previous first");
    }
    if (fs.existsSync(output)) {
        fs.renameSync(output, backup);
    }
    try {
        fs.renameSync(staging, output);
        staging = undefined;
    } catch (error) {
        if (fs.existsSync(backup)) {
            fs.renameSync(backup, output);
        }
        throw error;
    }
    fs.rmSync(backup, { recursive: true, force: true });
    console.error("TypeScript build completed");
} catch (error) {
    fail(`Script build failed: ${error.message}`);
} finally {
    if (staging) fs.rmSync(staging, { recursive: true, force: true });
    if (lock) fs.rmSync(lock, { recursive: true, force: true });
}
