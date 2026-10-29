/** Native desktop engine module. This module is supplied by ScriptSystem. */
declare module "engine" {
    /** One project entry instance, independent of nodes and scenes. */
    export class Gameplay {
        /** Return true to start gameplay; false reports startup failure. */
        OnInitialize(): boolean;
        Update(): void;
        /** Also called after a partially completed OnInitialize. */
        OnShutdown(): void;
    }

    export class Debug {
        private constructor();
        static Log(message: string): void;
        static LogWarning(message: string): void;
        static LogError(message: string): void;
    }

    export class App {
        private constructor();
        static readonly builtinAssetsPath: string;
        static readonly persistentDataPath: string;
        static readonly cachePath: string;
    }
}
