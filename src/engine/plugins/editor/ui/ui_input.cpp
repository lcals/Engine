#include "plugins/editor/ui/ui_editor_state.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace engine::plugins::ui
{
namespace
{
std::uint8_t button(std::string_view name)
{
    if (name == "left")
    {
        return SDL_BUTTON_LEFT;
    }
    if (name == "middle")
    {
        return SDL_BUTTON_MIDDLE;
    }
    if (name == "right")
    {
        return SDL_BUTTON_RIGHT;
    }
    return 0;
}
std::uint16_t modifier(SDL_Keycode key)
{
    switch (key)
    {
    case SDLK_LSHIFT:
        return SDL_KMOD_LSHIFT;
    case SDLK_RSHIFT:
        return SDL_KMOD_RSHIFT;
    case SDLK_LCTRL:
        return SDL_KMOD_LCTRL;
    case SDLK_RCTRL:
        return SDL_KMOD_RCTRL;
    case SDLK_LALT:
        return SDL_KMOD_LALT;
    case SDLK_RALT:
        return SDL_KMOD_RALT;
    case SDLK_LGUI:
        return SDL_KMOD_LGUI;
    case SDLK_RGUI:
        return SDL_KMOD_RGUI;
    default:
        return 0;
    }
}
bool same(UiElementHandle a, UiElementHandle b)
{
    return a.document_id == b.document_id && a.element_id == b.element_id;
}
bool descendant(UiSystem& ui, UiElementHandle hit, UiElementHandle target)
{
    for (std::size_t depth = 0; depth < 256 && hit.document_id == target.document_id; ++depth)
    {
        if (same(hit, target))
        {
            return true;
        }
        UiElementInfo info;
        if (!ui.inspect_element(hit, info))
        {
            return false;
        }
        hit = info.parent;
    }
    return false;
}
bool native_pressed()
{
    if (SDL_GetMouseState(nullptr, nullptr) != 0)
    {
        return true;
    }
    int count = 0;
    const auto* state = SDL_GetKeyboardState(&count);
    for (int i = 0; state && i < count; ++i)
    {
        if (state[i])
        {
            return true;
        }
    }
    int devices_count = 0;
    auto* devices = SDL_GetTouchDevices(&devices_count);
    bool pressed = false;
    for (int i = 0; devices && i < devices_count; ++i)
    {
        int fingers_count = 0;
        auto** fingers = SDL_GetTouchFingers(devices[i], &fingers_count);
        pressed = pressed || fingers_count > 0;
        SDL_free(fingers);
    }
    SDL_free(devices);
    return pressed;
}
Json receipt(const InputJob& job)
{
    return {{"job", job.id},
            {"status", job.status},
            {"completedEvents", job.completed},
            {"eventCount", job.events.size()},
            {"error", job.error},
            {"failedIndex", job.status == "failed" ? Json(job.completed) : Json(nullptr)},
            {"focus", handle_json(job.focus)},
            {"hit", handle_json(job.hit)}};
}
} // namespace
SDL_Keycode input_key(std::string_view text)
{
    // Explicit names ensure every accepted key is mapped by the existing RmlUi bridge.
    if (text.size() == 1)
    {
        const auto c = text.front();
        if (c >= 'a' && c <= 'z')
        {
            return SDLK_A + (c - 'a');
        }
        if (c >= 'A' && c <= 'Z')
        {
            return SDLK_A + (c - 'A');
        }
        if (c >= '0' && c <= '9')
        {
            return SDLK_0 + (c - '0');
        }
    }
    constexpr std::array names{std::pair{"Space", SDLK_SPACE},
                               std::pair{"Enter", SDLK_RETURN},
                               std::pair{"NumpadEnter", SDLK_KP_ENTER},
                               std::pair{"Escape", SDLK_ESCAPE},
                               std::pair{"Tab", SDLK_TAB},
                               std::pair{"Backspace", SDLK_BACKSPACE},
                               std::pair{"Delete", SDLK_DELETE},
                               std::pair{"Left", SDLK_LEFT},
                               std::pair{"Right", SDLK_RIGHT},
                               std::pair{"Up", SDLK_UP},
                               std::pair{"Down", SDLK_DOWN},
                               std::pair{"Home", SDLK_HOME},
                               std::pair{"End", SDLK_END},
                               std::pair{"PageUp", SDLK_PAGEUP},
                               std::pair{"PageDown", SDLK_PAGEDOWN},
                               std::pair{"Insert", SDLK_INSERT},
                               std::pair{"Shift", SDLK_LSHIFT},
                               std::pair{"Control", SDLK_LCTRL},
                               std::pair{"Alt", SDLK_LALT},
                               std::pair{"Meta", SDLK_LGUI},
                               std::pair{"RightShift", SDLK_RSHIFT},
                               std::pair{"RightControl", SDLK_RCTRL},
                               std::pair{"RightAlt", SDLK_RALT},
                               std::pair{"RightMeta", SDLK_RGUI},
                               std::pair{"F1", SDLK_F1},
                               std::pair{"F2", SDLK_F2},
                               std::pair{"F3", SDLK_F3},
                               std::pair{"F4", SDLK_F4},
                               std::pair{"F5", SDLK_F5},
                               std::pair{"F6", SDLK_F6},
                               std::pair{"F7", SDLK_F7},
                               std::pair{"F8", SDLK_F8},
                               std::pair{"F9", SDLK_F9},
                               std::pair{"F10", SDLK_F10},
                               std::pair{"F11", SDLK_F11},
                               std::pair{"F12", SDLK_F12}};
    for (const auto& [name, code] : names)
    {
        if (name == text)
        {
            return code;
        }
    }
    return SDLK_UNKNOWN;
}
bool validate_input(const Json& events, std::uint64_t document, std::string& error)
{
    if (!events.is_array() || events.empty() || events.size() > 256)
    {
        error = "events must contain 1..256 events";
        return false;
    }
    std::set<SDL_Keycode> keys;
    std::set<std::uint8_t> buttons;
    float wait_total = 0;
    for (const auto& event : events)
    {
        if (!event.is_object() || !event.contains("type") || !event["type"].is_string())
        {
            error = "Every input event requires type";
            return false;
        }
        const auto type = event["type"].get<std::string>();
        bool accepted = false;
        if (type == "pointer_move")
        {
            accepted = fields(event, {"type", "target", "x", "y"}, error);
        }
        else if (type == "pointer_down" || type == "pointer_up")
        {
            accepted = fields(event, {"type", "target", "x", "y", "button", "clickCount"}, error);
            const auto which = event.contains("button") && event["button"].is_string()
                                   ? button(event["button"].get<std::string>())
                                   : 0;
            if (!which ||
                (type == "pointer_down" ? !buttons.insert(which).second : !buttons.erase(which)))
            {
                error = "Pointer buttons must have paired down/up events";
                return false;
            }
            if (event.contains("clickCount") && (!event["clickCount"].is_number_integer() ||
                                                 event["clickCount"].get<std::int64_t>() < 1 ||
                                                 event["clickCount"].get<std::int64_t>() > 2))
            {
                error = "clickCount must be 1 or 2";
                return false;
            }
        }
        else if (type == "wheel")
        {
            accepted = fields(event, {"type", "target", "x", "y", "deltaX", "deltaY"}, error);
            float x = 0, y = 0;
            if (!event.contains("deltaX") || !number(event["deltaX"], x) || std::abs(x) > 1000 ||
                !event.contains("deltaY") || !number(event["deltaY"], y) || std::abs(y) > 1000)
            {
                error = "Wheel deltas must be finite and at most 1000";
                return false;
            }
        }
        else if (type == "key_down" || type == "key_up")
        {
            accepted = fields(event, {"type", "key", "repeat", "target"}, error);
            const auto key = event.contains("key") && event["key"].is_string()
                                 ? input_key(event["key"].get<std::string>())
                                 : SDLK_UNKNOWN;
            if (!key || (event.contains("repeat") && !event["repeat"].is_boolean()))
            {
                error = "Unknown input key or invalid repeat";
                return false;
            }
            const bool repeat = event.contains("repeat") && event["repeat"].get<bool>();
            if ((type == "key_up" && (repeat || !keys.erase(key))) ||
                (type == "key_down" && (repeat ? !keys.contains(key) : !keys.insert(key).second)))
            {
                error = "Keys require paired down/up; repeat requires a held key";
                return false;
            }
        }
        else if (type == "focus")
        {
            accepted = fields(event, {"type", "target"}, error) && event.contains("target");
        }
        else if (type == "text")
        {
            std::string value;
            accepted = fields(event, {"type", "text", "target"}, error) && event.contains("text") &&
                       bounded_text(event["text"], value, 4096) && !value.empty();
        }
        else if (type == "wait")
        {
            float ms = 0;
            accepted = fields(event, {"type", "milliseconds"}, error) &&
                       event.contains("milliseconds") && number(event["milliseconds"], ms) &&
                       ms >= 0 && ms <= 10000;
            wait_total += ms;
            if (wait_total > 10000)
            {
                error = "Input waits exceed 10 seconds";
                return false;
            }
        }
        if (!accepted)
        {
            if (error.empty())
            {
                error = "Invalid or unsupported input event";
            }
            return false;
        }
        if (event.contains("target"))
        {
            UiElementHandle target;
            if (!handle(event["target"], target) || target.document_id != document)
            {
                error = "Target must belong to this input document";
                return false;
            }
        }
        if (type.starts_with("pointer_") || type == "wheel")
        {
            float x = 0, y = 0;
            if (event.contains("x") != event.contains("y") ||
                (event.contains("x") && (!number(event["x"], x) || !number(event["y"], y))) ||
                (!event.contains("x") && !event.contains("target")))
            {
                error = "Pointer event needs a target or both finite logical x/y coordinates";
                return false;
            }
        }
    }
    if (!keys.empty() || !buttons.empty())
    {
        error = "Sequence must release every pressed key and pointer button";
        return false;
    }
    return true;
}
bool UiEditorCommands::State::input(std::string_view name, const Json& args, Json& result,
                                    std::string& error)
{
    if (name == "get_ui_input" || name == "cancel_ui_input")
    {
        std::string id;
        if (!fields(args, {"job"}, error) || !args.contains("job") ||
            !bounded_text(args["job"], id, 64))
        {
            if (error.empty())
            {
                error = "job is required";
            }
            return false;
        }
        const auto found = inputs.find(id);
        if (found == inputs.end())
        {
            error = "Unknown UI input job";
            return false;
        }
        if (name == "cancel_ui_input" && active == id)
        {
            cancel_input("Canceled by caller");
        }
        result = receipt(found->second);
        return true;
    }
    Json request = args;
    if (name == "click_ui")
    {
        UiElementHandle target;
        if (!fields(args, {"target", "x", "y", "button", "clickCount"}, error) ||
            !args.contains("target") || !handle(args["target"], target))
        {
            if (error.empty())
            {
                error = "click_ui requires a complete target handle";
            }
            return false;
        }
        Json point{{"target", args["target"]}};
        if (args.contains("x"))
        {
            point["x"] = args["x"];
        }
        if (args.contains("y"))
        {
            point["y"] = args["y"];
        }
        Json move = point;
        move["type"] = "pointer_move";
        Json down = point;
        down["type"] = "pointer_down";
        down["button"] = args.contains("button") ? args["button"] : Json("left");
        if (args.contains("clickCount"))
        {
            down["clickCount"] = args["clickCount"];
        }
        Json up = down;
        up["type"] = "pointer_up";
        request = {{"documentId", std::to_string(target.document_id)},
                   {"events", Json::array({move, down, up})}};
        if (args.contains("clickCount") && args["clickCount"].is_number_integer() &&
            args["clickCount"] == 2)
        {
            // RmlUi detects double clicks from real consecutive presses, not SDL's clicks field.
            request["events"][1]["clickCount"] = 1;
            request["events"][2]["clickCount"] = 1;
            request["events"].push_back(down);
            request["events"].push_back(up);
        }
    }
    std::uint64_t doc = 0, node = 0;
    UiElementHandle root;
    if (!fields(request, {"documentId", "events"}, error) || !request.contains("documentId") ||
        !identity(request["documentId"], doc) || !request.contains("events") ||
        !validate_input(request["events"], doc, error))
    {
        if (error.empty())
        {
            error = "Invalid UI input request";
        }
        return false;
    }
    if (!active.empty())
    {
        error = "Another UI input sequence is running";
        return false;
    }
    if (!document(doc, node, root) || !ui.document_visible(node))
    {
        error = "Input document is stale, unavailable or hidden";
        return false;
    }
    if (ui.pointer_active() || native_pressed())
    {
        error = "Native input is already pressed; input sequence was not started";
        return false;
    }
    InputJob job;
    if (!window.size(job.size) || !window.native_window())
    {
        error = "Input window is unavailable";
        return false;
    }
    job.id = std::to_string(++sequence);
    job.document = doc;
    job.node = node;
    job.events = request["events"];
    job.start = Clock::now();
    (void)focused(doc, job.focus);
    while (inputs.size() >= 32)
    {
        inputs.erase(inputs.begin());
    }
    active = job.id;
    result = {{"job", job.id}, {"accepted", true}, {"status", "running"}};
    inputs.emplace(job.id, std::move(job));
    return true;
}
void UiEditorCommands::State::cancel_input(std::string_view reason) noexcept
{
    if (active.empty())
    {
        return;
    }
    auto& job = inputs.at(active);
    ui.cancel_input();
    job.buttons = 0;
    job.keys.clear();
    job.modifiers = 0;
    job.status = "canceled";
    job.error = reason;
    job.focus = {};
    job.hit = {};
    active.clear();
}
void UiEditorCommands::State::tick_input()
{
    if (active.empty())
    {
        return;
    }
    auto& job = inputs.at(active);
    WindowSize size;
    if (Clock::now() - job.start > std::chrono::seconds(10))
    {
        cancel_input("UI input exceeded its 10 second deadline");
        return;
    }
    if (!window.size(size) || size.logical_width != job.size.logical_width ||
        size.logical_height != job.size.logical_height ||
        size.pixel_width != job.size.pixel_width || size.pixel_height != job.size.pixel_height ||
        ui.document_id(job.node) != job.document || !ui.document_visible(job.node))
    {
        cancel_input("UI document or window identity changed");
        return;
    }
    for (std::size_t count = 0; count < 32 && job.completed < job.events.size(); ++count)
    {
        const auto& event = job.events[job.completed];
        if (event["type"] == "wait")
        {
            if (!job.waiting)
            {
                job.resume = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                                std::chrono::duration<double, std::milli>(
                                                    event["milliseconds"].get<double>()));
                job.waiting = true;
                return;
            }
            if (Clock::now() < job.resume)
            {
                return;
            }
            job.waiting = false;
            ++job.completed;
            continue;
        }
        std::string error;
        if (!execute_event(job, event, error))
        {
            ui.cancel_input();
            job.keys.clear();
            job.buttons = 0;
            job.modifiers = 0;
            job.status = "failed";
            job.error = error;
            active.clear();
            return;
        }
        ++job.completed;
    }
    (void)focused(job.document, job.focus);
    if (job.completed == job.events.size())
    {
        job.status = "complete";
        active.clear();
    }
}
bool UiEditorCommands::State::execute_event(InputJob& job, const Json& step, std::string& error)
{
    const auto type = step["type"].get<std::string>();
    UiElementHandle target;
    UiElementInfo info;
    if (step.contains("target"))
    {
        if (!handle(step["target"], target) || !ui.inspect_element(target, info) || !info.visible ||
            !info.enabled)
        {
            error = "Input target is stale, hidden or disabled";
            return false;
        }
    }
    if (type == "focus")
    {
        if (!ui.focus_element(target))
        {
            error = "UI focus request was rejected";
            return false;
        }
        job.focus = target;
        return true;
    }
    SDL_Event event{};
    const auto window_id = SDL_GetWindowID(window.native_window());
    if (type.starts_with("pointer_") || type == "wheel")
    {
        if (step.contains("x"))
        {
            job.x = step["x"].get<float>();
            job.y = step["y"].get<float>();
        }
        else
        {
            job.x = info.bounds[0] + info.bounds[2] * .5F;
            job.y = info.bounds[1] + info.bounds[3] * .5F;
        }
        if (!std::isfinite(job.x) || !std::isfinite(job.y) || job.x < 0 || job.y < 0 ||
            job.x >= float(job.size.logical_width) || job.y >= float(job.size.logical_height) ||
            !ui.hit_test(job.x, job.y, job.hit))
        {
            error = "Pointer coordinates are outside the logical window";
            return false;
        }
        const bool releasing_capture = type == "pointer_up" && job.buttons != 0;
        const bool moving_capture = type == "pointer_move" && job.buttons != 0;
        const bool leaving_ui =
            type == "pointer_move" && !target.document_id && !job.hit.document_id;
        if (!releasing_capture && !moving_capture && !leaving_ui)
        {
            UiElementInfo hit_info;
            if (job.hit.document_id != job.document ||
                (target.document_id && !descendant(ui, job.hit, target)) ||
                !ui.inspect_element(job.hit, hit_info) || !hit_info.visible || !hit_info.enabled)
            {
                error =
                    "Input target is occluded, clipped, disabled or not hit at these coordinates";
                return false;
            }
        }
        if (type == "pointer_move" || type == "wheel")
        {
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.motion.windowID = window_id;
            event.motion.x = job.x;
            event.motion.y = job.y;
            event.motion.state = job.buttons;
            (void)ui.process_event(event, job.modifiers);
            if (type == "pointer_move")
            {
                return true;
            }
            event = {};
            event.type = SDL_EVENT_MOUSE_WHEEL;
            event.wheel.windowID = window_id;
            event.wheel.x = step["deltaX"].get<float>();
            event.wheel.y = step["deltaY"].get<float>();
            event.wheel.mouse_x = job.x;
            event.wheel.mouse_y = job.y;
        }
        else
        {
            const auto which = button(step["button"].get<std::string>());
            event.type =
                type == "pointer_down" ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button.windowID = window_id;
            event.button.button = which;
            event.button.down = type == "pointer_down";
            event.button.clicks =
                step.contains("clickCount") ? step["clickCount"].get<std::uint8_t>() : 1;
            event.button.x = job.x;
            event.button.y = job.y;
            if (event.button.down)
            {
                job.buttons |= SDL_BUTTON_MASK(which);
            }
            else
            {
                job.buttons &= ~SDL_BUTTON_MASK(which);
            }
        }
    }
    else
    {
        UiElementHandle focus;
        if (!focused(job.document, focus) || (target.document_id && !same(target, focus)))
        {
            error = "Keyboard input requires the actual expected focused element";
            return false;
        }
        if (type == "text")
        {
            event.type = SDL_EVENT_TEXT_INPUT;
            event.text.windowID = window_id;
            event.text.text = step["text"].get_ref<const std::string&>().c_str();
        }
        else
        {
            const auto key = input_key(step["key"].get<std::string>());
            const bool down = type == "key_down";
            if (down)
            {
                job.keys.insert(key);
                job.modifiers |= modifier(key);
            }
            else
            {
                job.keys.erase(key);
                job.modifiers &= ~modifier(key);
            }
            event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            event.key.windowID = window_id;
            event.key.key = key;
            event.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
            event.key.mod = job.modifiers;
            event.key.down = down;
            event.key.repeat = step.contains("repeat") && step["repeat"].get<bool>();
        }
    }
    // False means not consumed by RmlUi; it is not a delivery failure. Never forward to gameplay.
    (void)ui.process_event(event, job.modifiers);
    return true;
}
} // namespace engine::plugins::ui
