#include "runtime/input/input_state.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::input_detail
{
namespace
{
std::string text(const char* value, const char* fallback)
{
    return value && *value ? value : fallback;
}

Device* find_device(State& state, DeviceKind kind, std::uint64_t native_id)
{
    const auto found =
        std::find_if(state.devices.begin(), state.devices.end(), [kind, native_id](const Device& d)
                     { return d.data.kind == kind && d.native_id == native_id; });
    return found == state.devices.end() ? nullptr : &*found;
}

void add_control(State& state, std::uint64_t device, Kind kind, std::uint32_t code,
                 std::string name, double minimum = 0, double maximum = 1, double value = 0)
{
    auto& control = state.controls[{device, kind, code}];
    control.info.source = {device, kind, code};
    control.info.name = std::move(name);
    control.info.value_mode = mode(kind);
    control.info.unit = unit(kind);
    control.info.minimum = minimum;
    control.info.maximum = maximum;
    control.value = value;
}

double axis(Sint16 value)
{
    return value < 0 ? value / 32768.0 : value / 32767.0;
}

Device* add_device(State& state, DeviceKind kind, std::uint64_t native_id, std::string& error)
{
    if (auto* existing = find_device(state, kind, native_id))
    {
        return existing;
    }
    if (state.next_connection == 0)
    {
        error = "Input connection identity exhausted";
        return nullptr;
    }
    Device device;
    device.data.kind = kind;
    device.native_id = native_id;
    device.data.connection_id = state.next_connection++;
    device.data.capabilities = 1ULL << static_cast<unsigned int>(kind);
    const auto connection = device.data.connection_id;
    if (kind == DeviceKind::keyboard)
    {
        device.data.name =
            native_id == 0 ? "Keyboard (aggregate)"
                           : text(SDL_GetKeyboardNameForID(static_cast<SDL_KeyboardID>(native_id)),
                                  "Keyboard");
        device.data.layout_key = native_id == 0 ? "sdl:keyboard:aggregate" : "sdl:keyboard";
        for (std::uint32_t code = 1; code < SDL_SCANCODE_COUNT; ++code)
        {
            add_control(state, connection, Kind::key, code,
                        text(SDL_GetScancodeName(static_cast<SDL_Scancode>(code)), "Scancode ") +
                            (SDL_GetScancodeName(static_cast<SDL_Scancode>(code))[0]
                                 ? ""
                                 : std::to_string(code)));
        }
    }
    else if (kind == DeviceKind::mouse)
    {
        device.data.name =
            native_id == 0
                ? "Mouse (aggregate)"
                : text(SDL_GetMouseNameForID(static_cast<SDL_MouseID>(native_id)), "Mouse");
        device.data.layout_key = native_id == 0 ? "sdl:mouse:aggregate" : "sdl:mouse";
        for (std::uint32_t code = 1; code <= 32; ++code)
        {
            add_control(state, connection, Kind::mouse_button, code,
                        "Mouse button " + std::to_string(code));
        }
        for (const auto k :
             {Kind::mouse_position, Kind::mouse_delta, Kind::mouse_relative, Kind::mouse_wheel})
        {
            for (std::uint32_t component = 0; component < 2; ++component)
            {
                add_control(state, connection, k, component, component == 0 ? "X" : "Y",
                            -std::numeric_limits<double>::max(),
                            std::numeric_limits<double>::max());
            }
        }
    }
    else if (kind == DeviceKind::touch)
    {
        device.data.name = text(SDL_GetTouchDeviceName(native_id), "Touchscreen");
        device.data.layout_key = "sdl:touch:" + std::to_string(SDL_GetTouchDeviceType(native_id));
        add_control(state, connection, Kind::touch, 0, "Screen contacts");
    }
    else
    {
        const auto id = static_cast<SDL_JoystickID>(native_id);
        if (kind == DeviceKind::gamepad)
        {
            device.gamepad = SDL_OpenGamepad(id);
            if (!device.gamepad)
            {
                error = SDL_GetError();
                return nullptr;
            }
            device.data.name = text(SDL_GetGamepadName(device.gamepad), "Gamepad");
            char guid[33]{};
            SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), guid, sizeof(guid));
            device.data.layout_key = guid;
            for (std::uint32_t code = 0; code < SDL_GAMEPAD_BUTTON_COUNT; ++code)
            {
                const auto button = static_cast<SDL_GamepadButton>(code);
                if (SDL_GamepadHasButton(device.gamepad, button))
                {
                    add_control(state, connection, Kind::gamepad_button, code,
                                text(SDL_GetGamepadStringForButton(button), "Gamepad button"), 0, 1,
                                SDL_GetGamepadButton(device.gamepad, button) ? 1 : 0);
                }
            }
            for (std::uint32_t code = 0; code < SDL_GAMEPAD_AXIS_COUNT; ++code)
            {
                const auto axis_id = static_cast<SDL_GamepadAxis>(code);
                if (SDL_GamepadHasAxis(device.gamepad, axis_id))
                {
                    const bool trigger = code >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
                    const auto value = axis(SDL_GetGamepadAxis(device.gamepad, axis_id));
                    add_control(state, connection, Kind::gamepad_axis, code,
                                text(SDL_GetGamepadStringForAxis(axis_id), "Gamepad axis"),
                                trigger ? 0 : -1, 1, trigger ? std::max(0.0, value) : value);
                }
            }
            const int touchpads = SDL_GetNumGamepadTouchpads(device.gamepad);
            for (int index = 0; index < touchpads; ++index)
            {
                add_control(state, connection, Kind::gamepad_touch,
                            static_cast<std::uint32_t>(index), "Touchpad " + std::to_string(index));
            }
        }
        else
        {
            device.joystick = SDL_OpenJoystick(id);
            if (!device.joystick)
            {
                error = SDL_GetError();
                return nullptr;
            }
            device.data.name = text(SDL_GetJoystickName(device.joystick), "Joystick");
            char guid[33]{};
            SDL_GUIDToString(SDL_GetJoystickGUID(device.joystick), guid, sizeof(guid));
            device.data.layout_key = guid;
            for (int code = 0; code < SDL_GetNumJoystickButtons(device.joystick); ++code)
            {
                add_control(state, connection, Kind::joystick_button,
                            static_cast<std::uint32_t>(code), "Button " + std::to_string(code), 0,
                            1, SDL_GetJoystickButton(device.joystick, code) ? 1 : 0);
            }
            for (int code = 0; code < SDL_GetNumJoystickAxes(device.joystick); ++code)
            {
                add_control(state, connection, Kind::joystick_axis,
                            static_cast<std::uint32_t>(code), "Axis " + std::to_string(code), -1, 1,
                            axis(SDL_GetJoystickAxis(device.joystick, code)));
            }
            for (int code = 0; code < SDL_GetNumJoystickHats(device.joystick); ++code)
            {
                const auto hat = SDL_GetJoystickHat(device.joystick, code);
                add_control(state, connection, Kind::joystick_hat,
                            static_cast<std::uint32_t>(code * 2),
                            "Hat " + std::to_string(code) + " X", -1, 1,
                            (hat & SDL_HAT_RIGHT ? 1 : 0) - (hat & SDL_HAT_LEFT ? 1 : 0));
                add_control(state, connection, Kind::joystick_hat,
                            static_cast<std::uint32_t>(code * 2 + 1),
                            "Hat " + std::to_string(code) + " Y", -1, 1,
                            (hat & SDL_HAT_DOWN ? 1 : 0) - (hat & SDL_HAT_UP ? 1 : 0));
            }
        }
    }
    state.devices.push_back(std::move(device));
    if (kind == DeviceKind::mouse)
    {
        float x = 0, y = 0;
        SDL_GetMouseState(&x, &y);
        state.controls[{connection, Kind::mouse_position, 0}].value = x;
        state.controls[{connection, Kind::mouse_position, 1}].value = y;
    }
    ++state.device_revision;
    return &state.devices.back();
}

