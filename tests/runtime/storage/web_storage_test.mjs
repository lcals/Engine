import assert from "node:assert/strict";
import { test } from "node:test";
import * as storage from "../../../platforms/web/storage/storage.mjs";

function fixture() {
    const files = new Map();
    const syncs = [];
    const events = [];
    const fs = {
        mkdirTree() {}, mount() {},
        syncfs(populate, callback) { syncs.push({ populate, callback }); },
        analyzePath(path) { return { exists: files.has(path) }; },
        readFile(path) { return files.get(path).bytes; },
        writeFile(path, bytes) { files.set(path, { bytes: Uint8Array.from(bytes),
            mode: 0o100600, atime: new Date(100), mtime: new Date(100) }); },
        stat(path) { return { ...files.get(path) }; },
        rename(from, to) { assert.ok(files.has(from)); files.set(to, files.get(from)); files.delete(from); },
        chmod(path, mode) { files.get(path).mode = mode; },
        utime(path, atime, mtime) { Object.assign(files.get(path), { atime: new Date(atime), mtime: new Date(mtime) }); },
        unlink(path) { files.delete(path); }
    };
    const module = { FS: fs, IDBFS: { quit() { events.push("quit"); } },
        _engine_web_storage_open: () => 1,
        _engine_web_storage_close: () => events.push("close") };
    return { fs, files, syncs, module, events };
}
const tick = () => new Promise(resolve => setImmediate(resolve));
const root = "/persistent/web-test";

async function open(f) {
    const restored = storage.restore(f.module, root);
    const initial = f.syncs.shift();
    assert.equal(initial.populate, true);
    initial.callback(null);
    await restored;
}

async function close(f) {
    const closed = storage.close();
    await tick();
    f.syncs.shift().callback(null);
    await closed;
}

test("Save completes only after the persistent transaction; close waits for Save", async () => {
    const f = fixture();
    await open(f);
    f.fs.writeFile(root + "/temporary", [2]);
    let completed = false;
    const saved = storage.publish(root + "/temporary", root + "/values");
    saved.then(() => { completed = true; });
    await tick();
    assert.equal(completed, false);
    const transaction = f.syncs.shift();
    const closed = storage.close();
    assert.equal(storage.close(), closed);
    await assert.rejects(storage.publish(root + "/other", root + "/values"), /closed/);
    assert.deepEqual(f.events, []);
    transaction.callback(null);
    await saved; await tick();
    assert.equal(completed, true);
    assert.deepEqual(f.events, []);
    f.syncs.shift().callback(null);
    await closed;
    assert.deepEqual(f.events, ["close", "quit"]);
});

test("failed transactions restore the old view and cannot be flushed as successful saves", async () => {
    const f = fixture();
    await open(f);
    f.fs.writeFile(root + "/values", [1]);
    f.fs.writeFile(root + "/temporary", [2]);
    const saved = storage.publish(root + "/temporary", root + "/values");
    const rejected = assert.rejects(saved, /Quota/);
    await tick();
    f.syncs.shift().callback(new Error("Quota exceeded"));
    await rejected;
    assert.deepEqual([...f.fs.readFile(root + "/values")], [1]);
    f.fs.writeFile(root + "/temporary", [3]);
    const initial = storage.publish(root + "/temporary", root + "/new-values");
    const initialRejected = assert.rejects(initial, /Quota/);
    await tick(); f.syncs.shift().callback(new Error("Quota exceeded"));
    await initialRejected;
    assert.equal(f.files.has(root + "/new-values"), false);
    await close(f);
    assert.deepEqual([...f.fs.readFile(root + "/values")], [1]);
});

test("multiple saves in one clock millisecond remain visible to IDBFS", async () => {
    const f = fixture();
    await open(f);
    const clock = Date.now;
    Date.now = () => 1000;
    try {
        f.fs.writeFile(root + "/values", [0]);
        f.fs.utime(root + "/values", 1000, 1000);
        for (const next of [1, 2]) {
            f.fs.writeFile(root + "/temporary", [next]);
            const saved = storage.publish(root + "/temporary", root + "/values");
            await tick();
            assert.equal(f.fs.stat(root + "/values").mtime.getTime(), 1000 + next);
            f.syncs.shift().callback(null);
            await saved;
        }
    } finally {
        Date.now = clock;
        await close(f);
    }
});
