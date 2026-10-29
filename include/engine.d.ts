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

    /** Resource paths start with builtin/ or project/. Parameters match the selected shader. */
    export class Material {
        private constructor();
        IsValid(): boolean;
        SetType(path: string): boolean;
        SetShader(path: string): boolean;
        SetFloat(name: string, value: number): boolean;
        SetV2(name: string, x: number, y: number): boolean;
        SetV3(name: string, x: number, y: number, z: number): boolean;
        SetV4(name: string, x: number, y: number, z: number, w: number): boolean;
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
        AddComponent<T extends Component>(type: ComponentType<T>): T | null;
        GetComponent<T extends Component>(type: ComponentType<T>): T | null;
    }

    /** A registered engine component class; constructors are not publicly callable. */
    export interface ComponentType<T extends Component> { readonly prototype: T; }

    export class Component {
        protected constructor();
        readonly node: Node;
        IsValid(): boolean;
        SetEnabled(enabled: boolean): boolean;
    }
    /** Every scene node starts with one Transform component. */
    export class Transform extends Component {
        private constructor();
        SetPosition(x: number, y: number, z: number): boolean;
        /** Local Euler angles in degrees, applied X then Y then Z. */
        SetRotation(x: number, y: number, z: number): boolean;
        SetScale(x: number, y: number, z: number): boolean;
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
        readonly material: Material;
    }

}
