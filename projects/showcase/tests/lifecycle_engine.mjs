import { resourceStub } from "./resource_mock.mjs";
// 仅模拟已有宿主契约；不实现渲染、物理、资源异步调度或 RmlUi 布局。
export const engineStub = resourceStub + String.raw`
export const fixture = {
    scene: null,
    failDocument: false, failRefreshOnce: false, views: [], nodes: [], loads: [],
    pendingLoad: null, loadStatus: "ready", failCommit: false, cancellations: 0,
    ids: [], movie: null, movieReleased: 0,
    reset() {
        for (const node of this.nodes) node.live = false;
        this.scene = null; this.failDocument = false; this.failRefreshOnce = false;
        this.views = []; this.nodes = []; this.loads = []; this.pendingLoad = null;
        this.pendingResource = null; this.loadStatus = "ready"; this.failCommit = false; this.cancellations = 0;
        this.movie = null; this.movieReleased = 0;
    },
    element(id) {
        const views = [this.scene?.view, ...this.views].filter(Boolean);
        return views.map(view => view.FindElement(id)).find(element => element?.listeners.length > 0)
            ?? views.map(view => view.FindElement(id)).find(Boolean);
    },
    click(id) {
        const element = this.element(id);
        if (!element || !element.enabled) throw new Error("Missing or disabled test control: " + id);
        for (const subscription of [...element.listeners]) {
            if (subscription.type === "click")
                subscription.callback({ type: "click", target: element, currentTarget: element, value: "" });
        }
    },
    input(id, value) {
        const element = this.element(id);
        if (!element?.isValid || !element.enabled) throw new Error("Unavailable input: " + id);
        element.value = value;
        for (const subscription of [...element.listeners])
            if (subscription.type === "input") subscription.callback({type:"input",target:element,currentTarget:element,value});
    },
};
class Element {
    constructor(id, view) { this.id = id; this.view = view; this.text = ""; this.value = ""; this.enabled = true; this.classes = new Set(); this.listeners = []; }
    get isValid() { return this.view.node?.IsValid() ?? this.view.scene.IsValid(); }
    SetText(value) {
        if (fixture.failRefreshOnce) { fixture.failRefreshOnce = false; return false; }
        this.text = value; return true;
    }
    SetEnabled(value) { this.enabled = value; return true; }
    SetClass(name, active) { active ? this.classes.add(name) : this.classes.delete(name); return true; }
}
export class UiComponent {
    constructor(scene) {
        this.scene = scene;
        this.elements = new Map(fixture.ids.map(id => [id, new Element(id, this)]));
        fixture.views.push(this);
    }
    FindElement(id) { return this.elements.get(id) ?? null; }
    SetDocument() { return !fixture.failDocument; }
}
export class Node {
    constructor(scene, name) { this.scene = scene; this.name = name; this.live = true; this.persistent = false; this.id = BigInt(fixture.nodes.length + 1); fixture.nodes.push(this); }
    IsValid() { return this.live && (this.persistent || this.scene.IsValid()); }
    SetPersistent(value) { this.persistent = value; return this.live; }
    Destroy() {
        if (!this.live) return false;
        this.live = false;
        for (const element of this.view?.elements.values() ?? []) element.listeners = [];
        return true;
    }
    GetComponent(type) {
        if (type === UiComponent) return this.view ?? this.scene.view;
        if (type === Transform) return new Transform();
        return null;
    }
    AddComponent(type) {
        if (type !== UiComponent) return null;
        this.scene.view = new UiComponent(this.scene);
        this.view = this.scene.view; this.view.node = this;
        return this.scene.view;
    }
}
export class Scene {
    constructor(path) {
        this.path = path; this.live = true;
        this.view = path === "scenes/launcher.scene" ? new UiComponent(this) : null;
        this.nodes = [];
        const node = this.CreateNode(path === "scenes/launcher.scene" ? "Launcher UI" : "Cube");
        node.view = this.view; if (this.view) this.view.node = node;
    }
    static Load(path) {
        fixture.scene?.Destroy();
        fixture.scene = new Scene(path); fixture.loads.push(path); return fixture.scene;
    }
    static BeginLoad(path) {
        if (path.includes("missing")) return false;
        fixture.pendingLoad = path; return true;
    }
    static StepLoad() {
        return fixture.pendingLoad ? {status:fixture.loadStatus,completed:1,total:2,error:fixture.loadStatus === "failed" ? "fixture load failure" : ""} : null;
    }
    static CommitLoad() {
        const path = fixture.pendingLoad; fixture.pendingLoad = null;
        return path && !fixture.failCommit ? Scene.Load(path) : null;
    }
    static CancelLoad() { ++fixture.cancellations; fixture.pendingLoad = null; }
    IsValid() { return this.live && fixture.scene === this; }
    Destroy() {
        if (!this.IsValid()) return false;
        for (const node of this.nodes) if (!node.persistent) node.Destroy();
        this.live = false; fixture.scene = null; return true;
    }
    FindNode(name) { return this.nodes.find(node => node.name === name && node.IsValid()) ?? null; }
    CreateNode(name) { const node = new Node(this, name); this.nodes.push(node); return node; }
}
export class UiController {
    constructor(view, model) { this.view = view; this.model = model; this.bound = false; }
    Bind() {
        this.bound = true;
        if (this.OnBind() === true && this.OnRefresh() === true) return true;
        this.Unbind();
        return false;
    }
    Refresh() { return this.OnRefresh(); }
    Listen(element, type, callback) { element.listeners.push({ owner: this, type, callback }); return true; }
    Unbind() {
        if (!this.bound) return;
        this.bound = false;
        for (const element of this.view.elements.values()) {
            element.listeners = element.listeners.filter(subscription => subscription.owner !== this);
        }
        this.OnUnbind();
    }
    OnBind() { return true; }
    OnRefresh() { return true; }
    OnUnbind() {}
}
export const MovieState = { preparing: 0, playing: 1, paused: 2, completed: 3, cancelled: 4, failed: 5 };
export const MovieStopReason = { none: 0, stopped: 1, skipped: 2 };
export const Movie = {
    lastError: "fixture rejection",
    Play(resource) { const path = acquisitions.get(resource); if (!path || fixture.movie) return null; fixture.movie = { id: 1n, generation: 1n, path, state: MovieState.preparing }; return fixture.movie; },
    Status(handle) { return fixture.movie === handle ? { state: handle.state, error: "" } : null; },
    Stop(handle) { if (fixture.movie !== handle) return false; handle.state = MovieState.cancelled; return true; },
    Release(handle) { if (fixture.movie !== handle) return false; fixture.movie = null; ++fixture.movieReleased; return true; },
};
export class Gameplay { OnInitialize() { return true; } Update() {} OnShutdown() {} }
export class Transform { SetRotation() { return true; } }
export class Animator {}
export class MeshRenderer {}
// The menu loads every gameplay module; provide export identities for modules not executed here.
export class AnimationState {}
export class AnimationStateMachine {
    AddParameter() { return true; }
    AddState() { return true; }
    AddTransition() { return true; }
}
export class Camera {}
export class EnvironmentComponent {}
export class CharacterMotor {}
export class NavigationAgent {}
export class CharacterController {
    constructor() { this.inputs = []; this.cancellations = 0; }
    IsValid() { return true; }
    SetInput(input) { this.inputs.push(input); return true; }
    ClearInput() { ++this.cancellations; return true; }
}
export const InputDeviceKind = { keyboard: 0, mouse: 1, gamepad: 2, touch: 4 };
export const InputControlKind = { key: 0, mouse_button: 1, mouse_delta: 3, gamepad_button: 6, gamepad_axis: 7 };
export const InputActionKind = { button: 0, axis1d: 1, axis2d: 2 };
export const InputActivation = { wait_for_neutral: 0, use_current_state: 1 };
export const InputVirtualKind = { button: 0, stick: 1, drag: 2 };
export const InputValueMode = { state: 0, delta: 1 };
export const inputFixture = {
    devices: [], configurations: [], reject: false, destroyed: 0,
    frame: { device_revision: 0n, cancel_revision: 0n },
};
export const Input = {
    GetDevices() { return inputFixture.devices; },
    GetFrameState() { return inputFixture.frame; },
    CreateChannel() { return { slot: 0, generation: 1 }; },
    ConfigureChannel(channel, data) {
        if (inputFixture.reject) return false;
        inputFixture.configurations.push(structuredClone(data));
        return true;
    },
    ReadAction(channel, name) { return { value_x: name === "move" ? 0.5 : 0, value_y: 0 }; },
    DestroyChannel() { ++inputFixture.destroyed; return true; },
    lastError: "fixture rejection",
};
export const Storage = {
    GetString() { return null; }, SetString() { return true; }, Save() { return true; },
    lastError: "",
};
export class TerrainComponent {}
export class DestructionComponent {}
export class AudioListener {}
export class AudioSource {}
export class EffectComponent {}
export class Collider {}
export const Physics = {};
export const Navigation = {};
export const Time = { deltaTime: 1 / 60 };
export const Debug = { Log() {}, LogError() {}, LogWarning() {} };
export const Render = {
    SetAmbient() { return true; }, SetExposure() { return true; },
    SetFilmicTonemap() { return true; }, SetDirectionalShadows() { return true; },
    SetClearColor() { return true; }
};
`;
