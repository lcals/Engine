import { ByteBuffer } from "flatbuffers";
import { AssetCatalogData } from "./engine/schema/asset-catalog-data";

export function catalogCarriers(bytes: Uint8Array): Map<string, number> {
    const buffer = new ByteBuffer(bytes);
    if (bytes.length < 8 || bytes.length > 16 * 1024 * 1024 ||
        !AssetCatalogData.bufferHasIdentifier(buffer)) throw new Error("Invalid resource catalog");
    const catalog = AssetCatalogData.getRootAsAssetCatalogData(buffer);
    if (catalog.schemaVersion() !== 2 || catalog.targetPlatform() !== "web" || !catalog.revision()) {
        throw new Error("Resource catalog must use the current schema and Web target");
    }
    if (catalog.entriesLength() > 65536) throw new Error("Resource catalog is too large");
    const files = new Map<string, number>();
    for (let index = 0; index < catalog.entriesLength(); index++) {
        const location = catalog.entries(index)?.location();
        const path = location?.path();
        if (!location || location.mount() !== "content" || !path ||
            !/^(gen|packs)\/[a-zA-Z0-9_./-]+$/.test(path) ||
            path.split("/").some(part => !part || part === "." || part === "..")) {
            throw new Error("Resource carrier path is invalid");
        }
        const end = location.offset() + location.length();
        if (end > BigInt(Number.MAX_SAFE_INTEGER)) throw new Error("Resource carrier is too large");
        files.set(path, Math.max(files.get(path) || 0, Number(end)));
    }
    return files;
}
