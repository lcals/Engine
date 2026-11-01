import {
    CharacterController,
    Debug,
    Input,
    InputActionData,
    InputActionKind,
    InputActivation,
    InputBindingData,
    InputBindingPartData,
    InputChannelHandle,
    InputControlKind,
    InputControlRef,
    InputDeviceKind,
    InputValueMode,
    InputVirtualControlData,
    InputVirtualKind,
    Scene,
    Storage,
    Time,
} from "engine";

// SDL scancodes describe key positions. Only this sample chooses these defaults.
const default_keys = { left: 4, right: 7, forward: 26, back: 22, jump: 44, view: 43 };
type Keys = typeof default_keys;
const storage_key = "showcase.input.keys.v1";

/** Project policy: one character, aggregate keyboard/mouse, explicitly assigned gamepad. */
export class CharacterControls {
    private channel: InputChannelHandle | null = null;
    private controller: CharacterController | null = null;
    private keys: Keys = { ...default_keys };
    private pad: bigint | null = null;
    private revision = -1n;
    private cancel_revision = -1n;
    private configuration_key = "";

    Bind(scene: Scene): boolean {
        this.Shutdown();
        this.keys = { ...default_keys };
        const player = scene.FindNode("Player");
        this.controller = player?.GetComponent(CharacterController) ?? null;
        this.channel = Input.CreateChannel();
        if (!this.controller || !this.channel) return false;
        const saved = Storage.GetString(storage_key);
        if (Storage.lastError) Debug.LogWarning(`Input overrides: ${Storage.lastError}`);
        if (saved !== null) {
            try {
                const value: unknown = JSON.parse(saved);
                if (value && typeof value === "object") {
                    const overrides = value as Partial<Keys>;
                    for (const key of Object.keys(default_keys) as (keyof Keys)[]) {
                        const code = overrides[key];
                        if (
                            typeof code === "number" &&
                            Number.isInteger(code) &&
                            code > 0 &&
                            code < 512
                        )
                            this.keys[key] = code;
                    }
                }
            } catch {
                Debug.LogWarning("Invalid input overrides; using project defaults.");
            }
        }
        // Initial assignment is sample TS policy. Reconnected pads require AssignGamepad again.
        this.pad =
            Input.GetDevices()?.find((device) => device.kind === InputDeviceKind.gamepad)
                ?.connection_id ?? null;
        return this.configure();
    }

    AssignGamepad(device: bigint | null): boolean {
        if (
            device !== null &&
            !Input.GetDevices()?.some(
                (value) => value.connection_id === device && value.kind === InputDeviceKind.gamepad,
            )
        )
            return false;
        const previous = this.pad;
        this.pad = device;
        if (this.configure()) return true;
        this.pad = previous;
        return false;
    }

    SaveKeys(overrides: Partial<Keys>): boolean {
        const next = { ...this.keys, ...overrides };
        if (Object.values(next).some((code) => !Number.isInteger(code) || code <= 0 || code >= 512))
            return false;
        const previous = this.keys;
        this.keys = next;
        if (!this.configure()) {
            this.keys = previous;
            return false;
        }
        return Storage.SetString(storage_key, JSON.stringify(next)) && Storage.Save();
    }

