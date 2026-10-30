import fs from "node:fs";
import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

// 诊断写 stderr；--inputs 仅向 stdout 输出稳定的输入指纹 JSON。
function requireDirectory(directory) {
    if (!fs.lstatSync(directory).isDirectory()) throw new Error(`Expected directory: ${directory}`);
}

function validateAssetTree(directory) {
    for (const item of fs.readdirSync(directory, { withFileTypes: true })) {
        const filename = path.join(directory, item.name);
        if (item.isSymbolicLink()) throw new Error(`Asset symlinks are not supported: ${filename}`);
        if (item.isDirectory()) validateAssetTree(filename);
        else if (!item.isFile()) throw new Error(`Unsupported asset type: ${filename}`);
    }
}

function hashBytes(bytes) {
    // FNV-1a 64 位；使用两个精确的 32 位部分，避免逐字节 BigInt 影响大型 TS 编译器文件。
    let high = 0xcbf29ce4;
    let low = 0x84222325;
    for (const byte of bytes) {
        low = (low ^ byte) >>> 0;
        const product = low * 435;
        high = (high * 435 + low * 256 + Math.floor(product / 0x100000000)) >>> 0;
        low = product >>> 0;
    }
    return high.toString(16).padStart(8, "0") + low.toString(16).padStart(8, "0");
}

