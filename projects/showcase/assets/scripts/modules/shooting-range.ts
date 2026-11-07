import { Resources, ResourceHandle,
    Animator,
    Camera,
    Collider,
    Debug,
    Gameplay,
    Input,
    InputActivation,
    InputBindingData,
    InputChannelHandle,
    InputControlKind,
    InputDeviceKind,
    MeshRenderer,
    Node,
    Physics,
    Render,
    Scene,
    Transform,
    UiComponent,
} from "engine";
import { CharacterControls } from "../character_controls.js";
import { ShootingRangeController } from "../controllers/shooting_range_controller.js";
import { ShootingRangeModel } from "../models/shooting_range_model.js";

interface Target {
    readonly node: Node;
    readonly label: string;
    readonly collider: Collider;
    readonly renderers: readonly MeshRenderer[];
    hit: boolean;
}

const target_names = ["Near target", "Far target", "Covered target"];
const maximum_distance = 100;

/** Instant queries and example feedback only; movement stays in the character plugin. */
export default class Game extends Gameplay {
    private readonly resources: ResourceHandle[] = [];
    scene: Scene | null = null;
    private readonly movement = new CharacterControls();
    private readonly model = new ShootingRangeModel();
    private readonly targets = new Map<bigint, Target>();
    private controller: ShootingRangeController | null = null;
    private player: Node | null = null;
    private camera: Camera | null = null;
    private marker: Transform | null = null;
    private marker_renderer: MeshRenderer | null = null;
    private channel: InputChannelHandle | null = null;
    private device_revision = -1n;
    private configuration_key = "";

    override OnInitialize(): boolean {
        for (const path of ["scenes/shooting-range.scene", "prefabs/characters/player.prefab", "materials/character.material", "models/locomotions.fbx/animations/walk"]) {
            const resource = Resources.Load(path);
            if (!resource) { Debug.LogError(`Cannot load ${path}: ${Resources.lastError}`); return false; }
            this.resources.push(resource);
        }

        this.scene = Scene.Load("scenes/shooting-range.scene");
        const scene = this.scene;
        if (!scene) return false;
        this.player = scene.FindNode("Player");
        this.camera = scene.FindNode("Camera")?.GetComponent(Camera) ?? null;
        const marker = scene.FindNode("Hit marker");
        this.marker = marker?.GetComponent(Transform) ?? null;
        this.marker_renderer = marker?.GetComponent(MeshRenderer) ?? null;
        const view = scene.FindNode("Shooting range UI")?.GetComponent(UiComponent);
        if (
            !this.player ||
            !this.camera ||
            !this.marker ||
            !this.marker_renderer ||
            !view ||
            !this.marker_renderer.SetEnabled(false)
        )
            return false;

        // The same openly licensed showcase avatar and animation as the courtyard.
        const avatar = scene.InstantiatePrefab("prefabs/characters/player.prefab", "Avatar");
        const body = scene.FindNode("GENERIC MAN new");
        const transform = avatar?.GetComponent(Transform);
        const renderer = body?.GetComponent(MeshRenderer);
        const animator = body?.AddComponent(Animator);
        if (
            !avatar ||
            !avatar.SetParent(this.player) ||
            !transform ||
            !transform.SetPosition(0, 0.05, 0) ||
            !transform.SetRotation(0, 180, 0) ||
            !renderer?.material.SetType("materials/character.material") ||
            !animator ||
            !animator.SetAnimation("models/locomotions.fbx/animations/walk") ||
            !animator.Play()
        )
            return false;

        for (const label of target_names) {
            const node = scene.FindNode(label);
            const collider = node?.GetComponent(Collider);
            const face = node?.GetComponent(MeshRenderer);
            const center = node?.FindChild("Center")?.GetComponent(MeshRenderer);
            const bullseye = node?.FindChild("Bullseye")?.GetComponent(MeshRenderer);
            if (!node || !collider || !face || !center || !bullseye) return false;
            this.targets.set(node.id, {
                node,
                label,
                collider,
                renderers: [face, center, bullseye],
                hit: false,
            });
        }
        this.channel = Input.CreateChannel();
        if (!this.channel || !this.configure_input() || !this.movement.Bind(scene)) return false;
        this.model.ready = true;
        this.controller = new ShootingRangeController(view, this.model);
        if (!this.controller.Bind()) return false;
        Debug.Log(
            "Shooting range ready: WASD, right mouse look, left click fire, R reset. Three targets; walls block shots.",
        );
        return true;
    }

    private configure_input(): boolean {
        const frame = Input.GetFrameState();
        const devices = Input.GetDevices();
        if (!this.channel || !frame || !devices) return false;
        const fire: InputBindingData[] = [],
            reset: InputBindingData[] = [];
        for (const device of devices) {
            if (
                device.kind === InputDeviceKind.mouse &&
                device.layout_key === "sdl:mouse:aggregate"
            ) {
                fire.push({
                    parts: [
                        {
                            source: {
                                connection_id: device.connection_id,
                                kind: InputControlKind.mouse_button,
                                code: 1,
                            },
                        },
                    ],
                });
            } else if (
                device.kind === InputDeviceKind.keyboard &&
                device.layout_key === "sdl:keyboard:aggregate"
            ) {
                // SDL scancode R; this is the example's reset shortcut.
                reset.push({
                    parts: [
                        {
                            source: {
                                connection_id: device.connection_id,
                                kind: InputControlKind.key,
                                code: 21,
                            },
                        },
                    ],
                });
            }
        }
        const actions = [
            { name: "fire", bindings: fire },
            { name: "reset", bindings: reset },
        ];
        const key = JSON.stringify(actions, (_, value) =>
            typeof value === "bigint" ? value.toString() : value,
        );
        if (key !== this.configuration_key) {
            if (
                !Input.ConfigureChannel(this.channel, {
                    enabled: true,
                    activation: InputActivation.wait_for_neutral,
                    actions,
                })
            )
                return false;
            this.configuration_key = key;
        }
        this.device_revision = frame.device_revision;
        return true;
    }

