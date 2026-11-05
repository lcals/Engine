import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtemp, mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { engineStub } from "./lifecycle_engine.mjs";

const root = path.resolve(
    process.argv[2] ?? path.join(path.dirname(fileURLToPath(import.meta.url)), "../../.."),
);
const project = path.join(root, "projects/showcase");
const output = await mkdtemp(path.join(tmpdir(), "engine-showcase-contract-"));

try {
    execFileSync(process.execPath, [
        path.join(project, "settings/typescript/node_modules/typescript/lib/tsc.js"),
        "--project", path.join(project, "settings/typescript/tsconfig.json"),
        "--outDir", output, "--sourceMap", "false",
    ], { stdio: "inherit" });
    await writeFile(path.join(output, "package.json"), '{"type":"module"}\n');
    const engineDirectory = path.join(output, "node_modules/engine");
    await mkdir(engineDirectory, { recursive: true });
    await writeFile(path.join(engineDirectory, "package.json"),
        '{"type":"module","exports":"./index.js"}\n');
    await writeFile(path.join(engineDirectory, "index.js"), engineStub);
    const load = (relative) => import(pathToFileURL(path.join(output, relative)).href);
    const { fixture, Scene } = await load("node_modules/engine/index.js");
    const { showcase_cases } = await load("scripts/models/showcase_cases.js");
    const { LauncherModel } = await load("scripts/models/launcher_model.js");
    const { LauncherController } = await load("scripts/controllers/launcher_controller.js");

    const markup = await readFile(path.join(project, "assets/ui/launcher.rml"), "utf8");
    fixture.ids = [...markup.matchAll(/\bid="([^"]+)"/g)].map((match) => match[1]);
    assert.equal(new Set(fixture.ids).size, fixture.ids.length, "document IDs must be unique");
    assert.ok(showcase_cases.length > 0, "the launcher contains functional cases");
    for (const field of ["id", "button", "scene"]) {
        const values = showcase_cases.map((choice) => choice[field]);
        assert.ok(values.every((value) => typeof value === "string" && value.length > 0));
        assert.equal(new Set(values).size, values.length, `case ${field} must be unique`);
    }
    const build = await readFile(path.join(project, "settings/editor-resource-build.toml"), "utf8");
    const rootsSection = build.match(/\broots\s*=\s*\[([^\]]*)\]/);
    assert.ok(rootsSection, "the project declares runtime roots");
    const roots = new Set([...rootsSection[1].matchAll(/"([^"]+)"/g)].map((match) => match[1]));
    const guids = new Set();
    const loadScene = Scene.Load;

    for (const choice of showcase_cases) {
        assert.ok(choice.title.length > 0 && choice.goal.length > 0, `${choice.id} describes its purpose`);
        const scene = await readFile(path.join(project, "assets", choice.scene), "utf8");
        assert.ok(scene.length > 0, `${choice.id} has a saved scene`);
        const meta = await readFile(path.join(project, "assets", choice.scene + ".meta"), "utf8");
        const guid = meta.match(/^guid\s*=\s*"([0-9a-f-]{36})"/m)?.[1];
        assert.ok(guid && !guids.has(guid), `${choice.id} retains a distinct scene GUID`);
        guids.add(guid);
        assert.ok(roots.has(choice.scene), `${choice.id} is included in the runtime package`);

        // 执行实际 Gameplay 初始化，宿主拒绝加载；观察真正请求的路径，不解析 TS 源码。
        const requests = [];
        Scene.Load = (scenePath) => { requests.push(scenePath); return null; };
        try {
            const game = new choice.game();
            assert.equal(game.OnInitialize(), false, `${choice.id} reports a rejected scene load`);
            assert.deepEqual(requests, [choice.scene], `${choice.id} loads its declared scene`);
        } finally {
            Scene.Load = loadScene;
        }
    }
    console.log(`PASS ${showcase_cases.length} real gameplay entries match saved scenes, GUIDs and package roots`);

    fixture.reset();
    const view = Scene.Load("scenes/launcher.scene").view;
    const model = new LauncherModel();
    assert.equal(model.choices, showcase_cases, "the menu consumes the project case list directly");
    const controller = new LauncherController(view, model);
    assert.equal(controller.Bind(), true);
    for (const choice of showcase_cases) {
        assert.equal(view.FindElement(`${choice.button}-title`).text, choice.title);
        assert.equal(view.FindElement(`${choice.button}-goal`).text, choice.goal);
        assert.equal(view.FindElement(choice.button).text, `Test ${choice.title}`);
        fixture.click(choice.button);
        assert.equal(model.pending, choice, `${choice.id} is selected by its actual controller`);
        assert.ok(model.choices.every((item) => !view.FindElement(item.button).enabled));
        model.pending = null;
        assert.equal(controller.Refresh(), true);
    }
    controller.Unbind();
    assert.equal([...view.elements.values()].flatMap((element) => element.listeners).length, 0);
    console.log("PASS menu labels, pending selection, disabled controls and subscriptions follow the single case list");
} finally {
    await rm(output, { recursive: true, force: true });
}
