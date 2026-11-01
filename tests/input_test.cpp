#include "runtime/engine_config.hpp"
#include "runtime/input/input_system.hpp"
#include "runtime/window/window_system.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <memory>

namespace
{
using namespace engine;
using Kind = schema::InputControlKind;
SDL_WindowID test_window_id = 0;

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "Input test: %s\n", message);
    }
    return condition;
}

bool frame(InputSystem& input)
{
    return input.begin_frame() && input.set_routing_state(schema::InputRoutingDataT{});
}

bool key_event(InputSystem& input, SDL_Scancode code, bool down, bool claimed = false,
               bool repeat = false)
{
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.which = 0;
    event.key.windowID = test_window_id;
    event.key.scancode = code;
    event.key.down = down;
    event.key.repeat = repeat;
    return input.process_event(event, claimed);
}

bool mouse_button(InputSystem& input, bool down)
{
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.windowID = test_window_id;
    event.button.button = SDL_BUTTON_RIGHT;
    event.button.down = down;
    return input.process_event(event, false);
}

bool motion(InputSystem& input, float x)
{
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = test_window_id;
    event.motion.x = x;
    return input.process_event(event, false);
}

bool finger(InputSystem& input, SDL_EventType type, SDL_FingerID id, float x, bool claimed = false)
{
    SDL_Event event{};
    event.type = type;
    event.tfinger.windowID = test_window_id;
    event.tfinger.touchID = 777;
    event.tfinger.fingerID = id;
    event.tfinger.x = x;
    event.tfinger.y = 0.1F;
    event.tfinger.pressure = 0.75F;
    return input.process_event(event, claimed);
}

bool pad_event(InputSystem& input, SDL_JoystickID id, SDL_EventType type, Sint16 value = 0)
{
    SDL_Event event{};
    event.type = type;
    if (type == SDL_EVENT_GAMEPAD_AXIS_MOTION)
    {
        event.gaxis.which = id;
        event.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX;
        event.gaxis.value = value;
    }
    else
    {
        event.gdevice.which = id;
    }
    return input.process_event(event, false);
}

std::unique_ptr<schema::InputBindingDataT> binding(schema::InputControlRef source,
                                                   double weight_x = 1, double weight_y = 0)
{
    auto result = std::make_unique<schema::InputBindingDataT>();
    auto part = std::make_unique<schema::InputBindingPartDataT>();
    part->source = source;
    part->weight_x = weight_x;
    part->weight_y = weight_y;
    result->parts.push_back(std::move(part));
    return result;
}

schema::InputChannelDataT configuration(std::uint64_t keyboard)
{
    schema::InputChannelDataT result;
    auto fire = std::make_unique<schema::InputActionDataT>();
    fire->name = "fire";
    fire->bindings.push_back(binding({keyboard, Kind::key, SDL_SCANCODE_F}));
    fire->bindings.push_back(binding({keyboard, Kind::key, SDL_SCANCODE_G}));
    result.actions.push_back(std::move(fire));
    auto move = std::make_unique<schema::InputActionDataT>();
    move->name = "move";
    move->kind = schema::InputActionKind::axis2d;
    auto directions = binding({keyboard, Kind::key, SDL_SCANCODE_D}, 1, 0);
    auto forward = std::make_unique<schema::InputBindingPartDataT>();
    forward->source = {keyboard, Kind::key, SDL_SCANCODE_W};
    forward->weight_x = 0;
    forward->weight_y = 1;
    directions->parts.push_back(std::move(forward));
    move->bindings.push_back(std::move(directions));
    result.actions.push_back(std::move(move));
    return result;
}