    override Update(): void {
        this.movement.Update();
        const frame = Input.GetFrameState();
        if (frame && frame.device_revision !== this.device_revision && !this.configure_input())
            throw new Error(`Cannot refresh shooting input: ${Input.lastError}`);
        let action = this.model.pending;
        this.model.pending = null;
        if (!action && this.channel) {
            if (Input.ReadAction(this.channel, "reset")?.pressed) action = "reset";
            else if (Input.ReadAction(this.channel, "fire")?.pressed) action = "fire";
        }
        if (!action) return;
        if (action === "ray" || action === "sphere") {
            this.model.mode = action;
            this.model.status =
                action === "ray"
                    ? "Ray mode: exact crosshair aim."
                    : "Sphere mode: a 0.12 m radius sweep, still stopped by walls.";
        } else if (action === "reset") {
            if (!this.reset_targets())
                this.model.status =
                    "Could not restore the targets. Return to scenes and reopen the range.";
        } else if (!this.model.complete && !this.fire()) {
            this.model.status =
                "The shot query was unavailable. Aim again after the scene is ready.";
        }
        if (!this.controller?.Refresh()) throw new Error("Cannot refresh shooting range UI");
    }

    private fire(): boolean {
        const size = Render.GetViewportSize();
        if (!size || size.x <= 0 || size.y <= 0 || !this.camera || !this.player?.IsValid())
            return false;
        const ray = this.camera.ViewportPointToRay(0.5, 0.5, size.x / size.y);
        if (!ray) return false;
        const displacement = {
            x: ray.direction.x * maximum_distance,
            y: ray.direction.y * maximum_distance,
            z: ray.direction.z * maximum_distance,
        };
        const options = { ignoreNode: this.player, ignoreDescendants: true };
        const result =
            this.model.mode === "ray"
                ? Physics.Raycast(ray.origin, displacement, options)
                : Physics.SphereCast(ray.origin, displacement, 0.12, options);
        if (!result) return false;
        ++this.model.shots;
        if (!result.hit) {
            if (!this.marker_renderer?.SetEnabled(false)) return false;
            this.model.status = "Miss. No collision within 100 m.";
            return true;
        }
        if (!Physics.IsHitValid(result) || !result.node?.IsValid()) return false;
        if (
            !this.marker?.SetPosition(
                result.point.x + result.normal.x * 0.08,
                result.point.y + result.normal.y * 0.08,
                result.point.z + result.normal.z * 0.08,
            ) ||
            !this.marker_renderer?.SetEnabled(true)
        )
            return false;

        const target = this.targets.get(result.nodeId);
        if (target && target.node.sceneId === result.sceneId && !target.hit) {
            if (!this.set_target_enabled(target, false)) return false;
            target.hit = true;
            ++this.model.hits;
            this.model.status = this.model.complete
                ? `Range cleared in ${this.model.shots} shots. Reset targets to play again.`
                : `${target.label} hit at ${result.distance.toFixed(1)} m. ${this.model.total - this.model.hits} remaining.`;
        } else {
            this.model.status = `Blocked by the range geometry at ${result.distance.toFixed(1)} m. Walk around the blue wall to reach the covered target.`;
        }
        Debug.Log(
            `Range shot: ${this.model.mode}, ${this.model.hits}/${this.model.total} targets, ${this.model.shots} shots. ${this.model.status}`,
        );
        return true;
    }

    private set_target_enabled(target: Target, enabled: boolean): boolean {
        if (!target.collider.SetEnabled(enabled)) return false;
        for (const renderer of target.renderers) if (!renderer.SetEnabled(enabled)) return false;
        return true;
    }

    private reset_targets(): boolean {
        for (const target of this.targets.values()) {
            if (!this.set_target_enabled(target, true)) return false;
            target.hit = false;
        }
        if (!this.marker_renderer?.SetEnabled(false)) return false;
        this.model.Reset();
        return true;
    }

    override OnShutdown(): void {
        for (const resource of this.resources.splice(0)) {
            if (!Resources.Release(resource)) Debug.LogError("Cannot release showcase resource");
        }

        this.model.ready = false;
        this.model.pending = null;
        this.controller?.Unbind();
        this.controller = null;
        this.movement.Shutdown();
        if (this.channel) Input.DestroyChannel(this.channel);
        this.channel = null;
        this.configuration_key = "";
        this.device_revision = -1n;
        this.targets.clear();
        this.player = null;
        this.camera = null;
        this.marker = null;
        this.marker_renderer = null;
        this.scene = null;
    }
}