let staging;
try {
    const args = process.argv.slice(2);
    const inputsOnly = args.length === 3 && args[2] === "--inputs";
    const outputMode = args.length === 4 && args[2] === "--output";
    if (args[0] !== "--project" || (!inputsOnly && !outputMode)) {
        throw new Error(
            "Usage: node build_scripts.mjs --project <project-directory> (--inputs | --output <staging-directory>)",
        );
    }
    const project = path.resolve(args[1]);
    const settings = path.join(project, "settings");
    const cache = path.join(project, "cache");
    for (const name of ["settings", "assets", "cache"]) requireDirectory(path.join(project, name));
    validateAssetTree(path.join(project, "assets"));
    const projectRequire = createRequire(path.join(settings, "package.json"));
    let compilerRequire = projectRequire;
    try {
        projectRequire.resolve("typescript");
    } catch (error) {
        if (error.code !== "MODULE_NOT_FOUND") throw error;
        // 独立编辑器自带编译器；项目明确指定不同版本时必须由项目安装，不能静默替换。
        compilerRequire = createRequire(import.meta.url);
        const bundled = compilerRequire("typescript/package.json").version;
        const packagePath = path.join(settings, "package.json");
        const definition = JSON.parse(fs.readFileSync(packagePath, "utf8"));
        const requested =
            definition.devDependencies?.typescript ?? definition.dependencies?.typescript;
        if (requested !== undefined && requested !== bundled)
            throw new Error(
                `Project requires TypeScript ${requested}; bundled compiler is ${bundled}. Install the project dependencies.`,
            );
    }
    const ts = compilerRequire("typescript");
    const inputHashes = new Map();
    function record(filename) {
        const absolute = path.resolve(filename);
        inputHashes.set(absolute.split(path.sep).join("/"), hashBytes(fs.readFileSync(absolute)));
    }
    function readFile(filename) {
        const contents = ts.sys.readFile(filename);
        if (contents !== undefined) record(filename);
        return contents;
    }
    record(compilerRequire.resolve("typescript"));
    record(compilerRequire.resolve("typescript/package.json"));
    const configPath = path.join(settings, "tsconfig.json");
    const config = ts.readConfigFile(configPath, readFile);
    if (config.error)
        throw new Error(ts.flattenDiagnosticMessageText(config.error.messageText, "\n"));
    // readFile 同时捕获实际 extends 链（包括项目树以外的配置和 npm 包配置）。
    const parsed = ts.parseJsonConfigFileContent(
        config.config,
        { ...ts.sys, readFile },
        settings,
        undefined,
        configPath,
    );
    // 仓库外层 include/engine.d.ts 是约定的引擎声明入口，随编辑器交付。
    // 项目内部的同名文件和其他外部声明保留，不能按 basename 吞掉用户声明。
    const engineDeclarations = path.resolve(
        path.dirname(fileURLToPath(import.meta.url)),
        "../include/engine.d.ts",
    );
    parsed.fileNames = parsed.fileNames.filter((filename) => {
        const relative = path.relative(project, filename);
        const external =
            relative === ".." || relative.startsWith(".." + path.sep) || path.isAbsolute(relative);
        return !(
            external &&
            path.basename(filename) === "engine.d.ts" &&
            path.basename(path.dirname(filename)) === "include"
        );
    });
    parsed.fileNames.push(engineDeclarations);
    if (parsed.fileNames.some((filename) => !filename.endsWith(".ts"))) {
        throw new Error("Only .ts TypeScript inputs are supported");
    }
    const output = outputMode ? path.resolve(args[3]) : path.join(cache, ".script-output");
    const options = {
        ...parsed.options,
        target: ts.ScriptTarget.ES2022,
        module: ts.ModuleKind.ES2022,
        rootDir: path.join(project, "assets"),
        outDir: output,
        noEmitOnError: true,
        incremental: false,
        composite: false,
        declaration: false,
        emitDeclarationOnly: false,
        noEmit: false,
        outFile: undefined,
        declarationDir: undefined,
        tsBuildInfoFile: undefined,
        sourceMap: false,
        inlineSourceMap: true,
        inlineSources: true,
        sourceRoot: undefined,
        mapRoot: undefined,
        allowJs: false,
    };
    const host = ts.createCompilerHost(options);
    host.readFile = readFile;
    const program = ts.createProgram({
        rootNames: parsed.fileNames,
        options,
        host,
        projectReferences: parsed.projectReferences,
    });
    const diagnostics = [
        ...parsed.errors,
        ...program.getOptionsDiagnostics(),
        ...program.getGlobalDiagnostics(),
    ];
    if (!inputsOnly) diagnostics.push(...ts.getPreEmitDiagnostics(program));
    const diagnosticHost = {
        getCanonicalFileName: (name) => name,
        getCurrentDirectory: () => project,
        getNewLine: () => "\n",
    };
    if (diagnostics.length !== 0)
        throw new Error(ts.formatDiagnostics(diagnostics, diagnosticHost));
    const compiledOutputs = [];
    const emitted = program.emit(undefined, (filename, text, bom, onError, sources) => {
        compiledOutputs.push({ filename, text, sources: sources ?? [] });
    });
    if (emitted.emitSkipped || emitted.diagnostics.length !== 0)
        throw new Error(
            ts.formatDiagnostics(emitted.diagnostics, diagnosticHost) || "TypeScript emit failed",
        );
    const modules = [];
    for (const { filename, text, sources } of compiledOutputs) {
        const syntax = ts.createSourceFile(
            filename,
            text,
            ts.ScriptTarget.ES2022,
            true,
            ts.ScriptKind.JS,
        );
        const imports = new Set();
        function visit(node) {
            let specifier;
            if (ts.isImportDeclaration(node) || ts.isExportDeclaration(node))
                specifier = node.moduleSpecifier;
            else if (
                ts.isCallExpression(node) &&
                node.expression.kind === ts.SyntaxKind.ImportKeyword
            )
                specifier = node.arguments[0];
            if (specifier && ts.isStringLiteralLike(specifier)) imports.add(specifier.text);
            ts.forEachChild(node, visit);
        }
        visit(syntax);
        for (const source of sources) {
            const dependencies = new Set();
            for (const specifier of imports) {
                if (specifier === "engine") continue;
                if (!specifier.startsWith("./") && !specifier.startsWith("../"))
                    throw new Error(`Unsupported runtime module import: ${specifier}`);
                const resolved = ts.resolveModuleName(
                    specifier,
                    source.fileName,
                    options,
                    host,
                ).resolvedModule;
                if (!resolved || resolved.resolvedFileName.endsWith(".d.ts"))
                    throw new Error(`Runtime module has no emitted source: ${specifier}`);
                const relative = path.relative(
                    path.join(project, "assets"),
                    resolved.resolvedFileName,
                );
                if (relative.startsWith("..") || path.isAbsolute(relative))
                    throw new Error(`Runtime module is outside project assets: ${specifier}`);
                dependencies.add(relative.split(path.sep).join("/"));
            }
            modules.push({
                source: path
                    .relative(path.join(project, "assets"), source.fileName)
                    .split(path.sep)
                    .join("/"),
                dependencies: [...dependencies].sort(),
            });
        }
    }
    modules.sort((left, right) => left.source.localeCompare(right.source));
    const inputs = [...inputHashes]
        .sort(([left], [right]) => (left < right ? -1 : left > right ? 1 : 0))
        .map(([filename, hash]) => ({ path: filename, hash }));
    if (inputsOnly) {
        process.stdout.write(JSON.stringify({ inputs, modules }) + "\n");
    } else {
        // 调用方资源管线持有可自动释放的操作系统锁。
        if (fs.existsSync(output)) {
            requireDirectory(output);
            if (fs.readdirSync(output).length !== 0)
                throw new Error("Script staging output must be empty");
        } else {
            fs.mkdirSync(output, { recursive: true });
        }
        staging = fs.mkdtempSync(path.join(cache, ".script-build-"));
        const outputs = [];
        for (const { filename, text, sources } of compiledOutputs) {
            const relative = path.relative(output, filename);
            if (
                relative.startsWith("..") ||
                path.isAbsolute(relative) ||
                !filename.endsWith(".js")
            ) {
                throw new Error(`Unsupported script output: ${filename}`);
            }
            const target = path.join(staging, relative);
            fs.mkdirSync(path.dirname(target), { recursive: true });
            fs.writeFileSync(target, text, "utf8");
            for (const source of sources ?? []) {
                outputs.push({
                    source: path
                        .relative(path.join(project, "assets"), source.fileName)
                        .split(path.sep)
                        .join("/"),
                    output: relative.split(path.sep).join("/"),
                });
            }
        }
        fs.writeFileSync(
            path.join(staging, ".script-build.json"),
            JSON.stringify({ outputs, inputs, modules }, null, 2) + "\n",
        );
        for (const item of fs.readdirSync(staging))
            fs.renameSync(path.join(staging, item), path.join(output, item));
        fs.rmdirSync(staging);
        staging = undefined;
        console.error("TypeScript staging build completed");
    }
} catch (error) {
    console.error(`Script build failed: ${error.message}`);
    process.exitCode = 1;
} finally {
    if (staging) fs.rmSync(staging, { recursive: true, force: true });
}
