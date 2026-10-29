/** Native engine module. This module is supplied by ScriptSystem. */
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

    /** Seconds of the current Update; zero during initialization and shutdown. */
    export class Time {
        private constructor();
        static readonly deltaTime: number;
    }

    export interface Color { r: number; g: number; b: number; a: number; }
    export interface Material {
        baseColor: Color;
        metallic: number;
        roughness: number;
        unlit: boolean;
    }

    /** One active scene, owned by the current Gameplay run. */
    export class Scene {
        private constructor();
        static Create(name: string): Scene | null;
        IsValid(): boolean;
        CreateNode(name: string): Node | null;
        SetActiveCamera(camera: Camera | null): boolean;
        Destroy(): boolean;
    }

    export class Node {
        private constructor();
        IsValid(): boolean;
        Destroy(): boolean;
        SetParent(parent: Node | null): boolean;
        SetPosition(x: number, y: number, z: number): boolean;
        /** Local Euler angles in degrees, applied X then Y then Z. */
        SetRotation(x: number, y: number, z: number): boolean;
        SetScale(x: number, y: number, z: number): boolean;
        AddCamera(): Camera | null;
        AddLight(): Light | null;
        /** The first implementation uses a built-in cube mesh. */
        AddMeshRenderer(): MeshRenderer | null;
    }

    export class Component {
        protected constructor();
        readonly node: Node;
        IsValid(): boolean;
        SetEnabled(enabled: boolean): boolean;
    }
    export class Camera extends Component {
        private constructor();
        SetPerspective(fovDegrees: number, nearClip: number, farClip: number): boolean;
    }
    export class Light extends Component {
        private constructor();
        SetColor(r: number, g: number, b: number): boolean;
        SetIntensity(intensity: number): boolean;
    }
    export class MeshRenderer extends Component {
        private constructor();
        SetMaterial(material: Material): boolean;
    }

}
