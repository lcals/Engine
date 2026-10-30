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

    /** Current renderer settings. Calls require an active Gameplay callback and renderer. */
    export class Render {
        private constructor();
        /** Linear RGB in [0, 1], intensity in [0, 1000]. Invalid calls leave settings unchanged. */
        static SetAmbient(r: number, g: number, b: number, intensity: number): boolean;
        /** Exposure in (0, 100]. */
        static SetExposure(value: number): boolean;
        static SetFilmicTonemap(enabled: boolean): boolean;
        /** Resolution defaults to 2048; bias defaults to 0.001. */
        static SetDirectionalShadows(enabled: boolean, resolution?: number, bias?: number): boolean;
        /** Unsigned 32-bit 0xRRGGBBAA; MCP uses a normalized [r,g,b,a] array in the same order. */
        static SetClearColor(rgba: number): boolean;
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
        SetTexture(name: string, path: string): boolean;
        GetTexture(name: string): string | null;
    }

    /** One active scene, owned by the current Gameplay run. */
    export class Scene {
        private constructor();
        static Create(name: string): Scene | null;
        /** Load a compiled scene from the active GUID resource catalog. */
        static Load(path: string): Scene | null;
        /** First node with the requested name in scene order. */
        FindNode(name: string): Node | null;
        IsValid(): boolean;
        CreateNode(name: string): Node | null;
        /** Instantiate a model package as ordinary nodes; failure leaves the scene unchanged. */
        InstantiateModel(path: string, name: string): Node | null;
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
    export interface ComponentType<T extends Component> {
        readonly prototype: T;
    }

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
        /** Size is the vertical half-height in world units. */
        SetOrthographic(size: number, nearClip: number, farClip: number): boolean;
    }
    export class Light extends Component {
        private constructor();
        SetColor(r: number, g: number, b: number): boolean;
        SetIntensity(intensity: number): boolean;
        SetDirectional(): boolean;
        SetPoint(range: number): boolean;
        /** Cone half-angles in degrees. */
        SetSpot(range: number, innerAngle: number, outerAngle: number): boolean;
    }
    export class MeshRenderer extends Component {
        private constructor();
        readonly material: Material;
        readonly materialCount: number;
        SetMesh(path: string): boolean;
        GetMaterial(slot: number): Material | null;
        SetMaterialCount(count: number): boolean;
    }
    /** An independent animation action sampled onto the MeshRenderer on the same node. */
    export class Animator extends Component {
        private constructor();
        /** Empty path clears the action; failure preserves the previous action and settings. */
        SetAnimation(path: string): boolean;
        /** Playback multiplier in [0, 16]; zero freezes time. */
        SetSpeed(value: number): boolean;
        SetLoop(value: boolean): boolean;
        SetPlayOnAwake(value: boolean): boolean;
        Play(): boolean;
        /** Retain the last sampled pose. */
        Pause(): boolean;
        /** Restore the source Mesh and reset time to zero. */
        Stop(): boolean;
        /** Seconds in [0, duration]; seeking from stopped enters a retained paused pose. */
        Seek(seconds: number): boolean;
        readonly isPlaying: boolean;
        readonly playback: "stopped" | "playing" | "paused" | null;
        readonly time: number | null;
    }
    export class Collider extends Component {
        private constructor();
        /** Local half-extents of a box. */
        SetBox(x: number, y: number, z: number): boolean;
        SetSphere(radius: number): boolean;
        /** Half-height is measured along the cylinder part, excluding the hemispheres. */
        SetCapsule(radius: number, halfHeight: number): boolean;
        SetFriction(value: number): boolean;
        SetRestitution(value: number): boolean;
        SetTrigger(value: boolean): boolean;
    }
    export class RigidBody extends Component {
        private constructor();
        SetMotion(motion: "static" | "kinematic" | "dynamic"): boolean;
        SetMass(value: number): boolean;
        /** World-space linear velocity. */
        SetVelocity(x: number, y: number, z: number): boolean;
        GetVelocity(): { readonly x: number; readonly y: number; readonly z: number } | null;
        /** A world-space impulse consumed by the next physics synchronization. */
        AddImpulse(x: number, y: number, z: number): boolean;
    }
    export class CharacterMotor extends Component {
        private constructor();
        /** Capsule origin is at its feet; halfHeight excludes the hemispheres. */
        SetCapsule(radius: number, halfHeight: number): boolean;
        SetSlopeLimit(degrees: number): boolean;
        SetStepHeight(value: number): boolean;
        SetGroundSnapDistance(value: number): boolean;
        SetGravityFactor(value: number): boolean;
        /** Desired world-space horizontal velocity; physics supplies the vertical component. */
        SetVelocity(x: number, z: number): boolean;
        /** Last physics velocity, not the pending movement request. */
        GetVelocity(): { readonly x: number; readonly y: number; readonly z: number } | null;
        /** Accepted only while grounded, consumed at the next physics step. */
        Jump(speed: number): boolean;
        readonly grounded: boolean;
    }
    export class AudioSource extends Component {
        private constructor();
        /** Empty path clears the current clip; failure preserves the previous clip. */
        SetClip(path: string): boolean;
        SetVolume(value: number): boolean;
        SetPitch(value: number): boolean;
        SetLoop(value: boolean): boolean;
        SetSpatial(value: boolean): boolean;
        SetPlayOnAwake(value: boolean): boolean;
        SetDistances(minimum: number, maximum: number): boolean;
        /** True means the request was accepted, not that audio output has started. */
        Play(): boolean;
        Pause(): boolean;
        Stop(): boolean;
        readonly isPlaying: boolean;
        readonly playback: "stopped" | "pending" | "playing" | "paused" | "failed" | null;
    }
    export class AudioListener extends Component {
        private constructor();
        SetVolume(value: number): boolean;
    }
}
