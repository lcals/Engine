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
        static readonly assetsPath: string;
        static readonly persistentDataPath: string;
        static readonly cachePath: string;
        /** Requests normal host shutdown at a frame boundary. Active callbacks only. */
        static RequestQuit(): boolean;
    }

    export type WindowMode = "windowed" | "borderless" | "fullscreen";
    export type MouseMode = "normal" | "hidden" | "relative";
    export interface WindowSize {
        readonly logicalWidth: number;
        readonly logicalHeight: number;
        readonly pixelWidth: number;
        readonly pixelHeight: number;
    }
    /** SDL mode dimensions; output pixels are width/height multiplied by pixelDensity. */
    export interface DisplayMode {
        readonly width: number;
        readonly height: number;
        readonly pixelDensity: number;
        readonly refreshRate: number;
    }
    /** Existing native window; all operations require an active Run callback. */
    export class Window {
        private constructor();
        static GetSize(): WindowSize | null;
        static GetDisplayModes(): DisplayMode[] | null;
        static GetMode(): WindowMode | null;
        /** Logical window units, only in windowed mode. Query the actual size after events. */
        static SetSize(logicalWidth: number, logicalHeight: number): boolean;
        /** Fullscreen requires a supported mode; other modes require null (the default). */
        static SetMode(mode: WindowMode, fullscreen?: DisplayMode | null): boolean;
        static GetMouseMode(): MouseMode | null;
        /** SDL relative mode hides/grabs the cursor; Run cleanup restores the previous mode. */
        static SetMouseMode(mode: MouseMode): boolean;
    }

    /** Standard catalog resource, parsed into a fresh JS value. No filesystem access. */
    export class Json {
        private constructor();
        /** Active callbacks only; missing/wrong-kind/invalid resources return null.
         * JSON null also returns null. Validate the unknown result in project code. */
        static Load(path: string): unknown;
    }

    /** Per-run string storage. Methods require an active gameplay or component callback. */
    export class Storage {
        private constructor();
        /** Missing keys return null with an empty lastError; read failures also return null. */
        static GetString(key: string): string | null;
        static SetString(key: string, value: string): boolean;
        static HasKey(key: string): boolean;
        static DeleteKey(key: string): boolean;
        /** Explicitly persists changes. Stopping or reloading does not save automatically. */
        static Save(): boolean;
        static readonly isReady: boolean;
        /** Empty when there is no error; null outside an active callback. */
        static readonly lastError: string | null;
    }

    export enum InputDeviceKind {
        keyboard,
        mouse,
        gamepad,
        joystick,
        touch,
    }
    export enum InputControlKind {
        key,
        mouse_button,
        mouse_position,
        mouse_delta,
        mouse_relative,
        mouse_wheel,
        gamepad_button,
        gamepad_axis,
        joystick_button,
        joystick_axis,
        joystick_hat,
        touch,
        gamepad_touch,
    }
    export enum InputActionKind {
        button,
        axis1d,
        axis2d,
    }
    export enum InputValueMode {
        state,
        delta,
    }
    export enum InputCombine {
        maximum,
        sum_clamped,
    }
    export enum InputActivation {
        wait_for_neutral,
        use_current_state,
    }
    export enum InputVirtualKind {
        button,
        stick,
        drag,
    }
    export enum InputCaptureStatus {
        waiting,
        completed,
        canceled,
    }
    export enum InputUnit {
        none,
        normalized,
        logical,
        relative,
        wheel,
    }
    export enum InputRegionShape {
        rectangle,
        circle,
    }

    export interface InputFrameState {
        readonly frame_id: bigint;
        readonly device_revision: bigint;
        readonly cancel_revision: bigint;
    }
    /** Session handle; not a persistent player or device identifier. */
    export interface InputChannelHandle {
        readonly slot: number;
        readonly generation: number;
    }
    export interface InputCaptureHandle {
        readonly slot: number;
        readonly generation: number;
    }
    export interface InputControlRef {
        readonly connection_id: bigint;
        readonly kind: InputControlKind;
        readonly code: number;
    }
    export interface InputDeviceData {
        readonly connection_id: bigint;
        readonly kind: InputDeviceKind;
        readonly name: string;
        readonly layout_key: string;
        readonly capabilities: bigint;
    }
    export interface InputControlInfo {
        readonly source: InputControlRef;
        readonly name: string;
        readonly value_mode: InputValueMode;
        readonly unit: InputUnit;
        readonly minimum: number;
        readonly maximum: number;
    }
    export interface InputControlState {
        readonly source: InputControlRef;
        readonly frame_id: bigint;
        readonly value: number;
        readonly held: boolean;
        readonly pressed: boolean;
        readonly released: boolean;
        readonly canceled: boolean;
        readonly repeat: boolean;
    }
    export interface InputActionState {
        readonly frame_id: bigint;
        readonly value_x: number;
        readonly value_y: number;
        readonly held: boolean;
        readonly pressed: boolean;
        readonly released: boolean;
        readonly canceled: boolean;
    }
    export interface InputContactState {
        readonly source: InputControlRef;
        readonly contact_id: bigint;
        readonly normalized_x: number;
        readonly normalized_y: number;
        readonly logical_x: number;
        readonly logical_y: number;
        readonly pressure: number;
        readonly held: boolean;
    }
    /** Each part selects one physical or virtual scalar and contributes to the binding vector. */
    export interface InputBindingPartData {
        readonly source?: InputControlRef;
        readonly virtual_control?: string;
        readonly virtual_axis?: number;
        readonly weight_x?: number;
        readonly weight_y?: number;
    }
    export interface InputBindingData {
        readonly parts: readonly InputBindingPartData[];
        readonly modifiers?: readonly InputControlRef[];
        readonly scale?: number;
        readonly deadzone?: number;
        readonly press_threshold?: number;
        readonly release_threshold?: number;
        readonly normalize?: boolean;
    }
    export interface InputActionData {
        readonly name: string;
        readonly kind?: InputActionKind;
        readonly value_mode?: InputValueMode;
        readonly combine?: InputCombine;
        readonly bindings: readonly InputBindingData[];
    }
    export interface InputRegionData {
        readonly connection_id: bigint;
        readonly x?: number;
        readonly y?: number;
        readonly width: number;
        readonly height: number;
        readonly anchor_x?: number;
        readonly anchor_y?: number;
        readonly shape?: InputRegionShape;
        readonly floating?: boolean;
        readonly deadzone?: number;
        readonly cancel_on_exit?: boolean;
    }
    export interface InputVirtualControlData {
        readonly id: string;
        readonly kind?: InputVirtualKind;
        readonly priority?: number;
        readonly enabled?: boolean;
        readonly visible?: boolean;
        readonly region: InputRegionData;
    }
    export interface InputChannelData {
        readonly enabled?: boolean;
        readonly priority?: number;
        readonly consume_inputs?: boolean;
        readonly activation?: InputActivation;
        readonly actions: readonly InputActionData[];
        readonly virtual_controls?: readonly InputVirtualControlData[];
    }
    export interface InputCaptureOptions {
        readonly devices?: readonly bigint[];
        readonly kinds?: readonly InputControlKind[];
        readonly threshold?: number;
        readonly neutral_threshold?: number;
        readonly include_modifiers?: boolean;
    }
    export interface InputCaptureState {
        readonly status: InputCaptureStatus;
        readonly frame_id: bigint;
        readonly candidates: readonly InputControlRef[];
    }
    /** Device facts and explicitly submitted mappings; requires an active Run callback. */
    export class Input {
        private constructor();
        static GetFrameState(): InputFrameState | null;
        static GetDevices(): InputDeviceData[] | null;
        static GetControls(device: bigint): InputControlInfo[] | null;
        static ReadControl(control: InputControlRef): InputControlState | null;
        static GetContacts(): InputContactState[] | null;
        static CreateChannel(): InputChannelHandle | null;
        /** Accepts a complete mapping; activation occurs at the next input frame. */
        static ConfigureChannel(channel: InputChannelHandle, data: InputChannelData): boolean;
        static SetChannelEnabled(
            channel: InputChannelHandle,
            enabled: boolean,
            activation: InputActivation,
        ): boolean;
        static DestroyChannel(channel: InputChannelHandle): boolean;
        /** Returns a non-consuming copy of the last published action snapshot. */
        static ReadAction(channel: InputChannelHandle, name: string): InputActionState | null;
        static BeginCapture(
            channel: InputChannelHandle,
            options: InputCaptureOptions,
        ): InputCaptureHandle | null;
        static ReadCapture(capture: InputCaptureHandle): InputCaptureState | null;
        static CancelCapture(capture: InputCaptureHandle): boolean;
        static readonly lastError: string | null;
    }

    /** Frame scheduling. Methods require an active gameplay or component callback. */
    export class Engine {
        private constructor();
        /** -1 removes the cap; positive int32 values set its maximum. Invalid calls return false. */
        static SetTargetFrameRate(fps: number): boolean;
        /** The current cap, or null outside an active callback. */
        static GetTargetFrameRate(): number | null;
    }

    /** Seconds of the current Update; zero during initialization and shutdown. */
    export class Time {
        private constructor();
        static readonly deltaTime: number;
    }

    /** Scene parameter facade. Requires an active callback and existing scene components. */
    export class Render {
        private constructor();
        /** Current render target size in pixels; null without an initialized renderer. */
        static GetViewportSize(): Vector2 | null;
        /** Accepted requests are applied through bgfx at the next drawable frame. */
        static SetVSync(enabled: boolean): boolean;
        /** Last accepted request, not a measurement of presentation timing. */
        static GetVSync(): boolean | null;
        /** Linear RGB in [0, 1], intensity in [0, 1000]. Invalid calls leave settings unchanged. */
        static SetAmbient(r: number, g: number, b: number, intensity: number): boolean;
        /** Enables three-color ambient light. SetAmbient switches back to a constant color. */
        static SetAmbientTricolor(
            sky: readonly [number, number, number],
            equator: readonly [number, number, number],
            ground: readonly [number, number, number],
            intensity: number,
        ): boolean;
        /** Linear RGB and world distances. Requires 0 <= start < end, even when disabled. */
        static SetLinearFog(
            enabled: boolean,
            r: number,
            g: number,
            b: number,
            start: number,
            end: number,
        ): boolean;
        /** Nonnegative density, forward camera depth. squared defaults to false. */
        static SetExponentialFog(
            enabled: boolean,
            r: number,
            g: number,
            b: number,
            density: number,
            squared?: boolean,
        ): boolean;
        /** Exposure in (0, 100]. */
        static SetExposure(value: number): boolean;
        static SetFilmicTonemap(enabled: boolean): boolean;
        /** HDR defaults off. Disable Bloom before disabling HDR; failure preserves all settings. */
        static SetHdr(enabled: boolean): boolean;
        /** Requires HDR when enabled; preserves HDR. Defaults: threshold 1, intensity 0.15. */
        /** Threshold [0,100], intensity [0,10], radius [0.25,4] (default 1). */
        static SetBloom(
            enabled: boolean,
            threshold?: number,
            intensity?: number,
            radius?: number,
        ): boolean;
        /** AO defaults: radius 1, strength 1, power 1.5, fade 80..150 world units. */
        /** Radius (0,100], strength [0,2], power [0.1,8], 0 <= fadeStart < fadeEnd <= 100000. */
        static SetAmbientOcclusion(
            enabled: boolean,
            radius?: number,
            strength?: number,
            power?: number,
            fadeStart?: number,
            fadeEnd?: number,
        ): boolean;
        /** Full atlas resolution defaults to 2048; bias to 0.001. Preserves cascade settings. */
        static SetDirectionalShadows(enabled: boolean, resolution?: number, bias?: number): boolean;
        /** Count: 1/2/4; distance: (0, 100000]. Preserves the shadow enabled state. */
        /** splitLambda: [0,1], default 0.6; blend: [0,0.3], default 0.1. */
        static SetShadowCascades(
            count: number,
            distance: number,
            splitLambda?: number,
            blend?: number,
        ): boolean;
        /** Unsigned 32-bit 0xRRGGBBAA; MCP uses a normalized [r,g,b,a] array in the same order. */
        static SetClearColor(rgba: number): boolean;
    }

    /** Resource paths are relative to assets, or use builtin://. Parameters match the shader. */
    export class Material {
        private constructor();
        IsValid(): boolean;
        SetType(path: string): boolean;
        SetShader(path: string): boolean;
        SetCullMode(mode: "back" | "front" | "none"): boolean;
        GetCullMode(): "back" | "front" | "none" | null;
        SetBlendMode(mode: "opaque" | "alpha_blend" | "premultiplied"): boolean;
        GetBlendMode(): "opaque" | "alpha_blend" | "premultiplied" | null;
        SetFloat(name: string, value: number): boolean;
        SetV2(name: string, x: number, y: number): boolean;
        SetV3(name: string, x: number, y: number, z: number): boolean;
        SetV4(name: string, x: number, y: number, z: number, w: number): boolean;
        SetTexture(name: string, path: string): boolean;
        GetTexture(name: string): string | null;
    }

    export interface SceneLoadProgress {
        readonly completed: number;
        readonly total: number;
        readonly status: "loading" | "ready" | "failed";
        readonly error: string;
    }
    /** One active scene, owned by the current Gameplay run. */
    export class Scene {
        private constructor();
        static Create(name: string): Scene | null;
        /** Load a compiled scene from the active GUID resource catalog. */
        static Load(path: string): Scene | null;
        /** Begin one single-thread dependency load. Keeps the current scene/UI intact. */
        static BeginLoad(path: string): boolean;
        /** Budget counts completed resources, not elapsed time; zero only queries. */
        static StepLoad(resourceBudget: number): SceneLoadProgress | null;
        /** Restore/commit a ready scene synchronously. Failure preserves the active scene. */
        static CommitLoad(): Scene | null;
        /** Release this Run's pending load. Cannot interrupt a running step/commit. */
        static CancelLoad(): void;
        /** First node with the requested name in scene order. */
        FindNode(name: string): Node | null;
        IsValid(): boolean;
        /** Freeze automatic world simulation while UI and Gameplay.Update continue. */
        SetPaused(paused: boolean): boolean;
        GetPaused(): boolean | null;
        CreateNode(name: string): Node | null;
        /** Instantiate a model package as ordinary nodes; failure leaves the scene unchanged. */
        InstantiateModel(path: string, name: string): Node | null;
        SetActiveCamera(camera: Camera | null): boolean;
        /** Unloads this active scene, preserving persistent subtrees until gameplay stops. */
        Destroy(): boolean;
    }

    export class Node {
        private constructor();
        /** Stable lifetime identities; use IsValid() before accessing a deleted node. */
        readonly id: bigint;
        readonly sceneId: bigint;
        IsValid(): boolean;
        Destroy(): boolean;
        /** Root nodes only. Retains the subtree across scene unloads within this gameplay Run. */
        SetPersistent(value: boolean): boolean;
        /** True when this node or one of its ancestors is marked persistent. */
        IsPersistent(): boolean;
        SetParent(parent: Node | null): boolean;
        /** Relative child path, e.g. "Assembly/Body". Invalid, missing or ambiguous paths return null. */
        FindChild(path: string): Node | null;
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
        GetWorldPosition(): Vector3 | null;
        /** Normalized world direction of local -Z. */
        GetWorldForward(): Vector3 | null;
    }
    export class Camera extends Component {
        private constructor();
        SetPerspective(fovDegrees: number, nearClip: number, farClip: number): boolean;
        /** Size is the vertical half-height in world units. */
        SetOrthographic(size: number, nearClip: number, farClip: number): boolean;
        /** Viewport-local point and dimensions in matching units; origin is top-left. */
        ScreenPointToRay(x: number, y: number, width: number, height: number): Ray | null;
        /** Top-left normalized coordinates; rays start on the near plane with unit direction. */
        ViewportPointToRay(u: number, v: number, aspect: number): Ray | null;
    }
    /** Scene-wide environment parameters, stored with the scene. */
    export class EnvironmentComponent extends Component {
        private constructor();
        /** Uses the scene's precomputed IBL maps; baking does not change this switch. */
        SetIbl(enabled: boolean, intensity?: number): boolean;
        SetAmbient(r: number, g: number, b: number, intensity: number): boolean;
        SetAmbientTricolor(
            sky: readonly [number, number, number],
            equator: readonly [number, number, number],
            ground: readonly [number, number, number],
            intensity: number,
        ): boolean;
        SetLinearFog(
            enabled: boolean,
            r: number,
            g: number,
            b: number,
            start: number,
            end: number,
        ): boolean;
        SetExponentialFog(
            enabled: boolean,
            r: number,
            g: number,
            b: number,
            density: number,
            squared?: boolean,
        ): boolean;
        /** Unsigned 32-bit 0xRRGGBBAA. */
        SetClearColor(rgba: number): boolean;
    }
    /** Attach to a camera node; the active camera supplies the scene's post-processing. */
    export class PostProcessComponent extends Component {
        private constructor();
        SetExposure(value: number): boolean;
        SetFilmicTonemap(enabled: boolean): boolean;
        /** HDR defaults off. Disable Bloom before disabling HDR; failure preserves all settings. */
        SetHdr(enabled: boolean): boolean;
        /** Requires HDR when enabled; preserves HDR. Defaults: threshold 1, intensity 0.15. */
        /** Threshold [0,100], intensity [0,10], radius [0.25,4] (default 1). */
        SetBloom(
            enabled: boolean,
            threshold?: number,
            intensity?: number,
            radius?: number,
        ): boolean;
        /** Scale [0.25,1], integer steps [8,256], distance (0,100000], thickness (0,10]. */
        SetScreenSpaceReflections(enabled: boolean, resolutionScale?: number, maxSteps?: number,
                                  maxDistance?: number, thickness?: number,
                                  roughnessCutoff?: number): boolean;
        /** AO defaults: radius 1, strength 1, power 1.5, fade 80..150 world units. */
        /** Radius (0,100], strength [0,2], power [0.1,8], 0 <= fadeStart < fadeEnd <= 100000. */
        SetAmbientOcclusion(
            enabled: boolean,
            radius?: number,
            strength?: number,
            power?: number,
            fadeStart?: number,
            fadeEnd?: number,
        ): boolean;
    }
    /** Attach beside MeshRenderer; the node's local XZ plane defines the mirror. */
    export class PlanarReflectionComponent extends Component {
        private constructor();
        SetSettings(resolutionScale?: number, clipBias?: number): boolean;
    }
    export class LightProbeVolumeComponent extends Component {
        private constructor();
        /** Empty path clears the resource; failure preserves the previous binding. */
        SetResource(path: string): boolean;
    }
    export class Light extends Component {
        private constructor();
        SetColor(r: number, g: number, b: number): boolean;
        SetIntensity(intensity: number): boolean;
        SetDirectional(): boolean;
        /** Directional, point and spot shadows; resolution defaults to 2048, bias to 0.001. */
        SetShadows(enabled: boolean, resolution?: number, bias?: number): boolean;
        /** Count: 1/2/4; distance: (0, 100000]. Preserves the shadow enabled state. */
        /** splitLambda: [0,1], default 0.6; blend: [0,0.3], default 0.1. */
        SetShadowCascades(
            count: number,
            distance: number,
            splitLambda?: number,
            blend?: number,
        ): boolean;
        SetPoint(range: number): boolean;
        /** Cone half-angles in degrees. */
        SetSpot(range: number, innerAngle: number, outerAngle: number): boolean;
    }
    export class MeshRenderer extends Component {
        private constructor();
        readonly material: Material;
        readonly materialCount: number;
        readonly castShadows: boolean;
        readonly bakeStatic: boolean;
        readonly lightmapPath: string | null;
        SetBakeStatic(enabled: boolean): boolean;
        /** Requires UV2 and a linear texture. Empty path clears the binding. */
        SetLightmap(path: string): boolean;
        SetCastShadows(enabled: boolean): boolean;
        SetMesh(path: string): boolean;
        GetMaterial(slot: number): Material | null;
        SetMaterialCount(count: number): boolean;
    }
    /** An independent animation action sampled onto the MeshRenderer on the same node. */
    export class Animator extends Component {
        private constructor();
        /** Empty path clears the action; failure preserves the previous action and settings. */
        SetAnimation(path: string): boolean;
        /** Blend from the current pose. Nonnegative seconds; zero switches immediately. */
        CrossFade(path: string, seconds: number): boolean;
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
    /** Available when the optional terrain runtime plugin is enabled. */
    export class TerrainComponent extends Component {
        private constructor();
        /** Sphere center and radius are in terrain-node local space. */
        DigSphere(x: number, y: number, z: number, radius: number): boolean;
        FillSphere(x: number, y: number, z: number, radius: number): boolean;
        /** Accept a queued edit; completed runs on the main thread after publish or cancellation.
         * Scene changes and script shutdown discard callbacks. No callback on rejected requests. */
        DigSphereAsync(
            x: number, y: number, z: number, radius: number,
            completed: (success: boolean) => void,
        ): boolean;
        FillSphereAsync(
            x: number, y: number, z: number, radius: number,
            completed: (success: boolean) => void,
        ): boolean;
        /** Extract unsupported soil; refuses over-budget work without removing that soil. */
        CollapseDetached(
            minSamples: number,
            maxFragments: number,
            fragmentCells?: number,
            density?: number,
        ): boolean;
        readonly fragmentCount: number | null;
        /** Keep at most maxCount dynamic fragments; zero retires all. A zero age disables expiry.
         * Explicit main-thread request; returns retired count, or null for invalid/busy owners. */
        RetireFragments(maxCount: number, maxAgeSeconds: number): number | null;
        Reset(): boolean;
        /** Slot: 1..48 ASCII letters, digits, underscore or hyphen; no filesystem paths. */
        Save(slot: string): boolean;
        Load(slot: string): boolean;
        readonly ready: boolean;
        readonly revision: bigint | null;
        readonly chunkCount: number | null;
        readonly rebuiltChunks: number | null;
        readonly triangleCount: number | null;
        readonly lastError: string | null;
    }
    /** Available when the optional effects runtime plugin is enabled. */
    export class EffectComponent extends Component {
        private constructor();
        SetEffect(path: string): boolean;
        SetLoop(value: boolean): boolean;
        SetPlayOnAwake(value: boolean): boolean;
        SetSpeed(value: number): boolean;
        Play(): boolean;
        Pause(): boolean;
        Stop(): boolean;
        readonly isPlaying: boolean;
    }
    export interface PhysicsImpact {
        readonly frame: bigint;
        readonly a: PhysicsObject;
        readonly b: PhysicsObject;
        readonly point: { readonly x: number; readonly y: number; readonly z: number };
        /** World normal from A to B. */
        readonly normal: { readonly x: number; readonly y: number; readonly z: number };
        /** Pre-solver normal approach speed in m/s. */
        readonly closingSpeed: number;
        /** Jolt's estimated normal impulse, not the actual solver result (N s). */
        readonly estimatedImpulse: number;
    }
    export interface PhysicsContactEvent {
        readonly frame: bigint;
        readonly step: bigint;
        readonly phase: "begin" | "stay" | "end";
        readonly a: PhysicsObject;
        readonly b: PhysicsObject;
        /** End events retain the last contact point and normal. */
        readonly point: Vector3;
        /** World normal from a to b. */
        readonly normal: Vector3;
        readonly isTrigger: boolean;
    }
    /** Contact delivered to a subscribed node before Gameplay.Update; all values are snapshots. */
    export interface PhysicsContact {
        readonly frame: bigint;
        readonly step: bigint;
        readonly phase: "begin" | "stay" | "end";
        readonly self: PhysicsObject;
        /** May be invalid on Exit after the other component was disabled or removed. */
        readonly other: PhysicsObject;
        readonly point: Vector3;
        /** World normal from self to other; Exit retains the last contact geometry. */
        readonly normal: Vector3;
        readonly isTrigger: boolean;
    }
    export class Physics {
        private constructor();
        /** Last physics tick for this Run's live objects, including persistent nodes. */
        static GetImpacts(): PhysicsImpact[];
        /** Last physics tick, ordered by fixed step; read once per frame, not per event. */
        static GetContacts(): readonly PhysicsContactEvent[];
        /**
         * Subscribe to future contacts of this node's active Collider or CharacterMotor instance.
         * Returns an ID, or 0 on failure. Callbacks must be synchronous; Off cancels immediately.
         * Disable/removal/replacement of the receiver and run shutdown cancel it.
         * Persistent receivers keep subscriptions across scene changes.
         * Subscriptions added inside callbacks start with the next physics batch.
         */
        static OnCollisionEnter(node: Node, callback: (contact: PhysicsContact) => void): number;
        static OnCollisionStay(node: Node, callback: (contact: PhysicsContact) => void): number;
        static OnCollisionExit(node: Node, callback: (contact: PhysicsContact) => void): number;
        static OnTriggerEnter(node: Node, callback: (contact: PhysicsContact) => void): number;
        static OnTriggerStay(node: Node, callback: (contact: PhysicsContact) => void): number;
        static OnTriggerExit(node: Node, callback: (contact: PhysicsContact) => void): number;
        /** Repeated cancellation or an unknown ID returns false. */
        static Off(subscription: number): boolean;
        /** All overlapping objects, deduplicated. Null = invalid query; [] = no overlap. */
        static OverlapSphere(
            center: Vector3,
            radius: number,
            options?: PhysicsQueryOptions,
        ): readonly PhysicsObject[] | null;
        /** World-axis-aligned box with positive half extents; same filter as Raycast. */
        static OverlapBox(
            center: Vector3,
            halfExtents: Vector3,
            options?: PhysicsQueryOptions,
        ): readonly PhysicsObject[] | null;
        /** Check component identity and enablement. Historical end-event objects may be invalid. */
        static IsObjectValid(object: PhysicsObject): boolean;
        /** Sweeps a sphere through the full displacement. 1 = no hit; null = invalid query. */
        static CastSphere(
            x: number,
            y: number,
            z: number,
            dx: number,
            dy: number,
            dz: number,
            radius: number,
        ): number | null;
        /** Closest Collider or CharacterMotor hit through a nonzero world displacement. Null = invalid query. */
        static Raycast(
            origin: Vector3,
            displacement: Vector3,
            options?: PhysicsQueryOptions,
        ): PhysicsQueryHit | null;
        /** Radius must be positive. Hit distance is sphere-center travel; point is on the target. */
        static SphereCast(
            origin: Vector3,
            displacement: Vector3,
            radius: number,
            options?: PhysicsQueryOptions,
        ): PhysicsQueryHit | null;
        /** Checks physics object lifetime/enablement, not whether the snapshot moved. */
        static IsHitValid(hit: PhysicsQueryHit): boolean;
    }
    export interface Vector2 {
        readonly x: number;
        readonly y: number;
    }
    export interface Vector3 {
        readonly x: number;
        readonly y: number;
        readonly z: number;
    }
    export interface Ray {
        readonly origin: Vector3;
        readonly direction: Vector3;
    }
    export interface PhysicsQueryOptions {
        readonly ignoreNode?: Node | null;
        /** Defaults to true when ignoring a node. */
        readonly ignoreDescendants?: boolean;
        readonly includeStatic?: boolean;
        readonly includeKinematic?: boolean;
        readonly includeDynamic?: boolean;
        /** Defaults to false; other body categories default to true. */
        readonly includeSensors?: boolean;
        /** Defaults to true, independently of includeKinematic. */
        readonly includeCharacters?: boolean;
    }
    export interface PhysicsObject {
        readonly sceneId: bigint;
        readonly nodeId: bigint;
        readonly kind: "collider" | "character";
        readonly instanceId: bigint;
        readonly node: Node | null;
    }
    export interface PhysicsQueryHit extends PhysicsObject {
        readonly hit: boolean;
        readonly point: Vector3;
        readonly normal: Vector3;
        readonly fraction: number;
        readonly distance: number;
    }
    export interface NavigationOptions {
        readonly minimum: Vector3;
        readonly maximum: Vector3;
        readonly cellSize?: number;
        readonly cellHeight?: number;
        readonly tileSize?: number;
        readonly agentHeight?: number;
        readonly agentRadius?: number;
        readonly agentMaxClimb?: number;
        readonly agentMaxSlope?: number;
    }
    export interface NavigationAgentOptions {
        readonly radius?: number;
        readonly height?: number;
        readonly maxSpeed?: number;
        readonly maxAcceleration?: number;
        readonly separationWeight?: number;
        readonly arrivalDistance?: number;
        readonly faceVelocity?: boolean;
    }
    export type NavigationAgentStatus =
        | "idle"
        | "pending"
        | "moving"
        | "arrived"
        | "unreachable"
        | "invalid";
    /** Available when the optional navigation_crowd runtime plugin is enabled. */
    export class NavigationAgent extends Component {
        private constructor();
        /** Atomically replace configuration; omitted fields use schema defaults. */
        Configure(options: NavigationAgentOptions): boolean;
        /** Submit a world-space destination; true does not mean the agent has arrived. */
        MoveTo(target: Vector3): boolean;
        /** Cancel this agent's active movement request. */
        Stop(): boolean;
        /** All methods validate this component instance, including IsValid and SetEnabled. */
        readonly state: NavigationAgentStatus | null;
        readonly velocity: Vector3 | null;
    }
    export class Navigation {
        private constructor();
        /** Submit explicit same-scene static mesh nodes; does not scan the whole scene. */
        static Build(nodes: readonly Node[], options: NavigationOptions): boolean;
        /** Submit current geometry and replace tiles overlapping the dirty world bounds. */
        static Replace(nodes: readonly Node[], minimum: Vector3, maximum: Vector3): boolean;
        /** Complete reachable path only; null includes invalid or unreachable queries. */
        static FindPath(start: Vector3, end: Vector3): Vector3[] | null;
        static Clear(): boolean;
        static readonly ready: boolean;
        static readonly revision: bigint | null;
        static readonly rebuiltTiles: number | null;
        static readonly lastError: string | null;
    }
    export class DestructionComponent extends Component {
        private constructor();
        /** World-space damage sphere; impulse is in newton-seconds. */
        DamageSphere(
            x: number,
            y: number,
            z: number,
            radius: number,
            damage: number,
            impulse?: number,
        ): boolean;
        /** Accept queued damage; completed runs on the main thread after publish or cancellation.
         * Scene changes and script shutdown discard callbacks. No callback on rejected requests. */
        DamageSphereAsync(
            x: number, y: number, z: number, radius: number, damage: number, impulse: number,
            completed: (success: boolean) => void,
        ): boolean;
        /** Multi-sample foundation coverage against one current-scene static collider. */
        RefreshSupport(support: Collider, maxGap: number, minCoverage: number): boolean;
        OwnsFragment(nodeId: bigint): boolean;
        /** Keep at most maxCount dynamic groups; static support is preserved. Zero retires all.
         * A zero age disables expiry. Returns retired count, or null for invalid/busy owners. */
        RetireFragments(maxCount: number, maxAgeSeconds: number): number | null;
        /** Official Blast stress; limits in Pa. Broken bonds on success, null on failure. */
        ApplyStress(
            x: number,
            y: number,
            z: number,
            elasticLimit: number,
            fatalLimit: number,
            iterations: number,
        ): number | null;
        Reset(): boolean;
        readonly ready: boolean;
        readonly revision: bigint | null;
        readonly chunkCount: number | null;
        readonly groupCount: number | null;
        readonly dynamicGroups: number | null;
        readonly lastError: string | null;
    }
    export class Collider extends Component {
        private constructor();
        /** Local half-extents of a box. */
        SetBox(x: number, y: number, z: number): boolean;
        SetSphere(radius: number): boolean;
        /** Half-height is measured along the cylinder part, excluding the hemispheres. */
        SetCapsule(radius: number, halfHeight: number): boolean;
        /** Static triangle Mesh by resource path; rejects dynamic and kinematic rigid bodies. */
        SetMesh(path: string): boolean;
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
        /** External horizontal velocity damping per second, 0..1000; default 6. */
        SetExternalDamping(value: number): boolean;
        /** Desired world-space horizontal velocity; physics supplies the vertical component. */
        SetVelocity(x: number, z: number): boolean;
        /** Clear active movement and pending jump without cancelling external velocity. */
        ClearInput(): boolean;
        /** World-space velocity delta in m/s, accumulated and consumed once at a fixed step. */
        AddVelocity(x: number, y: number, z: number): boolean;
        /** Last physics velocity, not the pending movement request. */
        GetVelocity(): { readonly x: number; readonly y: number; readonly z: number } | null;
        /** Accepted only while grounded, consumed at the next physics step. */
        Jump(speed: number): boolean;
        readonly grounded: boolean;
    }
    /** Available when the optional character_controller runtime plugin is enabled. */
    export class CharacterController extends Component {
        private constructor();
        /** Caller supplies movement, look delta and one-shot jump/view intents explicitly. */
        SetInput(
            moveX: number,
            moveY: number,
            lookX: number,
            lookY: number,
            jump: boolean,
            toggleView: boolean,
        ): boolean;
        /** Cancel this controller's input and its driven motor's movement/jump; preserve external velocity. */
        ClearInput(): boolean;
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

    /** One RmlUi document on an ordinary node; controls are not scene nodes. */
    export class UiComponent extends Component {
        private constructor();
        /** Empty path clears the document; failed loads preserve the previous document. */
        SetDocument(path: string): boolean;
        /** Available immediately after Scene.Load, including during OnInitialize. */
        FindElement(id: string): UiElement | null;
        SetVisible(value: boolean): boolean;
        readonly visible: boolean;
    }

    export type UiEventType = "click" | "change" | "input";
    export interface UiEvent {
        readonly type: UiEventType;
        /** The original nearest element with an ID, fixed while the event bubbles. */
        readonly target: UiElement;
        /** The element whose subscription is currently receiving the event. */
        readonly currentTarget: UiElement;
        /** Form value captured when the native event occurred. */
        readonly value: string;
    }

    /** A checked identity; deleted elements and replaced documents invalidate old handles. */
    export class UiElement {
        private constructor();
        readonly isValid: boolean;
        readonly id: string | null;
        readonly text: string | null;
        readonly value: string | null;
        /** Sets plain text, not markup. */
        SetText(value: string): boolean;
        SetValue(value: string): boolean;
        SetVisible(value: boolean): boolean;
        SetEnabled(value: boolean): boolean;
        SetClass(name: string, active: boolean): boolean;
        SetStyle(name: string, value: string): boolean;
        /** The texture must be declared by the UI resource. */
        SetImage(path: string): boolean;
        /** Returns a subscription ID, or zero on failure. */
        On(type: UiEventType, callback: (event: UiEvent) => void): number;
        /** Removal still works after the element becomes invalid. */
        Off(subscription: number): boolean;
    }

    /** Project Model is ordinary TS data; derived controllers own business event handling. */
    export class UiController<T = unknown> {
        constructor(view: UiComponent, model: T);
        readonly view: UiComponent;
        readonly model: T;
        readonly bound: boolean;
        /** Runs OnBind then OnRefresh once; an already bound current view returns true. */
        Bind(): boolean;
        /** Explicitly refreshes the View from Model. */
        Refresh(): boolean;
        /** The element must belong to this controller's current View. */
        Listen(element: UiElement, type: UiEventType, callback: (event: UiEvent) => void): boolean;
        /** Removes all owned subscriptions; repeated calls are safe. */
        Unbind(): void;
        protected OnBind(): boolean;
        protected OnRefresh(): boolean;
        protected OnUnbind(): void;
    }
}
