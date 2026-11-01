import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtemp, mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const root = path.resolve(
    process.argv[2] ?? path.join(path.dirname(fileURLToPath(import.meta.url)), ".."),
);
const output = await mkdtemp(path.join(tmpdir(), "engine-showcase-navigation-"));
const launcherPath = "scenes/launcher.scene";
const cubePath = "scenes/main.scene";

// 仅替代 engine 模块的宿主边界；导航、Controller、Model 均来自实际项目编译结果。
const engineStub = String.raw`
export const fixture = {
    scene: null,
    failDocument: false,
    ids: [],
    reset() { this.scene = null; this.failDocument = false; },
    click(id) {
        const element = this.scene.view.FindElement(id);
        if (!element || !element.enabled) throw new Error("Missing or disabled test control: " + id);
        for (const subscription of [...element.listeners]) {
            subscription.callback({ type: "click", target: element, currentTarget: element, value: "" });
        }
    }
};
class Element {
    constructor(id) { this.id = id; this.text = ""; this.enabled = true; this.classes = new Set(); this.listeners = []; }
    SetText(value) { this.text = value; return true; }
    SetEnabled(value) { this.enabled = value; return true; }
    SetClass(name, active) { active ? this.classes.add(name) : this.classes.delete(name); return true; }
}
export class UiComponent {
    constructor(scene) {
        this.scene = scene;
        this.elements = new Map(fixture.ids.map(id => [id, new Element(id)]));
    }
    FindElement(id) { return this.elements.get(id) ?? null; }
    SetDocument() { return !fixture.failDocument; }
}
class Node {
    constructor(scene) { this.scene = scene; }
    GetComponent(type) {
        if (type === UiComponent) return this.scene.view;
        if (type === Transform) return new Transform();
        return null;
    }
    AddComponent(type) {
        if (type !== UiComponent) return null;
        this.scene.view = new UiComponent(this.scene);
        return this.scene.view;
    }
}
export class Scene {
    constructor(path) { this.path = path; this.view = new UiComponent(this); }
    static Load(path) { fixture.scene = new Scene(path); return fixture.scene; }
    IsValid() { return fixture.scene === this; }
    FindNode() { return new Node(this); }
    CreateNode() { return new Node(this); }
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
export class Gameplay { OnInitialize() { return true; } Update() {} OnShutdown() {} }
export class Transform { SetRotation() { return true; } }
export class Animator {}
export class MeshRenderer {}
// 菜单会加载所有玩法模块；此处只提供未执行玩法所需的导出身份。
export class Camera {}
export class CharacterMotor {}
export class CharacterController {
    constructor() { this.inputs = []; this.cancellations = 0; }
    IsValid() { return true; }
    SetInput(...input) { this.inputs.push(input); return true; }
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

try {
    const settings = path.join(root, "projects/showcase/settings");
    execFileSync(
        process.execPath,
        [
            path.join(settings, "node_modules/typescript/lib/tsc.js"),
            "--project",
            path.join(settings, "tsconfig.json"),
            "--outDir",
            output,
            "--sourceMap",
            "false",
        ],
        { stdio: "inherit" },
    );
    await writeFile(path.join(output, "package.json"), '{"type":"module"}\n');
    const engineDirectory = path.join(output, "node_modules/engine");
    await mkdir(engineDirectory, { recursive: true });
    await writeFile(
        path.join(engineDirectory, "package.json"),
        '{"type":"module","exports":"./index.js"}\n',
    );
    await writeFile(path.join(engineDirectory, "index.js"), engineStub);

    const load = (relative) => import(pathToFileURL(path.join(output, relative)).href);
    const { fixture } = await load("node_modules/engine/index.js");
    const { default: Launcher } = await load("scripts/modules/menu.js");
    const { default: Minimal } = await load("scripts/modules/minimal.js");
    const { LauncherController } = await load("scripts/controllers/launcher_controller.js");
    const markup = await readFile(
        path.join(root, "projects/showcase/assets/ui/launcher.rml"),
        "utf8",
    );
    fixture.ids = [...markup.matchAll(/\bid="([^"]+)"/g)].map((match) => match[1]);

    const initialize = Minimal.prototype.OnInitialize;
    const shutdown = Minimal.prototype.OnShutdown;
    let shutdowns = 0;
    let application;
    const begin = () => {
        fixture.reset();
        application = new Launcher();
        assert.equal(application.OnInitialize(), true, "launcher initializes");
        return application;
    };
    const openCube = () => {
        fixture.click("scene-cube");
        application.Update();
    };
    const reset = () => {
        Minimal.prototype.OnInitialize = initialize;
        Minimal.prototype.OnShutdown = shutdown;
        delete LauncherController.prototype.OnUnbind;
        application?.OnShutdown();
        application = null;
        shutdowns = 0;
    };
    const countShutdown = () => {
        Minimal.prototype.OnShutdown = function () {
            ++shutdowns;
            shutdown.call(this);
        };
    };
    const run = (name, check) => {
        try {
            countShutdown();
            check();
            console.log("PASS " + name);
        } finally {
            reset();
        }
    };

    run("real menu/controller navigation returns to the menu", () => {
        begin();
        openCube();
        assert.equal(fixture.scene.path, cubePath);
        assert.equal(fixture.scene.view.FindElement("launcher-root").classes.has("in-world"), true);
        fixture.click("back-menu");
        application.Update();
        assert.equal(fixture.scene.path, launcherPath);
        assert.equal(shutdowns, 1);
        application.OnShutdown();
        application.OnShutdown();
        assert.equal(shutdowns, 1, "returned gameplay is not closed twice");
    });

    run("throwing partial initialization is closed once by entry shutdown", () => {
        Minimal.prototype.OnInitialize = function () {
            assert.equal(initialize.call(this), true);
            throw new Error("partial initialization");
        };
        begin();
        assert.throws(openCube, /partial initialization/);
        application.OnShutdown();
        application.OnShutdown();
        assert.equal(shutdowns, 1);
    });

    run("failed initialization and navigation view restore a usable menu", () => {
        Minimal.prototype.OnInitialize = function () {
            assert.equal(initialize.call(this), true);
            return false;
        };
        begin();
        openCube();
        assert.equal(fixture.scene.path, launcherPath);
        assert.match(fixture.scene.view.FindElement("launcher-status").text, /Cannot open/);
        assert.equal(shutdowns, 1);
        Minimal.prototype.OnInitialize = initialize;
        fixture.failDocument = true;
        openCube();
        assert.equal(fixture.scene.path, launcherPath);
        assert.match(
            fixture.scene.view.FindElement("launcher-status").text,
            /Cannot create navigation/,
        );
        assert.equal(shutdowns, 2);
        fixture.failDocument = false;
        openCube();
        assert.equal(fixture.scene.path, cubePath, "retry is no longer blocked by pending state");
    });

    run("controller shutdown failure does not skip active gameplay cleanup", () => {
        begin();
        openCube();
        let unbinds = 0;
        LauncherController.prototype.OnUnbind = function () {
            ++unbinds;
            throw new Error("controller shutdown");
        };
        assert.throws(() => application.OnShutdown(), /controller shutdown/);
        application.OnShutdown();
        assert.equal(unbinds, 1);
        assert.equal(shutdowns, 1);
    });

    run("throwing gameplay shutdown is not repeated after navigation fails", () => {
        Minimal.prototype.OnShutdown = function () {
            ++shutdowns;
            shutdown.call(this);
            throw new Error("gameplay shutdown");
        };
        begin();
        openCube();
        fixture.click("back-menu");
        assert.throws(() => application.Update(), /gameplay shutdown/);
        application.OnShutdown();
        application.OnShutdown();
        assert.equal(shutdowns, 1);
    });

    // 实际 CharacterControls；替代宿主输入边界，观察通道提交而不重写映射逻辑。
    const { CharacterControls } = await load("scripts/character_controls.js");
    const {
        CharacterController,
        InputDeviceKind: kinds,
        inputFixture,
    } = await load("node_modules/engine/index.js");
    const device = (connection_id, kind, layout_key = "fixture") => ({
        connection_id,
        kind,
        layout_key,
    });
    const controller = new CharacterController();
    const scene = {
        FindNode: () => ({
            GetComponent: (type) => (type === CharacterController ? controller : null),
        }),
    };
    inputFixture.devices = [
        device(1n, kinds.keyboard, "sdl:keyboard:aggregate"),
        device(2n, kinds.mouse, "sdl:mouse:aggregate"),
        device(3n, kinds.touch),
        device(4n, kinds.keyboard),
        device(5n, kinds.mouse),
    ];
    const controls = new CharacterControls();
    assert.equal(controls.Bind(scene), true);
    const initial = inputFixture.configurations[0];
    const physicalSources = initial.actions.flatMap((action) =>
        action.bindings.flatMap((binding) => [
            ...(binding.modifiers ?? []),
            ...binding.parts.flatMap((part) => (part.source ? [part.source] : [])),
        ]),
    );
    assert.deepEqual(
        [...new Set(physicalSources.map((source) => source.connection_id))].sort(),
        [1n, 2n],
        "all-keyboard/mouse policy uses aggregate sources without duplicate physical bindings",
    );
    assert.equal(initial.virtual_controls.length, 3);
    controls.Update();
    inputFixture.devices.push(device(6n, kinds.gamepad), device(7n, kinds.keyboard));
    ++inputFixture.frame.device_revision;
    controls.Update();
    assert.equal(
        inputFixture.configurations.length,
        1,
        "unassigned gamepad and physical keyboard insertion do not replace held touch mappings",
    );
    assert.equal(
        controller.inputs.at(-1)[0],
        0.5,
        "the existing input snapshot continues driving the character",
    );
    inputFixture.devices = inputFixture.devices.filter((value) => value.connection_id !== 6n);
    ++inputFixture.frame.device_revision;
    controls.Update();
    assert.equal(
        inputFixture.configurations.length,
        1,
        "unassigned removal also preserves the channel",
    );
    assert.equal(controls.AssignGamepad(6n), false, "disconnected devices cannot be assigned");
    inputFixture.devices.push(device(6n, kinds.gamepad));
    assert.equal(controls.AssignGamepad(6n), true);
    assert.equal(
        inputFixture.configurations.length,
        2,
        "explicit pairing submits a changed mapping",
    );
    assert.equal(controls.AssignGamepad(6n), true);
    assert.equal(inputFixture.configurations.length, 2, "unchanged pairing is not resubmitted");
    inputFixture.reject = true;
    assert.equal(controls.SaveKeys({ jump: 45 }), false);
    inputFixture.reject = false;
    assert.equal(controls.SaveKeys({ jump: 45 }), true);
    assert.equal(
        inputFixture.configurations.length,
        3,
        "failed configuration does not poison a later retry",
    );
    inputFixture.devices = inputFixture.devices.filter((value) => value.connection_id !== 3n);
    ++inputFixture.frame.device_revision;
    controls.Update();
    assert.equal(
        inputFixture.configurations.length,
        4,
        "a bound touch device change updates the mapping",
    );
    assert.equal(inputFixture.configurations.at(-1).virtual_controls.length, 0);
    const canceled = controller.cancellations;
    ++inputFixture.frame.cancel_revision;
    controls.Update();
    assert.equal(
        controller.cancellations,
        canceled + 1,
        "safety cancellation uses the controller contract",
    );
    controls.Shutdown();
    assert.equal(inputFixture.destroyed, 1);
    assert.equal(controls.Bind(scene), true);
    const configured = inputFixture.configurations.length;
    assert.equal(controls.Bind(scene), true);
    assert.equal(inputFixture.destroyed, 2, "rebinding releases the previous channel");
    assert.equal(
        inputFixture.configurations.length,
        configured + 1,
        "a replacement channel receives its configuration even when the bindings are unchanged",
    );
    controls.Shutdown();
    assert.equal(inputFixture.destroyed, 3);
    console.log(
        "PASS real character input policy preserves mappings across unrelated device changes",
    );
} finally {
    await rm(output, { recursive: true, force: true });
}