bool run()
{
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    WindowSystem window;
    EngineConfig config;
    if (!expect(window.initialize(config), "window initializes"))
    {
        return false;
    }
    InputSystem input(window);
    test_window_id = SDL_GetWindowID(window.native_window());
    if (!expect(input.initialize(config), "input initializes"))
    {
        return false;
    }
    std::uint64_t keyboard = 0;
    std::uint64_t mouse = 0;
    for (const auto& device : input.devices())
    {
        if (device.kind == schema::InputDeviceKind::keyboard)
        {
            keyboard = device.connection_id;
            break;
        }
    }
    for (const auto& device : input.devices())
    {
        if (device.kind == schema::InputDeviceKind::mouse)
        {
            mouse = device.connection_id;
            break;
        }
    }
    if (!expect(keyboard != 0 && input.frame_state().frame_id() == 0, "initial baseline published"))
    {
        return false;
    }
    schema::InputChannelHandle channel;
    if (!input.create_channel(channel) ||
        !input.configure_channel(channel, configuration(keyboard)) || !frame(input) ||
        !input.tick(0))
    {
        return false;
    }
    schema::InputActionState state;
    if (!frame(input) || !key_event(input, SDL_SCANCODE_F, true) ||
        !key_event(input, SDL_SCANCODE_F, false))
    {
        return false;
    }
    if (!expect(input.read_action(channel, "fire", state) && !state.pressed(),
                "pump leaves published snapshot unchanged") ||
        !input.tick(0))
    {
        return false;
    }
    if (!expect(input.read_action(channel, "fire", state) && state.pressed() && state.released() &&
                    !state.held(),
                "same-frame tap keeps both edges"))
    {
        return false;
    }
    schema::InputActionState second;
    if (!expect(input.read_action(channel, "fire", second) && second == state,
                "pressed is non-consuming"))
    {
        return false;
    }
    if (!frame(input) || !key_event(input, SDL_SCANCODE_F, true) ||
        !key_event(input, SDL_SCANCODE_G, true) || !key_event(input, SDL_SCANCODE_F, false) ||
        !input.tick(0))
    {
        return false;
    }
    if (!expect(input.read_action(channel, "fire", state) && state.held() && state.pressed() &&
                    !state.released(),
                "independent sources aggregate before edges"))
    {
        return false;
    }
    if (!frame(input) || !key_event(input, SDL_SCANCODE_G, true, false, true) || !input.tick(0))
    {
        return false;
    }
    if (!expect(input.read_action(channel, "fire", state) && state.held() && !state.pressed(),
                "repeat does not press"))
    {
        return false;
    }
    if (!frame(input))
    {
        return false;
    }
    schema::InputRoutingDataT routing;
    routing.keyboard_claimed = true;
    if (!input.set_routing_state(routing) || !input.tick(0))
    {
        return false;
    }
    if (!expect(input.read_action(channel, "fire", state) && state.canceled() && !state.released(),
                "UI takeover cancels"))
    {
        return false;
    }
    if (!frame(input) || !input.tick(0))
    {
        return false;
    }
    if (!expect(input.read_action(channel, "fire", state) && !state.held(),
                "UI dismissal waits for release"))
    {
        return false;
    }
    if (!frame(input) || !key_event(input, SDL_SCANCODE_G, false) ||
        !key_event(input, SDL_SCANCODE_G, true) || !key_event(input, SDL_SCANCODE_D, true) ||
        !key_event(input, SDL_SCANCODE_W, true) || !input.tick(0))
    {
        return false;
    }
    if (!expect(input.read_action(channel, "move", state) &&
                    std::abs(std::hypot(state.value_x(), state.value_y()) - 1) < 0.00001,
                "WASD combined then normalized"))
    {
        return false;
    }
    auto bad = configuration(keyboard);
    bad.actions[0]->bindings[0]->deadzone = 2;
    if (!expect(!input.configure_channel(channel, bad),
                "invalid config refused without losing active config"))
    {
        return false;
    }
    schema::InputCaptureHandle capture;
    schema::InputCaptureOptionsT options;
    options.devices.push_back(keyboard);
    options.kinds.push_back(Kind::key);
    if (!input.begin_capture(channel, options, capture) || !frame(input) ||
        !key_event(input, SDL_SCANCODE_G, true) || !input.tick(0))
    {
        return false;
    }
    schema::InputCaptureStateT captured;
    if (!expect(input.read_capture(capture, captured) &&
                    captured.status == schema::InputCaptureStatus::waiting,
                "capture ignores initially held controls"))
    {
        return false;
    }
    if (!frame(input) || !key_event(input, SDL_SCANCODE_G, false) ||
        !key_event(input, SDL_SCANCODE_H, true) || !input.tick(0))
    {
        return false;
    }
    if (!expect(input.read_capture(capture, captured) &&
                    captured.status == schema::InputCaptureStatus::completed &&
                    captured.candidates.back().code() == SDL_SCANCODE_H,
                "capture completes with new source"))
    {
        return false;
    }
    auto extended = configuration(keyboard);
    auto look = std::make_unique<schema::InputActionDataT>();
    look->name = "look";
    look->kind = schema::InputActionKind::axis1d;
    look->value_mode = schema::InputValueMode::delta;
    auto look_binding = binding({mouse, Kind::mouse_delta, 0});
    look_binding->modifiers.emplace_back(mouse, Kind::mouse_button, SDL_BUTTON_RIGHT);
    look->bindings.push_back(std::move(look_binding));
    extended.actions.push_back(std::move(look));
    if (!input.configure_channel(channel, extended) || !frame(input) || !motion(input, 0) ||
        !input.tick(0) || !frame(input) || !mouse_button(input, true) || !motion(input, 10) ||
        !mouse_button(input, false) || !input.tick(0))
    {
        return false;
    }
    if (!expect(input.read_action(channel, "look", state) && state.value_x() == 10,
                "modifier release preserves preceding same-frame delta"))
    {
        return false;
    }
    if (!frame(input) || !motion(input, 20) || !mouse_button(input, true) || !input.tick(0))
    {
        return false;
    }
    if (!expect(input.read_action(channel, "look", state) && state.value_x() == 0,
                "modifier press cannot reclaim preceding delta"))
    {
        return false;
    }

    // A disconnected alternate source must not turn a normal keyboard release into cancellation.
    extended.actions[0]->bindings.push_back(binding({999999, Kind::gamepad_button, 0}));
    if (!input.configure_channel(channel, extended) || !frame(input) ||
        !key_event(input, SDL_SCANCODE_G, false) || !key_event(input, SDL_SCANCODE_F, true) ||
        !key_event(input, SDL_SCANCODE_F, false) || !input.tick(0))
    {
        return false;
    }
    if (!expect(input.read_action(channel, "fire", state) && state.released() && !state.canceled(),
                "offline alternate binding does not contaminate release"))
    {
        return false;
    }

    if (!frame(input) || !finger(input, SDL_EVENT_FINGER_DOWN, 1, 0.1F) ||
        !finger(input, SDL_EVENT_FINGER_UP, 1, 0.1F) || !input.tick(0))
    {
        return false;
    }
    std::uint64_t touch = 0;
    for (const auto& device : input.devices())
    {
        if (device.kind == schema::InputDeviceKind::touch)
        {
            touch = device.connection_id;
        }
    }
    auto touch_action = std::make_unique<schema::InputActionDataT>();
    touch_action->name = "touch";
    touch_action->bindings.push_back(binding({touch, Kind::touch, 0}));
    extended.actions.push_back(std::move(touch_action));
    auto button = std::make_unique<schema::InputVirtualControlDataT>();
    button->id = "touch_fire";
    button->region = std::make_unique<schema::InputRegionDataT>();
    button->region->connection_id = touch;
    button->region->width = 300;
    button->region->height = 300;
    extended.virtual_controls.push_back(std::move(button));
    if (!input.configure_channel(channel, extended) || !frame(input) ||
        !finger(input, SDL_EVENT_FINGER_DOWN, 2, 0.1F) ||
        !finger(input, SDL_EVENT_FINGER_DOWN, 3, 0.8F, true) || !input.tick(0))
    {
        return false;
    }
    if (!expect(input.read_action(channel, "touch", state) && state.held() && !state.canceled(),
                "UI-owned finger does not suppress independent game finger"))
    {
        return false;
    }
    SDL_Event pointer{};
    pointer.type = SDL_EVENT_FINGER_MOTION;
    pointer.tfinger.windowID = test_window_id;
    pointer.tfinger.touchID = 777;
    pointer.tfinger.fingerID = 2;
    if (!expect(input.owns_pointer(pointer), "virtual touch keeps immediate pointer ownership"))
    {
        return false;
    }
    if (!frame(input) || !finger(input, SDL_EVENT_FINGER_UP, 2, 0.1F) || !input.tick(0))
    {
        return false;
    }
    if (!expect(input.read_action(channel, "touch", state) && state.released() && !state.canceled(),
                "normal game finger release remains release while UI finger stays down"))
    {
        return false;
    }

    SDL_VirtualJoystickDesc pad_desc{};
    SDL_INIT_INTERFACE(&pad_desc);
    pad_desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    pad_desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    pad_desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    pad_desc.axis_mask = (1U << SDL_GAMEPAD_AXIS_COUNT) - 1;
    pad_desc.button_mask = (1U << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    pad_desc.name = "Input test gamepad";
    const auto pad_one = SDL_AttachVirtualJoystick(&pad_desc);
    const auto pad_two = SDL_AttachVirtualJoystick(&pad_desc);
    if (!expect(pad_one != 0 && pad_two != 0, "two virtual gamepads attach") || !frame(input) ||
        !pad_event(input, pad_one, SDL_EVENT_GAMEPAD_ADDED) ||
        !pad_event(input, pad_two, SDL_EVENT_GAMEPAD_ADDED) ||
        !pad_event(input, pad_one, SDL_EVENT_GAMEPAD_AXIS_MOTION, 300) || !input.tick(0))
    {
        return false;
    }
    std::uint64_t pad_connection = 0;
    for (const auto& device : input.devices())
    {
        if (device.kind == schema::InputDeviceKind::gamepad)
        {
            pad_connection = device.connection_id;
            break;
        }
    }
    auto stick = std::make_unique<schema::InputActionDataT>();
    stick->name = "stick";
    stick->kind = schema::InputActionKind::axis1d;
    auto stick_binding = binding({pad_connection, Kind::gamepad_axis, SDL_GAMEPAD_AXIS_LEFTX});
    stick_binding->deadzone = 0.2;
    stick->bindings.push_back(std::move(stick_binding));
    extended.actions.push_back(std::move(stick));
    if (!input.configure_channel(channel, extended) || !frame(input) ||
        !pad_event(input, pad_two, SDL_EVENT_GAMEPAD_AXIS_MOTION, 20000) || !input.tick(0))
    {
        return false;
    }
    if (!expect(input.read_action(channel, "stick", state) && !state.held(),
                "second gamepad does not drive first binding"))
    {
        return false;
    }
    if (!frame(input) || !pad_event(input, pad_one, SDL_EVENT_GAMEPAD_AXIS_MOTION, 20000) ||
        !input.tick(0))
    {
        return false;
    }
    if (!expect(input.read_action(channel, "stick", state) && state.value_x() > 0.1,
                "deadzone accepts drifting neutral before actuation"))
    {
        return false;
    }
    SDL_Event focus{};
    focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    focus.window.windowID = test_window_id;
    if (!frame(input) || !input.process_event(focus, false) || !input.tick(0))
    {
        return false;
    }
    if (!expect(input.read_action(channel, "stick", state) && state.canceled() && !state.released(),
                "focus loss cancels analog contribution"))
    {
        return false;
    }
    if (!frame(input) || !pad_event(input, pad_one, SDL_EVENT_GAMEPAD_AXIS_MOTION, 300) ||
        !pad_event(input, pad_one, SDL_EVENT_GAMEPAD_AXIS_MOTION, 20000) || !input.tick(0))
    {
        return false;
    }
    if (!expect(input.read_action(channel, "stick", state) && state.held(),
                "drifting axis re-arms after safe cancellation"))
    {
        return false;
    }
    if (!frame(input) || !pad_event(input, pad_one, SDL_EVENT_GAMEPAD_REMOVED) || !input.tick(0))
    {
        return false;
    }
    if (!expect(input.read_action(channel, "stick", state) && state.canceled() && !state.released(),
                "disconnect cancels final source"))
    {
        return false;
    }
    if (!SDL_DetachVirtualJoystick(pad_one) || !SDL_DetachVirtualJoystick(pad_two))
    {
        return false;
    }

    if (!input.destroy_channel(channel))
    {
        return false;
    }
    if (!expect(!input.read_action(channel, "fire", state) &&
                    !input.read_capture(capture, captured),
                "destroy invalidates owned state"))
    {
        return false;
    }
    schema::InputChannelHandle replacement;
    if (!input.create_channel(replacement) ||
        !expect(replacement.generation() != channel.generation(), "channel generation changes"))
    {
        return false;
    }
    input.shutdown();
    return input.initialize(config) && input.create_channel(channel) &&
           expect(channel.generation() != replacement.generation(),
                  "shutdown does not revive handles") &&
           expect(input.devices().front().connection_id != keyboard,
                  "shutdown does not reuse connection identities");
}
} // namespace

int main()
{
    return run() ? 0 : 1;
}
