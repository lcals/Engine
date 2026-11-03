import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtemp, mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const root = path.resolve(
    process.argv[2] ?? path.join(path.dirname(fileURLToPath(import.meta.url)), "../../.."),
);
const output = await mkdtemp(path.join(tmpdir(), "engine-showcase-navigation-"));
const launcherPath = "scenes/launcher.scene";
const cubePath = "scenes/main.scene";

// 仅替代 engine 模块的宿主边界；导航、Controller、Model 均来自实际项目编译结果。
import { engineStub } from "./lifecycle_engine.mjs";

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

    run("real menu preserves the original document and subscriptions across scene visits", () => {
        begin();
        const originalView = fixture.scene.view;
        const originalBack = originalView.FindElement("back-menu");
        const originalSubscriptions = originalBack.listeners.length;
        openCube();
        assert.equal(fixture.scene.path, cubePath);
        assert.equal(fixture.element("launcher-root").classes.has("in-world"), true);
        fixture.click("back-menu");
        application.Update();
        assert.equal(fixture.scene, null, "return leaves a usable persistent menu without a gameplay scene");
        assert.equal(shutdowns, 1);
        assert.equal(fixture.element("back-menu"), originalBack);
        assert.equal(originalBack.listeners.length, originalSubscriptions);
        assert.equal(fixture.loads.filter(path => path === launcherPath).length, 1);
        openCube();
        fixture.click("back-menu");
        application.Update();
        assert.equal(originalBack.listeners.length, originalSubscriptions, "returning twice does not duplicate subscriptions");
        application.OnShutdown();
        application.OnShutdown();
        assert.equal(shutdowns, 2, "each gameplay closes once");
        assert.equal([...originalView.elements.values()].flatMap(element => element.listeners).length, 0);
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
        assert.equal(fixture.scene, null, "return leaves a usable persistent menu without a gameplay scene");
        assert.match(fixture.element("launcher-status").text, /Cannot open/);
        assert.equal(shutdowns, 1);
        Minimal.prototype.OnInitialize = initialize;
        Minimal.prototype.OnInitialize = function () {
            const ready = initialize.call(this); fixture.failRefreshOnce = true; return ready;
        };
        openCube();
        assert.equal(fixture.scene, null, "return leaves a usable persistent menu without a gameplay scene");
        assert.match(
            fixture.element("launcher-status").text,
            /Cannot create navigation/,
        );
        assert.equal(shutdowns, 2);
        Minimal.prototype.OnInitialize = initialize;
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

    // 实际菜单、靶场模块及 MVC；只替代加载场景和原生查询结果，不模拟碰撞数学。
    const api = await load("node_modules/engine/index.js");
    const originalLoad = api.Scene.Load;
    const rangeMarkup = await readFile(
        path.join(root, "projects/showcase/assets/ui/shooting-range.rml"),
        "utf8",
    );
    fixture.ids.push(...[...rangeMarkup.matchAll(/\bid="([^"]+)"/g)].map((match) => match[1]));
    const nodes = new Map();
    const queries = [];
    const aspects = [];
    const rangePath = "scenes/shooting-range.scene";
    const rangeScene = {
        path: rangePath,
        Destroy() { if (!this.IsValid()) return false; fixture.scene = null; return true; },
        IsValid() {
            return fixture.scene === this;
        },
    };
    rangeScene.view = new api.UiComponent(rangeScene);
    let nextNode = 1n;
    const enabledComponent = () => ({
        enabled: true,
        SetEnabled(value) {
            this.enabled = value;
            return true;
        },
    });
    const makeNode = (name) => {
        const transform = {
            SetPosition() {
                return true;
            },
            SetRotation() {
                return true;
            },
        };
        const renderer = {
            ...enabledComponent(),
            material: {
                SetType() {
                    return true;
                },
            },
        };
        const collider = enabledComponent();
        const character = new api.CharacterController();
        const camera = {
            ViewportPointToRay(u, v, aspect) {
                assert.deepEqual([u, v], [0.5, 0.5]);
                aspects.push(aspect);
                return { origin: { x: 0, y: 2, z: 8 }, direction: { x: 0, y: 0, z: -1 } };
            },
        };
        const node = {
            id: nextNode++,
            sceneId: 10n,
            name,
            collider,
            renderer,
            character,
            IsValid() {
                return rangeScene.IsValid();
            },
            SetParent() {
                return true;
            },
            FindChild(child) {
                return nodes.get(`${name}/${child}`) ?? null;
            },
            GetComponent(type) {
                return (
                    new Map([
                        [api.Transform, transform],
                        [api.MeshRenderer, renderer],
                        [api.Collider, collider],
                        [api.Camera, camera],
                        [api.CharacterController, character],
                        [api.UiComponent, rangeScene.view],
                    ]).get(type) ?? null
                );
            },
            AddComponent(type) {
                if (type === api.Animator)
                    return {
                        SetAnimation() {
                            return true;
                        },
                        Play() {
                            return true;
                        },
                    };
                return this.GetComponent(type);
            },
        };
        nodes.set(name, node);
        return node;
    };
    for (const name of [
        "Player",
        "Camera",
        "Hit marker",
        "Shooting range UI",
        "Avatar",
        "GENERIC MAN new",
        "Near target",
        "Far target",
        "Covered target",
        "Cover wall",
    ])
        makeNode(name);
    for (const name of ["Near target", "Far target", "Covered target"])
        for (const child of ["Center", "Bullseye"]) makeNode(`${name}/${child}`);
    rangeScene.FindNode = (name) => nodes.get(name) ?? null;
    rangeScene.InstantiateModel = () => nodes.get("Avatar");
    rangeScene.CreateNode = (name) => makeNode(name);
    api.Scene.Load = (path) => {
        if (path !== rangePath) return originalLoad(path);
        fixture.scene?.Destroy();
        fixture.scene = rangeScene;
        return rangeScene;
    };
    let queryResult = null;
    const hit = (name) => {
        const node = nodes.get(name);
        return {
            hit: true,
            node,
            nodeId: node.id,
            sceneId: node.sceneId,
            instanceId: node.id,
            kind: "collider",
            point: { x: 0, y: 1.6, z: -6 },
            normal: { x: 0, y: 0, z: 1 },
            distance: 14,
            fraction: 0.14,
        };
    };
    api.Physics.Raycast = (origin, displacement, options) => {
        queries.push({ kind: "ray", origin, displacement, options });
        return queryResult;
    };
    api.Physics.SphereCast = (origin, displacement, radius, options) => {
        queries.push({ kind: "sphere", origin, displacement, radius, options });
        return queryResult;
    };
    api.Physics.IsHitValid = (result) => result.valid !== false;
    api.Render.GetViewportSize = () => ({ x: 900, y: 1200 });
    const destroyedBeforeRange = inputFixture.destroyed;
    try {
        application = begin();
        fixture.click("scene-shooting");
        application.Update();
        assert.equal(fixture.scene, rangeScene, "menu opens the independent shooting range");
        const score = () => rangeScene.view.FindElement("range-score").text;
        const click = (id) => {
            fixture.click(id);
            application.Update();
        };
        queryResult = hit("Cover wall");
        click("range-fire");
        assert.match(score(), /0 \/ 3 targets.*1 shots/, "obstruction does not score");
        assert.equal(aspects[0], 0.75, "uses actual viewport aspect, including portrait");
        assert.equal(queries[0].options.ignoreNode, nodes.get("Player"));
        assert.equal(queries[0].options.ignoreDescendants, true);
        queryResult = hit("Near target");
        click("range-fire");
        assert.match(score(), /1 \/ 3 targets.*2 shots/);
        assert.equal(nodes.get("Near target").collider.enabled, false);
        assert.equal(nodes.get("Near target/Center").renderer.enabled, false);
        click("range-fire");
        assert.match(score(), /1 \/ 3 targets.*3 shots/, "same target never scores twice");
        click("range-sphere");
        queryResult = hit("Far target");
        click("range-fire");
        assert.equal(queries.at(-1).kind, "sphere");
        assert.equal(queries.at(-1).radius, 0.12);
        queryResult = hit("Covered target");
        queryResult.valid = false;
        click("range-fire");
        assert.match(score(), /2 \/ 3 targets/, "stale identity does not score");
        queryResult = hit("Covered target");
        click("range-fire");
        assert.match(score(), /3 \/ 3 targets/);
        assert.equal(rangeScene.view.FindElement("range-fire").enabled, false);
        assert.match(rangeScene.view.FindElement("range-status").text, /Range cleared/);
        click("range-reset");
        assert.match(score(), /0 \/ 3 targets.*0 shots/);
        for (const name of ["Near target", "Far target", "Covered target"]) {
            assert.equal(nodes.get(name).collider.enabled, true);
            assert.equal(nodes.get(name).renderer.enabled, true);
            assert.equal(nodes.get(`${name}/Center`).renderer.enabled, true);
        }
        assert.equal(nodes.get("Hit marker").renderer.enabled, false);
        queryResult = { hit: false };
        click("range-fire");
        assert.match(score(), /0 \/ 3 targets.*1 shots/);
        click("back-menu");
        assert.equal(fixture.scene, null, "return leaves a usable persistent menu without a gameplay scene");
        assert.equal(
            inputFixture.destroyed,
            destroyedBeforeRange + 2,
            "both shooting and movement channels released",
        );
        assert.equal(
            [...rangeScene.view.elements.values()].flatMap((element) => element.listeners).length,
            0,
            "range subscriptions removed while the launcher document remains alive",
        );
        application.OnShutdown();
        console.log(
            "PASS real shooting range handles obstruction, score deduplication, reset, aspect and cleanup",
        );
    } finally {
        application?.OnShutdown();
        application = null;
        api.Scene.Load = originalLoad;
    }
} finally {
    await rm(output, { recursive: true, force: true });
}
