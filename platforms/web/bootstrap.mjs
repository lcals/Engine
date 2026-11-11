import createEngineModule from "./engine.mjs";
import { catalogCarriers } from "./catalog.mjs";
import * as storage from "./storage/storage.mjs";
import { applicationName } from "./application.mjs";

let current = null;
let generation = 0;
let terminalStop = Promise.resolve();
const transferred = new WeakSet();

function deferred() {
    let resolve, reject;
    const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
    return { promise, resolve, reject };
}
const cancelled = () => new DOMException("Runtime startup was cancelled", "AbortError");

function check(run) {
    if (current !== run || run.cancelled) throw cancelled();
    if (run.fault) throw run.fault;
}

async function download(run, relative, minimum = 0) {
    const response = await fetch(new URL(relative, run.base), { signal: run.network.signal });
    if (!response.ok) throw new Error("Cannot download " + relative + ": HTTP " + response.status);
    const bytes = new Uint8Array(await response.arrayBuffer());
    check(run);
    if (bytes.length < minimum) throw new Error("Incomplete resource carrier: " + relative);
    return bytes;
}

function write(module, path, bytes) {
    module.FS.mkdirTree(path.slice(0, path.lastIndexOf("/")));
    module.FS.writeFile(path, bytes, { canOwn: true });
}

async function prepare(run) {
    check(run);
    const module = await createEngineModule({ canvas: run.canvas, noInitialRun: true,
        locateFile: path => new URL(path, run.base).href,
        print: text => console.log(text), printErr: text => console.error(text),
        onEngineReady: receipt => {
            if (current !== run || run.cancelled || run.fault || run.phase !== "starting") return;
            if (!receipt?.submissionId || !receipt?.sceneId) {
                run.fault = new Error("Runtime returned an invalid first-frame receipt");
                void finish(run).catch(() => {});
                return;
            }
            run.phase = "running";
            run.startSettled = true;
            run.start.resolve();
        },
        onEngineStopped: (code, error) => {
            if (current !== run) return;
            if (code && !run.fault) run.fault = new Error(error || "Native runtime failed");
            run.nativeStopped.resolve();
            void finish(run).catch(() => {});
        },
        onAbort: error => {
            if (current !== run) return;
            run.fault ||= new Error("WebAssembly runtime aborted: " + String(error));
            run.nativeStopped.resolve();
            void finish(run).catch(() => {});
        }
    });
    run.module = module;
    check(run);
    const [catalog, settings] = await Promise.all([
        download(run, "assets/catalog.bin", 8), download(run, "settings/runtime.json", 2)
    ]);
    const entries = [...catalogCarriers(catalog)];
    let next = 0;
    await Promise.all(Array.from({ length: Math.min(entries.length, 4) }, async () => {
        for (;;) {
            const entry = entries[next++];
            if (!entry) return;
            const [path, minimum] = entry;
            const bytes = await download(run, "assets/" + path, minimum);
            write(module, "/assets/" + path, bytes);
        }
    }));
    check(run);
    write(module, "/assets/catalog.bin", catalog);
    write(module, "/settings/runtime.json", settings);
    await storage.restore(module, "/persistent/" + applicationName);
    run.storageReady = true;
    check(run);
    transferred.add(run.canvas);
    run.enteredNative = true;
    module.callMain([]);
}

function finish(run) {
    if (run.stop) return run.stop;
    run.phase = "stopping";
    run.cancelled = true;
    run.network.abort();
    run.stop = (async () => {
        try {
            await run.preparation.catch(error => {
                if (error?.name !== "AbortError") run.fault ||= error;
            });
            if (run.module && run.enteredNative) {
                run.module._engine_web_request_exit();
                await run.nativeStopped.promise;
            }
            if (run.storageReady) await storage.close();
        } catch (error) {
            run.fault ||= error;
        } finally {
            run.module?.PThread?.terminateAllThreads();
            run.module = null;
            run.signal?.removeEventListener("abort", run.abort);
        }
        if (!run.startSettled) {
            run.startSettled = true;
            run.start.reject(run.fault || cancelled());
        }
        run.phase = run.fault ? "failed" : "stopped";
        if (current === run) current = null;
        try { run.onExit(run.fault ? "failed" : "stopped", run.fault?.message || ""); }
        catch (error) { console.error("Runtime exit callback failed", error); }
        if (run.fault) throw run.fault;
    })();
    terminalStop = run.stop;
    run.stop.catch(() => {});
    return run.stop;
}

export function start(canvas, base_url, on_exit, signal) {
    if (current) return Promise.reject(new DOMException("Runtime is already active", "InvalidStateError"));
    if (signal?.aborted) return Promise.reject(cancelled());
    if (!(canvas instanceof HTMLCanvasElement) || !(base_url instanceof URL) ||
        typeof on_exit !== "function" || transferred.has(canvas) ||
        (canvas.id && !/^[a-zA-Z][a-zA-Z0-9_-]*$/.test(canvas.id))) {
        return Promise.reject(new TypeError("Startup requires a fresh canvas, URL and exit callback"));
    }
    if (!/^https?:$/.test(base_url.protocol) || !base_url.pathname.endsWith("/") ||
        base_url.search || base_url.hash) {
        return Promise.reject(new TypeError("The runtime base URL must identify an HTTP directory"));
    }
    if (!globalThis.isSecureContext || !globalThis.crossOriginIsolated || !navigator.gpu ||
        typeof SharedArrayBuffer !== "function" || !canvas.transferControlToOffscreen) {
        return Promise.reject(new Error("WebGPU requires a secure, cross-origin isolated browser with OffscreenCanvas"));
    }
    if (!/^[a-z][a-z0-9_-]*$/.test(applicationName)) {
        return Promise.reject(new Error("The exported application identity is invalid"));
    }
    const run = { generation: ++generation, phase: "starting", canvas, base: base_url,
        onExit: on_exit, signal, network: new AbortController(), module: null,
        nativeStopped: deferred(), start: deferred(), startSettled: false,
        enteredNative: false, storageReady: false, cancelled: false, fault: null, stop: null };
    if (!canvas.id) canvas.id = "engine-canvas-" + run.generation;
    current = run;
    terminalStop = Promise.resolve();
    run.abort = () => { void finish(run).catch(() => {}); };
    signal?.addEventListener("abort", run.abort, { once: true });
    run.preparation = Promise.resolve().then(() => prepare(run));
    run.preparation.catch(error => {
        if (error?.name !== "AbortError") run.fault ||= error;
        void finish(run).catch(() => {});
    });
    return run.start.promise;
}

export function stop() {
    return current ? finish(current) : terminalStop;
}