void update(State& state, std::uint64_t connection, Kind kind, std::uint32_t code, double value,
            bool claimed = false, bool repeat = false)
{
    if (!std::isfinite(value))
    {
        return;
    }
    auto found = state.controls.find({connection, kind, code});
    if (found == state.controls.end())
    {
        // SDL mouse events report an 8-bit button index, beyond the 32-bit polling mask.
        if (kind != Kind::mouse_button || code == 0 || code > 255)
        {
            return;
        }
        add_control(state, connection, kind, code, "Mouse button " + std::to_string(code));
        ++state.device_revision;
        found = state.controls.find({connection, kind, code});
    }
    auto& control = found->second;
    const bool previous = std::abs(control.value) > 0.000001;
    if (control.info.value_mode == schema::InputValueMode::delta)
    {
        control.value += value;
        control.sample += value;
    }
    else
    {
        control.value = value;
    }
    const bool held = std::abs(control.value) > 0.000001;
    control.pressed |= !previous && held && !repeat;
    control.released |= previous && !held;
    control.repeat |= repeat;
    if (claimed && held)
    {
        control.suppressed = true;
        for (auto& channel : state.channels)
        {
            channel.neutralized.erase(found->first);
        }
    }
    if (!held)
    {
        control.suppressed = false;
    }
}

