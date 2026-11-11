import Minimal from "./modules/minimal.js";
import { Debug, Storage } from "engine";

// 在公开范例的副本中作为 scripts/game.ts 使用，验证真实业务到持久事务的完整链路。
export default class StorageContract extends Minimal {
    private saved = false;

    override OnInitialize(): boolean {
        const previous = Storage.GetString("web-storage-contract") ?? "0";
        Debug.Log(`WEB_STORAGE_BEFORE:${previous}`);
        const next = String(Number(previous) + 1);
        this.saved = Storage.SetString("web-storage-contract", next) && Storage.Save();
        Debug.Log(`WEB_STORAGE_SAVE:${this.saved}:${next}:${Storage.lastError ?? ""}`);
        return super.OnInitialize();
    }

    override OnShutdown(): void {
        if (this.saved) Debug.Log(`WEB_STORAGE_SHUTDOWN_SAVE:${Storage.Save()}`);
        super.OnShutdown();
    }
}
