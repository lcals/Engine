import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import vm from "node:vm";

const source = await readFile(new URL("../../../platforms/web/bootstrap.mjs", import.meta.url), "utf8");
const tick = () => new Promise(resolve => setImmediate(resolve));

async function fixture() {
    const events = [];
    let callbacks, completeFactory, completeStorage;
    class Canvas { id = ""; transferControlToOffscreen() {} }
    const module = {
        FS: { mkdirTree() {}, writeFile() {} },
        callMain() { events.push("main"); },
        _engine_web_request_exit() { events.push("exit-request"); },
        PThread: { terminateAllThreads() { events.push("terminate"); } }
    };
    const context = vm.createContext({ URL, DOMException, AbortController, SharedArrayBuffer,
        HTMLCanvasElement: Canvas, navigator: { gpu: {} }, isSecureContext: true,
        crossOriginIsolated: true, console,
        fetch: async () => ({ ok: true, arrayBuffer: async () => new ArrayBuffer(8) }) });
    const exports = {
        "./engine.mjs": { default: options => {
            callbacks = options;
            return new Promise(resolve => { completeFactory = () => resolve(module); });
        } },
        "./catalog.mjs": { catalogCarriers: () => new Map() },
        "./application.mjs": { applicationName: "web-test" },
        "./storage/storage.mjs": {
            restore: async () => { events.push("restore"); },
            close: () => new Promise(resolve => {
                events.push("storage-close"); completeStorage = resolve;
            })
        }
    };
    const program = new vm.SourceTextModule(source, { context });
    await program.link(name => {
        const values = exports[name];
        return new vm.SyntheticModule(Object.keys(values), function() {
            for (const [key, value] of Object.entries(values)) this.setExport(key, value);
        }, { context });
    });
    await program.evaluate();
    return { api: program.namespace, events, Canvas, base: new URL("https://example.test/game/"),
        ready: () => callbacks.onEngineReady({ submissionId: "1", sceneId: "2" }),
        stopped: (code = 0) => callbacks.onEngineStopped(code, code ? "Device lost" : ""),
        factory: () => completeFactory(), storage: () => completeStorage(),
        callback: () => callbacks,
        onExit: (state, error) => events.push([state, error]) };
}

test("readiness waits for a scene receipt; shutdown drains native and storage before workers", async () => {
    const f = await fixture();
    const canvas = new f.Canvas();
    const started = f.api.start(canvas, f.base, f.onExit);
    let ready = false;
    started.then(() => { ready = true; });
    await tick(); f.factory(); await tick();
    assert.deepEqual(f.events, ["restore", "main"]);
    assert.equal(ready, false);
    await assert.rejects(f.api.start(new f.Canvas(), f.base, f.onExit), { name: "InvalidStateError" });
    f.ready(); await started;
    const stopped = f.api.stop();
    assert.equal(f.api.stop(), stopped);
    await tick();
    assert.deepEqual(f.events, ["restore", "main", "exit-request"]);
    f.stopped(); await tick();
    assert.equal(f.events.includes("terminate"), false);
    f.storage(); await stopped;
    assert.deepEqual(f.events.slice(-3), ["storage-close", "terminate", ["stopped", ""]]);
    await assert.rejects(f.api.start(canvas, f.base, f.onExit), /fresh canvas/);
});

test("cancelling an unresolved factory waits for it and emits one terminal event", async () => {
    const f = await fixture();
    const controller = new AbortController();
    const started = f.api.start(new f.Canvas(), f.base, f.onExit, controller.signal);
    const rejected = assert.rejects(started, { name: "AbortError" });
    await tick(); controller.abort();
    const stopped = f.api.stop();
    await tick(); assert.deepEqual(f.events, []);
    f.factory(); await stopped; await rejected;
    assert.deepEqual(f.events, ["terminate", ["stopped", ""]]);
    f.ready(); f.stopped(); await tick();
    assert.equal(f.events.filter(Array.isArray).length, 1);
});

test("faults after readiness reach on_exit and repeated stop retains the failure", async () => {
    const f = await fixture();
    const started = f.api.start(new f.Canvas(), f.base, f.onExit);
    await tick(); f.factory(); await tick(); f.ready(); await started;
    f.stopped(1); await tick();
    const failed = f.api.stop();
    const rejected = assert.rejects(failed, /Device lost/);
    f.storage(); await rejected;
    assert.equal(f.api.stop(), failed);
    assert.deepEqual(f.events.at(-1), ["failed", "Device lost"]);
});

test("startup abort during native execution still performs native shutdown", async () => {
    const f = await fixture();
    const started = f.api.start(new f.Canvas(), f.base, f.onExit);
    const rejected = assert.rejects(started, { name: "AbortError" });
    await tick(); f.factory(); await tick();
    const stopped = f.api.stop();
    await tick(); f.ready();
    assert.equal(f.events.at(-1), "exit-request");
    f.stopped(); await tick(); f.storage(); await stopped; await rejected;
    assert.equal(f.events.filter(Array.isArray).length, 1);
});