void aggregate_button(State& state, DeviceKind kind, Kind control_kind, std::uint32_t code)
{
    const auto* aggregate = find_device(state, kind, 0);
    if (!aggregate)
    {
        return;
    }
    bool held = kind == DeviceKind::keyboard ? state.unknown_keys.contains(code)
                                             : state.unknown_buttons.contains(code);
    for (const auto& device : state.devices)
    {
        if (device.data.kind == kind && device.native_id != 0)
        {
            const auto found = state.controls.find({device.data.connection_id, control_kind, code});
            held |= found != state.controls.end() && found->second.value != 0;
        }
    }
    update(state, aggregate->data.connection_id, control_kind, code, held ? 1 : 0);
}

void remove_device(State& state, DeviceKind kind, std::uint64_t native_id)
{
    auto found = std::find_if(
        state.devices.begin(), state.devices.end(), [kind, native_id](const Device& device)
        { return device.data.kind == kind && device.native_id == native_id; });
    if (found == state.devices.end())
    {
        return;
    }
    const auto connection = found->data.connection_id;
    for (auto& capture : state.captures)
    {
        if (capture.alive && capture.state.status == schema::InputCaptureStatus::waiting &&
            std::find(capture.options.devices.begin(), capture.options.devices.end(), connection) !=
                capture.options.devices.end())
        {
            capture.pending = false;
            capture.state.status = schema::InputCaptureStatus::canceled;
            capture.state.frame_id = state.frame_id;
        }
    }
    for (auto it = state.pointers.begin(); it != state.pointers.end();)
    {
        if (it->first.first == connection)
        {
            const auto channel = it->second.channel;
            ++it;
            cancel_pointers(state, channel);
            it = state.pointers.begin();
        }
        else
        {
            ++it;
        }
    }
    for (auto it = state.contacts.begin(); it != state.contacts.end();)
    {
        if (it->first.first == connection)
        {
            it = state.contacts.erase(it);
        }
        else
        {
            ++it;
        }
    }
    for (auto it = state.controls.begin(); it != state.controls.end();)
    {
        if (std::get<0>(it->first) == connection)
        {
            it = state.controls.erase(it);
        }
        else
        {
            ++it;
        }
    }
    if (found->gamepad)
    {
        SDL_CloseGamepad(found->gamepad);
    }
    if (found->joystick)
    {
        SDL_CloseJoystick(found->joystick);
    }
    state.devices.erase(found);
    ++state.device_revision;
    ++state.cancel_revision;
    if (kind == DeviceKind::keyboard)
    {
        for (std::uint32_t code = 1; code < SDL_SCANCODE_COUNT; ++code)
        {
            aggregate_button(state, kind, Kind::key, code);
        }
    }
    if (kind == DeviceKind::mouse)
    {
        for (std::uint32_t code = 1; code <= 255; ++code)
        {
            aggregate_button(state, kind, Kind::mouse_button, code);
        }
    }
    evaluate(state, false, true);
}

Device* event_device(State& state, DeviceKind kind, std::uint64_t native_id, std::string& error)
{
    auto* device = find_device(state, kind, native_id);
    return device ? device : add_device(state, kind, native_id, error);
}
} // namespace

