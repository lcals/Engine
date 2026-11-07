// Explicit resource acquisitions for the project facade tests. Native lifecycle
// and dependency validation are covered by the C++ resource tests.
export const resourceStub = String.raw`
const acquisitions = new WeakMap();
const loadedPaths = new Map();
export const Resources = {
    lastError: "fixture load failure",
    Load(path) {
        if (typeof path !== 'string' || !path || path.includes('missing')) return null;
        const handle = Object.freeze({});
        acquisitions.set(handle, path);
        loadedPaths.set(path, (loadedPaths.get(path) ?? 0) + 1);
        return handle;
    },
    Retain(handle) { const path = acquisitions.get(handle); return path ? this.Load(path) : null; },
    Release(handle) {
        const path = acquisitions.get(handle);
        if (!path) return false;
        acquisitions.delete(handle);
        const count = loadedPaths.get(path) - 1;
        if (count) loadedPaths.set(path, count); else loadedPaths.delete(path);
        return true;
    },
    BeginLoad(path) {
        if (typeof path !== 'string' || path.includes('missing')) return null;
        fixture.pendingResource = path;
        let terminal = null;
        return {
            Status() { return terminal ?? fixture.loadStatus; },
            Progress() { return {completed: 1, total: 2}; },
            Error() { return this.Status() === 'failed' ? 'fixture load failure' : ''; },
            Take() {
                if (this.Status() !== 'ready') return null;
                terminal = 'delivered'; fixture.pendingResource = null;
                return Resources.Load(path);
            },
            Cancel() {
                if (terminal) return false;
                terminal = 'cancelled'; fixture.pendingResource = null;
                ++fixture.cancellations; return true;
            },
        };
    },
};
`;