    private configure(activation: InputActivation = InputActivation.wait_for_neutral): boolean {
        if (!this.channel) return false;
        const devices = Input.GetDevices();
        const frame = Input.GetFrameState();
        if (!devices || !frame) return false;
        const move: InputBindingData[] = [],
            look: InputBindingData[] = [];
        const rate: InputBindingData[] = [],
            jump: InputBindingData[] = [],
            view: InputBindingData[] = [];
        const virtual_controls: InputVirtualControlData[] = [];
        const source = (id: bigint, kind: InputControlKind, code: number): InputControlRef => ({
            connection_id: id,
            kind,
            code,
        });
        const part = (ref: InputControlRef, x: number, y = 0): InputBindingPartData => ({
            source: ref,
            weight_x: x,
            weight_y: y,
        });
        for (const device of devices) {
            const id = device.connection_id;
            if (
                device.kind === InputDeviceKind.keyboard &&
                device.layout_key === "sdl:keyboard:aggregate"
            ) {
                const key = (code: number) => source(id, InputControlKind.key, code);
                move.push({
                    parts: [
                        part(key(this.keys.right), 1),
                        part(key(this.keys.left), -1),
                        part(key(this.keys.forward), 0, 1),
                        part(key(this.keys.back), 0, -1),
                    ],
                });
                jump.push({ parts: [part(key(this.keys.jump), 1)] });
                view.push({ parts: [part(key(this.keys.view), 1)] });
            } else if (
                device.kind === InputDeviceKind.mouse &&
                device.layout_key === "sdl:mouse:aggregate"
            ) {
                look.push({
                    normalize: false,
                    modifiers: [source(id, InputControlKind.mouse_button, 3)],
                    parts: [
                        part(source(id, InputControlKind.mouse_delta, 0), 1),
                        part(source(id, InputControlKind.mouse_delta, 1), 0, 1),
                    ],
                });
            } else if (device.kind === InputDeviceKind.gamepad && id === this.pad) {
                const axis = (code: number) => source(id, InputControlKind.gamepad_axis, code);
                // SDL standard positions: left/right stick x/y; south=0, north=3.
                move.push({ deadzone: 0.18, parts: [part(axis(0), 1), part(axis(1), 0, -1)] });
                rate.push({ deadzone: 0.18, parts: [part(axis(2), 1), part(axis(3), 0, 1)] });
                jump.push({ parts: [part(source(id, InputControlKind.gamepad_button, 0), 1)] });
                view.push({ parts: [part(source(id, InputControlKind.gamepad_button, 3), 1)] });
            } else if (device.kind === InputDeviceKind.touch) {
                const move_id = `move-${id}`,
                    look_id = `look-${id}`,
                    jump_id = `jump-${id}`;
                virtual_controls.push(
                    {
                        id: move_id,
                        kind: InputVirtualKind.stick,
                        region: {
                            connection_id: id,
                            x: 24,
                            y: -220,
                            anchor_y: 1,
                            width: 196,
                            height: 196,
                            floating: true,
                        },
                    },
                    {
                        id: look_id,
                        kind: InputVirtualKind.drag,
                        region: {
                            connection_id: id,
                            x: -360,
                            y: -280,
                            anchor_x: 1,
                            anchor_y: 1,
                            width: 336,
                            height: 256,
                        },
                    },
                    {
                        id: jump_id,
                        kind: InputVirtualKind.button,
                        priority: 20,
                        region: {
                            connection_id: id,
                            x: -124,
                            y: -124,
                            anchor_x: 1,
                            anchor_y: 1,
                            width: 96,
                            height: 96,
                        },
                    },
                );
                move.push({
                    parts: [
                        { virtual_control: move_id, virtual_axis: 1, weight_x: 1 },
                        { virtual_control: move_id, virtual_axis: 2, weight_x: 0, weight_y: -1 },
                    ],
                });
                look.push({
                    normalize: false,
                    parts: [
                        { virtual_control: look_id, virtual_axis: 1, weight_x: 1 },
                        { virtual_control: look_id, virtual_axis: 2, weight_x: 0, weight_y: 1 },
                    ],
                });
                jump.push({ parts: [{ virtual_control: jump_id, virtual_axis: 0 }] });
            }
        }
        const actions: InputActionData[] = [
            { name: "move", kind: InputActionKind.axis2d, bindings: move },
            {
                name: "look",
                kind: InputActionKind.axis2d,
                value_mode: InputValueMode.delta,
                bindings: look,
            },
            { name: "look_rate", kind: InputActionKind.axis2d, bindings: rate },
            { name: "jump", bindings: jump },
            { name: "view", bindings: view },
        ];
        // Device changes outside this character's bindings must not cancel its held touches.
        const key = JSON.stringify({ actions, virtual_controls }, (_, value) =>
            typeof value === "bigint" ? value.toString() : value,
        );
        if (key !== this.configuration_key) {
            if (
                !Input.ConfigureChannel(this.channel, {
                    enabled: true,
                    activation,
                    actions,
                    virtual_controls,
                })
            )
                return false;
            this.configuration_key = key;
        }
        this.revision = frame.device_revision;
        return true;
    }

    Update(): void {
        if (!this.channel || !this.controller?.IsValid()) return;
        const frame = Input.GetFrameState();
        if (!frame) return;
        if (frame.cancel_revision !== this.cancel_revision) {
            this.cancel_revision = frame.cancel_revision;
            this.controller.ClearInput();
        }
        if (
            frame.device_revision !== this.revision &&
            !this.configure(InputActivation.use_current_state)
        )
            throw new Error(`Cannot refresh character input: ${Input.lastError}`);
        const move = Input.ReadAction(this.channel, "move");
        const look = Input.ReadAction(this.channel, "look");
        const rate = Input.ReadAction(this.channel, "look_rate");
        const jump = Input.ReadAction(this.channel, "jump");
        const view = Input.ReadAction(this.channel, "view");
        // Controller sensitivity consumes mouse units; convert the stick's rate here once.
        const stick_scale = 800 * Time.deltaTime;
        if (
            !this.controller.SetInput(
                move?.value_x ?? 0,
                move?.value_y ?? 0,
                (look?.value_x ?? 0) + (rate?.value_x ?? 0) * stick_scale,
                (look?.value_y ?? 0) + (rate?.value_y ?? 0) * stick_scale,
                jump?.pressed ?? false,
                view?.pressed ?? false,
            )
        )
            throw new Error("Cannot submit character input");
    }

    Shutdown(): void {
        if (this.controller?.IsValid()) this.controller.ClearInput();
        if (this.channel) Input.DestroyChannel(this.channel);
        this.channel = null;
        this.controller = null;
        this.configuration_key = "";
    }
}