bool enumerate(State& state, std::string& error)
{
    // SDL's polling APIs aggregate keyboards and mice. Keep that fact explicit and also expose
    // reported individual event sources when the platform provides their identities.
    if (!add_device(state, DeviceKind::keyboard, 0, error) ||
        !add_device(state, DeviceKind::mouse, 0, error))
    {
        return false;
    }
    int count = 0;
    auto* keyboards = SDL_GetKeyboards(&count);
    for (int i = 0; i < count; ++i)
    {
        if (!add_device(state, DeviceKind::keyboard, keyboards[i], error))
        {
            SDL_free(keyboards);
            return false;
        }
    }
    SDL_free(keyboards);
    auto* mice = SDL_GetMice(&count);
    for (int i = 0; i < count; ++i)
    {
        if (!add_device(state, DeviceKind::mouse, mice[i], error))
        {
            SDL_free(mice);
            return false;
        }
    }
    SDL_free(mice);
    auto* joysticks = SDL_GetJoysticks(&count);
    for (int i = 0; i < count; ++i)
    {
        if (!add_device(state,
                        SDL_IsGamepad(joysticks[i]) ? DeviceKind::gamepad : DeviceKind::joystick,
                        joysticks[i], error))
        {
            SDL_free(joysticks);
            return false;
        }
    }
    SDL_free(joysticks);
    auto* touches = SDL_GetTouchDevices(&count);
    for (int i = 0; i < count; ++i)
    {
        if (touches[i] != SDL_MOUSE_TOUCHID &&
            !add_device(state, DeviceKind::touch, touches[i], error))
        {
            SDL_free(touches);
            return false;
        }
    }
    SDL_free(touches);
    const auto* keys = SDL_GetKeyboardState(&count);
    const auto keyboard = find_device(state, DeviceKind::keyboard, 0)->data.connection_id;
    for (int code = 1; code < count; ++code)
    {
        if (keys[code])
        {
            state.unknown_keys.insert(static_cast<std::uint32_t>(code));
        }
        state.controls[{keyboard, Kind::key, static_cast<std::uint32_t>(code)}].value =
            keys[code] ? 1 : 0;
    }
    float x = 0, y = 0;
    const auto buttons = SDL_GetMouseState(&x, &y);
    const auto mouse = find_device(state, DeviceKind::mouse, 0)->data.connection_id;
    for (std::uint32_t code = 1; code <= 32; ++code)
    {
        const bool held = (buttons & (1U << (code - 1))) != 0;
        state.controls[{mouse, Kind::mouse_button, code}].value = held ? 1 : 0;
        if (held)
        {
            state.unknown_buttons.insert(code);
        }
    }
    state.controls[{mouse, Kind::mouse_position, 0}].value = x;
    state.controls[{mouse, Kind::mouse_position, 1}].value = y;
    return true;
}

void close_devices(State& state) noexcept
{
    for (auto& device : state.devices)
    {
        if (device.gamepad)
        {
            SDL_CloseGamepad(device.gamepad);
        }
        if (device.joystick)
        {
            SDL_CloseJoystick(device.joystick);
        }
    }
    state.devices.clear();
}

