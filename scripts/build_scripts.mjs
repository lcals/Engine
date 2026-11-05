import fs from "node:fs";
import path from "node:path";
import { createRequire } from "node:module";
import { execFileSync } from "node:child_process";
import { fileURLToPath, pathToFileURL } from "node:url";

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
    // FNV-1a 64 位；两个精确的 32 位部分避免逐字节 BigInt 处理大型编译器文件。
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

function inside(directory, filename) {
    const relative = path.relative(directory, filename);
    return relative !== ".." && !relative.startsWith(".." + path.sep) && !path.isAbsolute(relative);
}

let staging;
let api;
const snapshots = [];
try {
    const args = process.argv.slice(2);
    const inputsOnly = args.length === 3 && args[2] === "--inputs";
    const outputMode = args.length === 4 && args[2] === "--output";
    const snippetMode = args.length === 4 && args[2] === "--snippet";
    if (args[0] !== "--project" || (!inputsOnly && !outputMode && !snippetMode))
        throw new Error(
            "Usage: node build_scripts.mjs --project <project-directory> (--inputs | --output <staging-directory> | --snippet <request.json>)",
        );
    const project = path.resolve(args[1]);
    const settings = path.join(project, "settings");
    const assets = path.join(project, "assets");
    const cache = path.join(project, "cache");
    for (const name of ["settings", "assets", "cache"]) requireDirectory(path.join(project, name));
    validateAssetTree(assets);
    const projectRequire = createRequire(path.join(settings, "package.json"));
    let compilerRequire = projectRequire;
    try {
        projectRequire.resolve("typescript");
    } catch (error) {
        if (error.code !== "MODULE_NOT_FOUND") throw error;
        // 项目显式安装的编译器优先；离开源码树后使用编辑器同版工具。
        compilerRequire = createRequire(import.meta.url);
        const bundled = compilerRequire("typescript/package.json").version;
        const definition = JSON.parse(fs.readFileSync(path.join(settings, "package.json"), "utf8"));
        const requested =
            definition.devDependencies?.typescript ?? definition.dependencies?.typescript;
        if (requested !== undefined && requested !== bundled)
            throw new Error(
                `Project requires TypeScript ${requested}; bundled compiler is ${bundled}. Install the project dependencies.`,
            );
    }
    // TypeScript 7 的官方原生编译器负责整批 emit；官方 API 负责配置、AST 和输入快照。
    // API 尚未提供 Program.emit，不保留旧 JS 编译器或另写 TS 解析/转换实现。
    const compilerPackage = compilerRequire.resolve("typescript/package.json");
    const compilerRoot = path.dirname(compilerPackage);
    const { API } = await import(
        pathToFileURL(compilerRequire.resolve("typescript/unstable/sync"))
    );
    const ts = await import(pathToFileURL(compilerRequire.resolve("typescript/unstable/ast")));
    const { default: getExePath } = await import(
        pathToFileURL(path.join(compilerRoot, "lib/getExePath.js"))
    );
    const compilerExecutable = getExePath();
    staging = fs.mkdtempSync(path.join(cache, ".script-build-"));
    const emittedDirectory = path.join(staging, "emit");
    const inputHashes = new Map();
    function record(filename) {
        const absolute = path.resolve(filename);
        if (inside(staging, absolute)) return;
        inputHashes.set(absolute.split(path.sep).join("/"), hashBytes(fs.readFileSync(absolute)));
    }
    function recordCompiler(directory) {
        for (const item of fs.readdirSync(directory, { withFileTypes: true })) {
            const filename = path.join(directory, item.name);
            if (item.isDirectory()) recordCompiler(filename);
            else if (item.isFile() && /\.(?:js|cjs|json)$/.test(item.name)) record(filename);
        }
    }
    recordCompiler(compilerRoot);
    record(compilerExecutable);
    record(path.resolve(path.dirname(compilerExecutable), "../package.json"));
    api = new API({
        cwd: settings,
        fs: {
            readFile(filename) {
                try {
                    const contents = fs.readFileSync(filename, "utf8");
                    record(filename);
                    return contents;
                } catch (error) {
                    if (error.code === "ENOENT" || error.code === "ENOTDIR") return null;
                    throw error;
                }
            },
        },
    });
    const configPath = path.join(settings, "tsconfig.json");
    const parsed = api.parseConfigFile(configPath);
    const engineDeclarations = path.resolve(
        path.dirname(fileURLToPath(import.meta.url)),
        "../include/engine.d.ts",
    );
    const fileNames = parsed.fileNames.filter(
        (filename) =>
            !(
                !inside(project, filename) &&
                path.basename(filename) === "engine.d.ts" &&
                path.basename(path.dirname(filename)) === "include"
            ),
    );
    fileNames.push(engineDeclarations);
    if (fileNames.some((filename) => !filename.endsWith(".ts")))
        throw new Error("Only .ts TypeScript inputs are supported");
    let snippet;
    if (snippetMode) {
        const request = JSON.parse(fs.readFileSync(args[3], "utf8"));
        if (
            !request ||
            typeof request !== "object" ||
            Array.isArray(request) ||
            Object.keys(request).some((name) => name !== "sourcePath" && name !== "source") ||
            typeof request.sourcePath !== "string" ||
            typeof request.source !== "string"
        )
            throw new Error("Snippet request requires sourcePath and source strings");
        const name = request.sourcePath;
        if (
            !name.endsWith(".ts") ||
            name.endsWith(".d.ts") ||
            name.includes("\\") ||
            name.includes(":") ||
            name.includes("\0") ||
            name.split("/").some((part) => !part || part === "." || part === "..")
        )
            throw new Error("Snippet sourcePath must be an assets-relative .ts path");
        if (Buffer.byteLength(request.source, "utf8") > 256 * 1024)
            throw new Error("Snippet source exceeds 256 KiB");
        // 不写 assets。rootDirs 让临时源沿用 sourcePath 对应的实际项目相对导入。
        const temporaryRoot = path.join(staging, "source");
        snippet = {
            ...request,
            filename: path.join(assets, name),
            // TS API 使用 / 分隔路径，片段身份比较及导入图键须保持相同格式。
            temporary: path.join(temporaryRoot, name).split(path.sep).join("/"),
            temporaryRoot,
        };
        fs.mkdirSync(path.dirname(snippet.temporary), { recursive: true });
        fs.writeFileSync(snippet.temporary, snippet.source);
    }
    const options = {
        target: "ES2022",
        module: snippet ? "CommonJS" : "ES2022",
        rootDir: snippet ? project : assets,
        outDir: emittedDirectory,
        noEmitOnError: true,
        incremental: false,
        composite: false,
        declaration: false,
        emitDeclarationOnly: false,
        noEmit: false,
        outFile: null,
        declarationDir: null,
        tsBuildInfoFile: null,
        sourceMap: false,
        inlineSourceMap: !snippet,
        inlineSources: !snippet,
        sourceRoot: null,
        mapRoot: null,
        allowJs: false,
        ...(snippet
            ? {
                  moduleResolution: "Bundler",
                  rootDirs: [assets, snippet.temporaryRoot],
                  verbatimModuleSyntax: false,
                  resolvePackageJsonExports: false,
                  resolvePackageJsonImports: false,
                  customConditions: null,
              }
            : {}),
    };
    const compilerConfig = path.join(staging, "tsconfig.json");
    fs.writeFileSync(
        compilerConfig,
        JSON.stringify({
            extends: configPath,
            files: snippet
                ? [...fileNames.filter((name) => name.endsWith(".d.ts")), snippet.temporary]
                : fileNames,
            include: [],
            exclude: [],
            compilerOptions: options,
        }),
    );
    const snapshot = api.updateSnapshot({ openProjects: [compilerConfig] });
    snapshots.push(snapshot);
    let sourceProject = snapshot.getProject(compilerConfig);
    if (!sourceProject) throw new Error("TypeScript did not load the project configuration");
    let program = sourceProject.program;
    function walk(node, visitor) {
        visitor(node);
        node.forEachChild((child) => {
            walk(child, visitor);
        });
    }
    function isString(node) {
        return (
            node &&
            (node.kind === ts.SyntaxKind.StringLiteral ||
                node.kind === ts.SyntaxKind.NoSubstitutionTemplateLiteral)
        );
    }
    let sourceFiles = program
        .getSourceFileNames()
        .filter((name) => !program.getSourceFile(name).isDeclarationFile);
    const resolvedImports = new Map();
    const staticRequires = new Set();
    for (const filename of sourceFiles) {
        const source = program.getSourceFile(filename);
        const imports = new Map();
        walk(source, (node) => {
            const dynamic =
                ts.isCallExpression(node) && node.expression.kind === ts.SyntaxKind.ImportKeyword;
            if (snippet && filename === snippet.temporary && dynamic)
                throw new Error("Dynamic import is not supported in a project snippet");
            let specifier;
            if (ts.isImportDeclaration(node) || ts.isExportDeclaration(node))
                specifier = node.moduleSpecifier;
            else if (
                ts.isImportEqualsDeclaration(node) &&
                ts.isExternalModuleReference(node.moduleReference)
            )
                specifier = node.moduleReference.expression;
            else if (
                dynamic ||
                (ts.isCallExpression(node) &&
                    ts.isIdentifier(node.expression) &&
                    node.expression.text === "require")
            )
                specifier = node.arguments[0];
            if (!isString(specifier)) return;
            if (
                snippet &&
                filename === snippet.temporary &&
                ts.isCallExpression(node) &&
                !dynamic &&
                (specifier.text.startsWith("./") || specifier.text.startsWith("../"))
            )
                staticRequires.add(specifier.text);
            const symbol = sourceProject.checker.getSymbolAtLocation(specifier);
            for (const declaration of symbol?.declarations ?? []) {
                const resolved = declaration.resolve(sourceProject)?.getSourceFile().fileName;
                if (resolved) {
                    imports.set(specifier.text, resolved);
                    break;
                }
            }
        });
        resolvedImports.set(filename, imports);
    }
    if (staticRequires.size !== 0) {
        // 声明过 require 的 TS 调用不进入普通 import 图。用原生编译器的类型查询解析，
        // 避免另写路径搜索器；探针只在 cache 中存在，不出现在返回的运行时代码中。
        const probe = snippet.temporary + ".require-probe.ts";
        fs.writeFileSync(
            probe,
            [...staticRequires]
                .map(
                    (specifier, index) =>
                        `export type Require${index} = typeof import(${JSON.stringify(specifier)});`,
                )
                .join("\n"),
        );
        const definition = JSON.parse(fs.readFileSync(compilerConfig, "utf8"));
        definition.files.push(probe);
        fs.writeFileSync(compilerConfig, JSON.stringify(definition));
        const requireSnapshot = api.updateSnapshot({
            fileChanges: { changed: [compilerConfig], created: [probe] },
        });
        snapshots.push(requireSnapshot);
        sourceProject = requireSnapshot.getProject(compilerConfig);
        if (!sourceProject) throw new Error("TypeScript did not resolve snippet require imports");
        program = sourceProject.program;
        sourceFiles = program
            .getSourceFileNames()
            .filter((name) => !program.getSourceFile(name).isDeclarationFile);
        walk(program.getSourceFile(probe), (node) => {
            if (!ts.isImportTypeNode(node) || !ts.isLiteralTypeNode(node.argument)) return;
            const literal = node.argument.literal;
            if (!isString(literal)) return;
            const symbol = sourceProject.checker.getSymbolAtLocation(literal);
            for (const declaration of symbol?.declarations ?? []) {
                const resolved = declaration.resolve(sourceProject)?.getSourceFile().fileName;
                if (resolved) {
                    resolvedImports.get(snippet.temporary).set(literal.text, resolved);
                    break;
                }
            }
        });
    }
    let compilerResult;
    try {
        compilerResult = execFileSync(
            compilerExecutable,
            ["--project", compilerConfig, "--pretty", "false"],
            { cwd: settings, encoding: "utf8", maxBuffer: 16 * 1024 * 1024 },
        );
    } catch (error) {
        throw new Error(String(error.stdout || error.stderr || error.message));
    }
    if (compilerResult.trim()) console.error(compilerResult.trim());
    const compiledOutputs = [];
    for (const filename of sourceFiles) {
        const relative = path.relative(options.rootDir, filename).slice(0, -3) + ".js";
        const output = path.resolve(emittedDirectory, relative);
        if (!inside(emittedDirectory, output) || !fs.existsSync(output))
            throw new Error(`Missing or unsupported script output: ${filename}`);
        if (!snippet || filename === snippet.temporary)
            compiledOutputs.push({
                filename: output,
                text: fs.readFileSync(output, "utf8"),
                source: filename,
            });
    }
    const javascriptConfig = path.join(staging, "jsconfig.json");
    fs.writeFileSync(
        javascriptConfig,
        JSON.stringify({
            files: compiledOutputs.map((entry) => entry.filename),
            compilerOptions: {
                allowJs: true,
                noEmit: true,
                noLib: true,
                types: [],
                module: "ES2022",
                moduleResolution: "Bundler",
            },
        }),
    );
    const javascriptSnapshot = api.updateSnapshot({ openProjects: [javascriptConfig] });
    snapshots.push(javascriptSnapshot);
    const javascriptProject = javascriptSnapshot.getProject(javascriptConfig);
    if (!javascriptProject) throw new Error("TypeScript did not load emitted JavaScript");
    const modules = [];
    for (const entry of compiledOutputs) {
        const syntax = javascriptProject.program.getSourceFile(entry.filename);
        const imports = new Set();
        walk(syntax, (node) => {
            let specifier;
            if (snippet) {
                if (
                    !ts.isCallExpression(node) ||
                    !ts.isIdentifier(node.expression) ||
                    node.expression.text !== "require"
                )
                    return;
                specifier = node.arguments[0];
                if (node.arguments.length !== 1 || !isString(specifier))
                    throw new Error("Snippet imports require a static module path");
            } else if (ts.isImportDeclaration(node) || ts.isExportDeclaration(node))
                specifier = node.moduleSpecifier;
            else if (
                ts.isCallExpression(node) &&
                node.expression.kind === ts.SyntaxKind.ImportKeyword
            )
                specifier = node.arguments[0];
            if (isString(specifier)) imports.add(specifier.text);
        });
        const dependencies = new Set();
        for (const specifier of imports) {
            if (specifier === "engine") continue;
            if (!specifier.startsWith("./") && !specifier.startsWith("../"))
                throw new Error(`Unsupported runtime module import: ${specifier}`);
            if (snippet && !specifier.endsWith(".js"))
                throw new Error(`Runtime imports must use an explicit .js path: ${specifier}`);
            const resolved = resolvedImports.get(entry.source)?.get(specifier);
            if (!resolved || !resolved.endsWith(".ts") || resolved.endsWith(".d.ts"))
                throw new Error(`Runtime module has no emitted source: ${specifier}`);
            if (!inside(assets, resolved))
                throw new Error(`Runtime module is outside project assets: ${specifier}`);
            if (
                snippet &&
                path.resolve(path.dirname(snippet.filename), specifier) !==
                    path.resolve(resolved.slice(0, -3) + ".js")
            )
                throw new Error(
                    `Runtime import does not match its emitted module path: ${specifier}`,
                );
            dependencies.add(path.relative(assets, resolved).split(path.sep).join("/"));
        }
        modules.push({
            source: path.relative(assets, entry.source).split(path.sep).join("/"),
            dependencies: [...dependencies].sort(),
        });
    }
    if (snippet) {
        if (compiledOutputs.length !== 1 || !compiledOutputs[0].filename.endsWith(".js"))
            throw new Error("Snippet must emit exactly one JavaScript module");
        // TS7 的 CommonJS emit 始终启用 ESM interop。真实 ESM namespace 已经包含
        // default 与活绑定，只需向标准 helper 暴露 __esModule 标记，不能再包一层 default。
        // Proxy 不修改原 namespace，保留导出属性/活绑定，且同次片段执行复用身份。
        const javascript = `(function(require) {
${compiledOutputs[0].text}
})((function(load) {
    const namespaces = new WeakMap();
    return function(name) {
        const value = load(name);
        if (!value || value[Symbol.toStringTag] !== "Module") return value;
        let namespace = namespaces.get(value);
        if (!namespace) {
            namespace = new Proxy(value, {
                get(target, key) {
                    return key === "__esModule" ? true : Reflect.get(target, key);
                }
            });
            namespaces.set(value, namespace);
        }
        return namespace;
    };
})(require));
`;
        if (Buffer.byteLength(javascript, "utf8") > 256 * 1024)
            throw new Error("Compiled snippet exceeds 256 KiB");
        process.stdout.write(
            JSON.stringify({ modulePath: snippet.sourcePath.slice(0, -3) + ".js", javascript }) +
                "\n",
        );
    } else {
        modules.sort((left, right) => left.source.localeCompare(right.source));
        const inputs = [...inputHashes]
            .sort(([left], [right]) => (left < right ? -1 : left > right ? 1 : 0))
            .map(([filename, hash]) => ({ path: filename, hash }));
        if (inputsOnly) process.stdout.write(JSON.stringify({ inputs, modules }) + "\n");
        else {
            const output = path.resolve(args[3]);
            if (fs.existsSync(output)) {
                requireDirectory(output);
                if (fs.readdirSync(output).length !== 0)
                    throw new Error("Script staging output must be empty");
            } else fs.mkdirSync(output, { recursive: true });
            const outputs = compiledOutputs.map((entry) => ({
                source: path.relative(assets, entry.source).split(path.sep).join("/"),
                output: path.relative(emittedDirectory, entry.filename).split(path.sep).join("/"),
            }));
            fs.writeFileSync(
                path.join(emittedDirectory, ".script-build.json"),
                JSON.stringify({ outputs, inputs, modules }, null, 2) + "\n",
            );
            for (const item of fs.readdirSync(emittedDirectory))
                fs.renameSync(path.join(emittedDirectory, item), path.join(output, item));
            console.error("TypeScript staging build completed");
        }
    }
} catch (error) {
    console.error(`Script build failed: ${error.message}`);
    process.exitCode = 1;
} finally {
    for (const snapshot of snapshots.reverse()) snapshot.dispose();
    api?.close();
    if (staging) fs.rmSync(staging, { recursive: true, force: true });
}
