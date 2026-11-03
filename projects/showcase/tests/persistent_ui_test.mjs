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
const output = await mkdtemp(path.join(tmpdir(), "engine-showcase-persistent-ui-"));

try {
    execFileSync(process.execPath, [
        path.join(project, "settings/node_modules/typescript/lib/tsc.js"),
        "--project", path.join(project, "settings/tsconfig.json"),
        "--outDir", output, "--sourceMap", "false",
    ], { stdio: "inherit" });
    await writeFile(path.join(output, "package.json"), '{"type":"module"}\n');
    const engineDirectory = path.join(output, "node_modules/engine");
    await mkdir(engineDirectory, { recursive: true });
    await writeFile(path.join(engineDirectory, "package.json"),
        '{"type":"module","exports":"./index.js"}\n');
    await writeFile(path.join(engineDirectory, "index.js"), engineStub);
    const load = (relative) => import(pathToFileURL(path.join(output, relative)).href);
    const { fixture } = await load("node_modules/engine/index.js");
    const { default: PersistentUi } = await load("scripts/modules/persistent-ui.js");
    const markup = await readFile(path.join(project, "assets/ui/persistent-ui.rml"), "utf8");
    fixture.ids = [...markup.matchAll(/\bid="([^"]+)"/g)].map((match) => match[1]);

    fixture.reset();
    const game = new PersistentUi();
    assert.equal(game.OnInitialize(), true);
    const originalRoot = fixture.nodes.find((node) => node.name === "Persistent loading UI");
    const originalView = originalRoot.view;
    const note = originalView.FindElement("lifetime-note");
    const identity = originalView.FindElement("lifetime-identity").text;
    const counts = originalView.FindElement("lifetime-counts");
    const status = originalView.FindElement("lifetime-status");
    const subscriptions = () => [...originalView.elements.values()].flatMap((element) => element.listeners).length;
    assert.equal(subscriptions(), 7, "six commands and one text-input listener");
    let events = 0;
    let commits = 0;
    let noteValue = "";
    const preserved = () => {
        assert.equal(originalRoot.IsValid(), true);
        assert.equal(note.isValid, true);
        assert.equal(fixture.element("lifetime-note"), note, "the original element handle survives");
        assert.equal(note.value, noteValue);
        assert.equal(originalView.FindElement("lifetime-identity").text, identity);
        assert.equal(subscriptions(), 7, "loading does not duplicate callbacks");
        assert.equal(counts.text, `Scene commits: ${commits} / UI events: ${events}`);
    };
    const edit = (value) => {
        noteValue = value;
        fixture.input("lifetime-note", value);
        ++events;
        game.Update();
        preserved();
    };
    const command = (id) => {
        fixture.click(id);
        ++events;
        game.Update();
    };

    edit("同一个输入框 / original text");
    command("load-a");
    ++commits;
    assert.equal(fixture.scene.path, "scenes/main.scene");
    preserved();
    command("load-b");
    ++commits;
    assert.equal(fixture.scene.path, "scenes/font-gallery.scene");
    preserved();
    edit("after two scene commits");
    console.log("PASS project input callback, text and original UI identity survive two different scene commits");

    fixture.loadStatus = "loading";
    command("load-a");
    assert.equal(fixture.pendingLoad, "scenes/main.scene");
    assert.match(status.text, /Loading 1 \/ 2/);
    edit("editing while resources are loading");
    fixture.loadStatus = "ready";
    game.Update();
    ++commits;
    preserved();
    assert.equal(fixture.pendingLoad, null);
    console.log("PASS pending resource loading keeps input responsive and commits only when ready");

    fixture.loadStatus = "loading";
    command("load-b");
    command("cancel");
    assert.equal(fixture.pendingLoad, null);
    assert.match(status.text, /canceled/);
    preserved();
    command("fail");
    assert.match(status.text, /Missing resource rejected/);
    preserved();
    fixture.loadStatus = "failed";
    const cancellations = fixture.cancellations;
    command("load-b");
    assert.equal(fixture.pendingLoad, null);
    assert.ok(fixture.cancellations > cancellations);
    assert.match(status.text, /fixture load failure/);
    preserved();
    fixture.loadStatus = "ready";
    fixture.failCommit = true;
    command("load-b");
    assert.equal(fixture.pendingLoad, null);
    assert.match(status.text, /Commit rejected/);
    preserved();
    fixture.failCommit = false;
    command("unload");
    assert.equal(fixture.scene, null);
    assert.equal(game.scene, null);
    preserved();
    edit("still interactive with no gameplay scene");
    command("load-b");
    ++commits;
    preserved();
    console.log("PASS cancellation, missing resource, failed loading, rejected commit and unload retain a usable UI");

    command("destroy");
    assert.equal(originalRoot.IsValid(), false);
    assert.equal(note.isValid, false);
    assert.equal(subscriptions(), 0);
    assert.throws(() => fixture.input("lifetime-note", "stale input"), /Unavailable input/);
    game.OnShutdown();
    game.OnShutdown();
    assert.equal(subscriptions(), 0);
    console.log("PASS explicit destruction invalidates original handles and releases callbacks exactly once");

    fixture.reset();
    fixture.failDocument = true;
    const partial = new PersistentUi();
    assert.equal(partial.OnInitialize(), false);
    const partialRoot = fixture.nodes.find((node) => node.name === "Persistent loading UI");
    assert.equal(partialRoot.persistent, true, "failure happened after persistence was enabled");
    partial.OnShutdown();
    partial.OnShutdown();
    assert.equal(partialRoot.IsValid(), false);
    assert.equal([...partialRoot.view.elements.values()].flatMap((element) => element.listeners).length, 0);
    console.log("PASS failed document initialization releases the partially created persistent node");
} finally {
    await rm(output, { recursive: true, force: true });
}