bool event(State& state, SDL_Window* window, const SDL_Event& value, bool claimed,
           std::string& error)
{
    const auto own_window = SDL_GetWindowID(window);
    SDL_WindowID event_window = own_window;
    if (value.type >= SDL_EVENT_WINDOW_FIRST && value.type <= SDL_EVENT_WINDOW_LAST)
    {
        event_window = value.window.windowID;
    }
    else if (value.type == SDL_EVENT_KEY_DOWN || value.type == SDL_EVENT_KEY_UP)
    {
        event_window = value.key.windowID;
    }
    else if (value.type == SDL_EVENT_MOUSE_MOTION)
    {
        event_window = value.motion.windowID;
    }
    else if (value.type == SDL_EVENT_MOUSE_BUTTON_DOWN || value.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        event_window = value.button.windowID;
    }
    else if (value.type == SDL_EVENT_MOUSE_WHEEL)
    {
        event_window = value.wheel.windowID;
    }
    else if (value.type == SDL_EVENT_FINGER_DOWN || value.type == SDL_EVENT_FINGER_UP ||
             value.type == SDL_EVENT_FINGER_MOTION || value.type == SDL_EVENT_FINGER_CANCELED)
    {
        event_window = value.tfinger.windowID;
    }
    if (event_window != own_window)
    {
        return true;
    }
    ++state.sample_serial;
    for (auto& [control_key, control] : state.controls)
    {
        (void)control_key;
        control.sample = 0;
    }
    for (auto& channel : state.channels)
    {
        for (auto& [id, v] : channel.virtuals)
        {
            (void)id;
            v.sample_x = v.sample_y = 0;
        }
    }
    switch (value.type)
    {
    case SDL_EVENT_KEYBOARD_ADDED:
        return add_device(state, DeviceKind::keyboard, value.kdevice.which, error) != nullptr;
    case SDL_EVENT_MOUSE_ADDED:
        return value.mdevice.which == SDL_TOUCH_MOUSEID ||
               add_device(state, DeviceKind::mouse, value.mdevice.which, error) != nullptr;
    case SDL_EVENT_JOYSTICK_ADDED:
    case SDL_EVENT_GAMEPAD_ADDED:
    {
        const auto id = value.jdevice.which;
        int count = 0;
        auto* attached = SDL_GetJoysticks(&count);
        bool present = false;
        for (int index = 0; index < count; ++index)
        {
            present |= attached[index] == id;
        }
        SDL_free(attached);
        if (!present)
        {
            return true;
        } // A queued add can be followed by removal before this pump.
        const auto kind = SDL_IsGamepad(id) ? DeviceKind::gamepad : DeviceKind::joystick;
        // A joystick can acquire a standard mapping after it was opened.
        if (kind == DeviceKind::gamepad && find_device(state, DeviceKind::joystick, id))
        {
            remove_device(state, DeviceKind::joystick, id);
        }
        return add_device(state, kind, id, error) != nullptr;
    }
    case SDL_EVENT_KEYBOARD_REMOVED:
        remove_device(state, DeviceKind::keyboard, value.kdevice.which);
        return true;
    case SDL_EVENT_MOUSE_REMOVED:
        remove_device(state, DeviceKind::mouse, value.mdevice.which);
        return true;
    case SDL_EVENT_GAMEPAD_REMOVED:
    case SDL_EVENT_JOYSTICK_REMOVED:
        remove_device(state, DeviceKind::gamepad, value.jdevice.which);
        remove_device(state, DeviceKind::joystick, value.jdevice.which);
        return true;
    case SDL_EVENT_GAMEPAD_REMAPPED:
    {
        const auto id = value.gdevice.which;
        // Re-open to refresh the mapping and its advertised controls, retaining this connection.
        auto* device = find_device(state, DeviceKind::gamepad, id);
        if (device)
        {
            const auto connection = device->data.connection_id;
            remove_device(state, DeviceKind::gamepad, id);
            device = add_device(state, DeviceKind::gamepad, id, error);
            if (!device)
            {
                return false;
            }
            const auto replacement = device->data.connection_id;
            device->data.connection_id = connection;
            std::map<Key, Control> refreshed;
            for (auto it = state.controls.begin(); it != state.controls.end();)
            {
                if (std::get<0>(it->first) == replacement)
                {
                    auto control = std::move(it->second);
                    const Key control_key{connection, std::get<1>(it->first),
                                          std::get<2>(it->first)};
                    control.info.source = source(control_key);
                    control.suppressed = std::abs(control.value) > 0.000001;
                    control.canceled = control.suppressed;
                    refreshed.emplace(control_key, std::move(control));
                    it = state.controls.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            state.controls.merge(refreshed);
            evaluate(state, false, true);
        }
        return true;
    }
    case SDL_EVENT_KEYMAP_CHANGED:
        ++state.device_revision;
        cancel(state, true, false, false);
        return true;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        cancel(state, true, true, true);
        return true;
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
        cancel(state, false, true, false);
        return true;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    {
        if (value.key.scancode <= SDL_SCANCODE_UNKNOWN || value.key.scancode >= SDL_SCANCODE_COUNT)
        {
            return true;
        }
        const auto id = value.key.which;
        auto* device = event_device(state, DeviceKind::keyboard, id, error);
        if (!device)
        {
            return false;
        }
        const auto code = static_cast<std::uint32_t>(value.key.scancode);
        const bool claim = claimed || state.routing.keyboard_claimed;
        update(state, device->data.connection_id, Kind::key, code, value.key.down ? 1 : 0, claim,
               value.key.repeat);
        if (id == 0)
        {
            if (value.key.down)
            {
                state.unknown_keys.insert(code);
            }
            else
            {
                state.unknown_keys.erase(code);
            }
        }
        else
        {
            state.unknown_keys.erase(code); // The aggregate startup baseline now has a known owner.
            aggregate_button(state, DeviceKind::keyboard, Kind::key, code);
            auto& aggregate = state.controls[{
                find_device(state, DeviceKind::keyboard, 0)->data.connection_id, Kind::key, code}];
            aggregate.repeat |= value.key.repeat;
            if (claim && value.key.down)
            {
                aggregate.suppressed = true;
            }
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        if (value.button.which == SDL_TOUCH_MOUSEID)
        {
            return true;
        }
        auto* device = event_device(state, DeviceKind::mouse, value.button.which, error);
        if (!device)
        {
            return false;
        }
        const auto code = value.button.button;
        const bool claim = claimed || state.routing.pointer_claimed;
        update(state, device->data.connection_id, Kind::mouse_button, code,
               value.button.down ? 1 : 0, claim);
        if (value.button.which == 0)
        {
            if (value.button.down)
            {
                state.unknown_buttons.insert(code);
            }
            else
            {
                state.unknown_buttons.erase(code);
            }
        }
        else
        {
            state.unknown_buttons.erase(code);
            aggregate_button(state, DeviceKind::mouse, Kind::mouse_button, code);
            if (claim && value.button.down)
            {
                state
                    .controls[{find_device(state, DeviceKind::mouse, 0)->data.connection_id,
                               Kind::mouse_button, code}]
                    .suppressed = true;
            }
        }
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
    {
        if (value.motion.which == SDL_TOUCH_MOUSEID)
        {
            return true;
        }
        auto* device = event_device(state, DeviceKind::mouse, value.motion.which, error);
        if (!device)
        {
            return false;
        }
        const auto connection = device->data.connection_id;
        const auto aggregate = find_device(state, DeviceKind::mouse, 0)->data.connection_id;
        for (const auto target : {connection, aggregate})
        {
            const bool relative = SDL_GetWindowRelativeMouseMode(window);
            const double previous_x = state.controls[{target, Kind::mouse_position, 0}].value;
            const double previous_y = state.controls[{target, Kind::mouse_position, 1}].value;
            const bool claim = claimed || state.routing.pointer_claimed;
            update(state, target, Kind::mouse_position, 0, value.motion.x, claim);
            update(state, target, Kind::mouse_position, 1, value.motion.y, claim);
            update(state, target, relative ? Kind::mouse_relative : Kind::mouse_delta, 0,
                   relative ? value.motion.xrel : value.motion.x - previous_x, claim);
            update(state, target, relative ? Kind::mouse_relative : Kind::mouse_delta, 1,
                   relative ? value.motion.yrel : value.motion.y - previous_y, claim);
            if (connection == aggregate)
            {
                break;
            }
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
    {
        if (value.wheel.which == SDL_TOUCH_MOUSEID)
        {
            return true;
        }
        auto* device = event_device(state, DeviceKind::mouse, value.wheel.which, error);
        if (!device)
        {
            return false;
        }
        const auto connection = device->data.connection_id;
        const auto aggregate = find_device(state, DeviceKind::mouse, 0)->data.connection_id;
        const auto direction = value.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
        for (const auto target : {connection, aggregate})
        {
            update(state, target, Kind::mouse_wheel, 0, value.wheel.x * direction,
                   claimed || state.routing.pointer_claimed);
            update(state, target, Kind::mouse_wheel, 1, value.wheel.y * direction,
                   claimed || state.routing.pointer_claimed);
            if (connection == aggregate)
            {
                break;
            }
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    {
        auto* device = find_device(state, DeviceKind::gamepad, value.gbutton.which);
        if (device)
        {
            update(state, device->data.connection_id, Kind::gamepad_button, value.gbutton.button,
                   value.gbutton.down ? 1 : 0, claimed);
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    {
        auto* device = find_device(state, DeviceKind::gamepad, value.gaxis.which);
        if (device)
        {
            const auto v = axis(value.gaxis.value);
            update(state, device->data.connection_id, Kind::gamepad_axis, value.gaxis.axis,
                   value.gaxis.axis >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? std::max(0.0, v) : v,
                   claimed);
        }
        break;
    }
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP:
    {
        auto* device = find_device(state, DeviceKind::joystick, value.jbutton.which);
        if (device)
        {
            update(state, device->data.connection_id, Kind::joystick_button, value.jbutton.button,
                   value.jbutton.down ? 1 : 0, claimed);
        }
        break;
    }
    case SDL_EVENT_JOYSTICK_AXIS_MOTION:
    {
        auto* device = find_device(state, DeviceKind::joystick, value.jaxis.which);
        if (device)
        {
            update(state, device->data.connection_id, Kind::joystick_axis, value.jaxis.axis,
                   axis(value.jaxis.value), claimed);
        }
        break;
    }
    case SDL_EVENT_JOYSTICK_HAT_MOTION:
    {
        auto* device = find_device(state, DeviceKind::joystick, value.jhat.which);
        if (device)
        {
            update(state, device->data.connection_id, Kind::joystick_hat, value.jhat.hat * 2,
                   (value.jhat.value & SDL_HAT_RIGHT ? 1 : 0) -
                       (value.jhat.value & SDL_HAT_LEFT ? 1 : 0),
                   claimed);
            update(state, device->data.connection_id, Kind::joystick_hat, value.jhat.hat * 2 + 1,
                   (value.jhat.value & SDL_HAT_DOWN ? 1 : 0) -
                       (value.jhat.value & SDL_HAT_UP ? 1 : 0),
                   claimed);
        }
        break;
    }
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        if (!std::isfinite(value.tfinger.x) || !std::isfinite(value.tfinger.y) ||
            !std::isfinite(value.tfinger.pressure))
        {
            error = "Non-finite touch event";
            return false;
        }
        if (value.tfinger.touchID == SDL_MOUSE_TOUCHID)
        {
            return true;
        }
        if (!event_device(state, DeviceKind::touch, value.tfinger.touchID, error))
        {
            return false;
        }
        touch(state, window, value.tfinger, claimed);
        capture_candidates(state);
        return true;
    case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
    {
        if (value.gtouchpad.touchpad < 0 || value.gtouchpad.finger < 0 ||
            !std::isfinite(value.gtouchpad.x) || !std::isfinite(value.gtouchpad.y) ||
            !std::isfinite(value.gtouchpad.pressure))
        {
            error = "Invalid gamepad touch event";
            return false;
        }
        auto* device = find_device(state, DeviceKind::gamepad, value.gtouchpad.which);
        if (!device)
        {
            break;
        }
        const auto connection = device->data.connection_id;
        const auto contact_id = (static_cast<std::uint64_t>(value.gtouchpad.touchpad) << 32) |
                                static_cast<std::uint32_t>(value.gtouchpad.finger);
        const ContactKey id{connection, contact_id};
        if (value.type == SDL_EVENT_GAMEPAD_TOUCHPAD_UP)
        {
            state.contacts.erase(id);
        }
        else
        {
            auto& contact = state.contacts[id];
            contact.data.source = {connection, Kind::gamepad_touch,
                                   static_cast<std::uint32_t>(value.gtouchpad.touchpad)};
            contact.data.contact_id = contact_id;
            contact.data.normalized_x = value.gtouchpad.x;
            contact.data.normalized_y = value.gtouchpad.y;
            contact.data.pressure = value.gtouchpad.pressure;
            contact.data.held = true;
            contact.suppressed |= claimed;
        }
        const bool held =
            std::any_of(state.contacts.begin(), state.contacts.end(),
                        [connection, &value](const auto& entry)
                        {
                            return entry.first.first == connection &&
                                   entry.second.data.source.code() ==
                                       static_cast<std::uint32_t>(value.gtouchpad.touchpad);
                        });
        update(state, connection, Kind::gamepad_touch,
               static_cast<std::uint32_t>(value.gtouchpad.touchpad), held ? 1 : 0, claimed);
        break;
    }
    default:
        return true;
    }
    capture_candidates(state);
    evaluate(state);
    return true;
}

void publish(State& state)
{
    state.published_devices.clear();
    for (const auto& device : state.devices)
    {
        state.published_devices.push_back(device.data);
    }
    state.published_controls.clear();
    state.published_info.clear();
    for (const auto& [control_key, control] : state.controls)
    {
        state.published_info.push_back(control.info);
        state.published_controls.emplace(
            control_key,
            schema::InputControlState{source(control_key), state.frame_id, control.value,
                                      std::abs(control.value) > 0.000001, control.pressed,
                                      control.released, control.canceled, control.repeat});
    }
    state.published_contacts.clear();
    for (const auto& [id, contact] : state.contacts)
    {
        (void)id;
        state.published_contacts.push_back(contact.data);
    }
    for (auto& channel : state.channels)
    {
        if (!channel.alive)
        {
            continue;
        }
        channel.published.clear();
        for (const auto& [name, action] : channel.actions)
        {
            channel.published.emplace(name, action.state);
        }
    }
    for (auto& capture : state.captures)
    {
        if (capture.alive)
        {
            capture.published = capture.state;
        }
    }
}
} // namespace engine::input_detail
