let active = null;

function sync(module, populate) {
    return new Promise((resolve, reject) => {
        module.FS.syncfs(populate, error => error ? reject(error) : resolve());
    });
}

function plainFile(state, path) {
    if (typeof path !== "string" || !path.startsWith(state.root + "/") ||
        path.includes("\\") || path.includes("\0") ||
        path.split("/").some(part => part === "." || part === "..")) {
        throw new Error("Persistent file path is invalid");
    }
}

export async function restore(module, root) {
    if (active) throw new Error("Persistent storage is already open");
    const state = { module, root, tail: Promise.resolve(), closing: null, poison: "", open: false };
    active = state;
    try {
        module.FS.mkdirTree(root);
        module.FS.mount(module.IDBFS, { autoPersist: false }, root);
        await sync(module, true);
        if (!module._engine_web_storage_open()) throw new Error("Cannot open the storage proxy");
        state.open = true;
        module.webStorage = { publish };
    } catch (error) {
        module.IDBFS.quit();
        active = null;
        throw error;
    }
}

export function publish(temporary, target) {
    const state = active;
    if (!state || !state.open || state.closing || state.poison) {
        return Promise.reject(new Error(state?.poison || "Persistent storage is closed"));
    }
    const operation = state.tail.then(async () => {
        if (state.poison) throw new Error(state.poison);
        plainFile(state, temporary);
        plainFile(state, target);
        if (temporary === target || temporary.slice(0, temporary.lastIndexOf("/")) !==
            target.slice(0, target.lastIndexOf("/"))) {
            throw new Error("Persistent publishing requires separate files in one directory");
        }
        const fs = state.module.FS;
        const exists = fs.analyzePath(target).exists;
        const prior = exists ? { bytes: fs.readFile(target).slice(), info: fs.stat(target) } : null;
        fs.rename(temporary, target);
        try {
            const time = Math.max(Date.now(), (prior?.info.mtime.getTime() || 0) + 1);
            fs.utime(target, time, time);
            await sync(state.module, false);
        } catch (error) {
            try {
                if (prior) {
                    fs.writeFile(target, prior.bytes);
                    fs.chmod(target, prior.info.mode);
                    fs.utime(target, prior.info.atime, prior.info.mtime);
                } else if (fs.analyzePath(target).exists) {
                    fs.unlink(target);
                }
            } catch (rollback) {
                state.poison = "Persistent rollback failed: " + String(rollback);
            }
            throw error;
        }
    });
    state.tail = operation.catch(() => {});
    return operation;
}

export function close() {
    const state = active;
    if (!state) return Promise.resolve();
    if (state.closing) return state.closing;
    state.closing = (async () => {
        try {
            await state.tail;
            if (state.poison) throw new Error(state.poison);
            await sync(state.module, false);
        } finally {
            state.open = false;
            state.module.webStorage = null;
            state.module._engine_web_storage_close();
            state.module.IDBFS.quit();
            active = null;
        }
    })();
    return state.closing;
}
