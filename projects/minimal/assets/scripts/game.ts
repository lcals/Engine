import { App, Debug, Gameplay } from "engine";

export default class Game extends Gameplay {
    override OnInitialize(): boolean {
        Debug.Log("Game.OnInitialize: editor script runtime ready");
        Debug.Log(`Built-in assets: ${App.builtinAssetsPath}`);
        Debug.Log(`Persistent data: ${App.persistentDataPath}`);
        Debug.Log(`Temporary cache: ${App.cachePath}`);
        Debug.LogWarning("Game: warning binding test");
        Debug.LogError("Game: error binding test (intentional)");
        return true;
    }

    override Update(): void {
    }

    override OnShutdown(): void {
        Debug.Log("Game.OnShutdown");
    }
}
