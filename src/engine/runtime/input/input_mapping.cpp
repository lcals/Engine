#include "runtime/input/input_state.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::input_detail
{
namespace
{
constexpr double epsilon = 0.000001;

bool pointer_kind(Kind kind)
{
    return (kind >= Kind::mouse_button && kind <= Kind::mouse_wheel) || kind == Kind::touch;
}

template <class T> bool valid_enum(T value)
{
    return value >= T::MIN && value <= T::MAX;
}

std::vector<std::uint32_t> channel_order(const State& state)
{
    std::vector<std::uint32_t> order;
    for (std::uint32_t index = 0; index < state.channels.size(); ++index)
    {
        if (state.channels[index].alive && state.channels[index].data.enabled)
        {
            order.push_back(index);
        }
    }
    std::stable_sort(order.begin(), order.end(),
                     [&state](auto a, auto b)
                     {
                         const auto& left = state.channels[a];
                         const auto& right = state.channels[b];
                         return left.data.priority != right.data.priority
                                    ? left.data.priority > right.data.priority
                                    : left.order < right.order;
                     });
    return order;
}

double physical(const State& state, Channel& channel, const schema::InputControlRef& ref,
                const std::set<Key>& consumed, bool& unavailable)
{
    const auto source_key = key(ref);
    const auto found = state.controls.find(source_key);
    if (found == state.controls.end())
    {
        unavailable = true;
        return 0;
    }
    auto& control = found->second;
    const auto neutral =
        channel.neutral.contains(source_key) ? channel.neutral.at(source_key) : epsilon;
    if (std::abs(control.value) <= neutral)
    {
        channel.blocked.erase(source_key);
        if (ref.kind() == Kind::gamepad_axis || ref.kind() == Kind::joystick_axis)
        {
            channel.neutralized.insert(source_key);
        }
    }
    const auto kind = ref.kind();
    const bool routed = kind == Kind::key ? state.routing.keyboard_claimed
                                          : pointer_kind(kind) && state.routing.pointer_claimed;
    if ((control.suppressed && !channel.neutralized.contains(source_key)) || routed ||
        channel.blocked.contains(source_key) || consumed.contains(source_key))
    {
        if (consumed.contains(source_key) && std::abs(control.value) > epsilon)
        {
            channel.blocked.insert(source_key);
        }
        unavailable = true;
        return 0;
    }
    if (kind == Kind::touch)
    {
        return std::any_of(state.contacts.begin(), state.contacts.end(),
                           [&ref](const auto& entry)
                           {
                               return entry.first.first == ref.connection_id() &&
                                      entry.second.data.held && !entry.second.suppressed;
                           })
                   ? 1
                   : 0;
    }
    return control.info.value_mode == schema::InputValueMode::delta ? control.sample
                                                                    : control.value;
}

void limit(double& x, double& y, bool vector)
{
    const auto magnitude = vector ? std::hypot(x, y) : std::abs(x);
    if (magnitude > 1)
    {
        x /= magnitude;
        y /= magnitude;
    }
}

const schema::InputVirtualControlDataT* virtual_control(const Channel& channel, std::string_view id)
{
    for (const auto& control : channel.data.virtual_controls)
    {
        if (control->id == id)
        {
            return control.get();
        }
    }
    return nullptr;
}

void region(const State& state, SDL_Window* window, const schema::InputRegionDataT& data, double& x,
            double& y, double& width, double& height)
{
    int window_width = 0;
    int window_height = 0;
    SDL_GetWindowSize(window, &window_width, &window_height);
    const auto& routing = state.routing;
    const double viewport_width =
        routing.viewport_width > 0 ? routing.viewport_width : window_width;
    const double viewport_height =
        routing.viewport_height > 0 ? routing.viewport_height : window_height;
    const double safe_width =
        std::max(0.0, viewport_width - routing.safe_left - routing.safe_right);
    const double safe_height =
        std::max(0.0, viewport_height - routing.safe_top - routing.safe_bottom);
    width = data.width;
    height = data.height;
    x = routing.viewport_x + routing.safe_left + data.anchor_x * safe_width + data.x;
    y = routing.viewport_y + routing.safe_top + data.anchor_y * safe_height + data.y;
}

bool inside(const schema::InputRegionDataT& data, double x, double y, double rx, double ry,
            double width, double height)
{
    if (data.shape == schema::InputRegionShape::circle)
    {
        const auto radius = std::min(width, height) * 0.5;
        return std::hypot(x - rx - width * 0.5, y - ry - height * 0.5) <= radius;
    }
    return x >= rx && x <= rx + width && y >= ry && y <= ry + height;
}

void release_pointer(State& state, std::map<ContactKey, PointerCapture>::iterator found)
{
    auto& channel = state.channels[found->second.channel];
    const auto control = channel.virtuals.find(found->second.control);
    if (control != channel.virtuals.end())
    {
        // A drag's already accumulated delta remains valid until publication on normal release.
        control->second.held = false;
        control->second.x = control->second.y = 0;
    }
    state.pointers.erase(found);
}
} // namespace

schema::InputValueMode mode(Kind kind)
{
    return kind == Kind::mouse_delta || kind == Kind::mouse_relative || kind == Kind::mouse_wheel
               ? schema::InputValueMode::delta
               : schema::InputValueMode::state;
}

schema::InputUnit unit(Kind kind)
{
    if (kind == Kind::mouse_delta || kind == Kind::mouse_position)
    {
        return schema::InputUnit::logical;
    }
    if (kind == Kind::mouse_relative)
    {
        return schema::InputUnit::relative;
    }
    if (kind == Kind::mouse_wheel)
    {
        return schema::InputUnit::wheel;
    }
    return schema::InputUnit::normalized;
}

bool validate_source(const schema::InputControlRef& ref)
{
    if (ref.connection_id() == 0 || !valid_enum(ref.kind()))
    {
        return false;
    }
    switch (ref.kind())
    {
    case Kind::key:
        return ref.code() > SDL_SCANCODE_UNKNOWN && ref.code() < SDL_SCANCODE_COUNT;
    case Kind::mouse_button:
        return ref.code() > 0 && ref.code() <= 255;
    case Kind::mouse_position:
    case Kind::mouse_delta:
    case Kind::mouse_relative:
    case Kind::mouse_wheel:
        return ref.code() <= 1;
    case Kind::gamepad_button:
        return ref.code() < SDL_GAMEPAD_BUTTON_COUNT;
    case Kind::gamepad_axis:
        return ref.code() < SDL_GAMEPAD_AXIS_COUNT;
    case Kind::touch:
        return ref.code() == 0;
    default:
        return true; // Raw joystick counts and touchpad indices are device capabilities.
    }
}

bool validate(const schema::InputChannelDataT& data, std::string& error)
{
    const auto fail = [&error](const char* message)
    {
        error = message;
        return false;
    };
    if (data.actions.size() > 4096 || data.virtual_controls.size() > 4096)
    {
        return fail("Input configuration list exceeds 4096 entries");
    }
    std::size_t total_parts = 0;
    if (!valid_enum(data.activation))
    {
        return fail("Invalid input activation");
    }
    std::map<std::string, const schema::InputVirtualControlDataT*, std::less<>> virtuals;
    for (const auto& control : data.virtual_controls)
    {
        if (!control || control->id.empty() || control->id.size() > 4096 ||
            !virtuals.emplace(control->id, control.get()).second || !valid_enum(control->kind) ||
            !control->region)
        {
            return fail("Invalid or duplicate virtual input control");
        }
        const auto& r = *control->region;
        const double values[] = {r.x, r.y, r.width, r.height, r.anchor_x, r.anchor_y, r.deadzone};
        if (r.connection_id == 0 || !valid_enum(r.shape) ||
            !std::all_of(std::begin(values), std::end(values),
                         [](auto v) { return std::isfinite(v); }) ||
            r.width <= 0 || r.height <= 0 || r.deadzone < 0 || r.deadzone >= 1)
        {
            return fail("Invalid virtual input region");
        }
    }
    std::set<std::string> names;
    for (const auto& action : data.actions)
    {
        if (!action || action->name.empty() || action->name.size() > 4096 ||
            action->bindings.size() > 4096 || !names.insert(action->name).second ||
            !valid_enum(action->kind) || !valid_enum(action->value_mode) ||
            !valid_enum(action->combine) ||
            (action->kind == schema::InputActionKind::button &&
             action->value_mode != schema::InputValueMode::state))
        {
            return fail("Invalid or duplicate input action");
        }
        auto action_unit = schema::InputUnit::none;
        for (const auto& binding : action->bindings)
        {
            if (!binding || binding->parts.empty() || binding->parts.size() > 4096 ||
                binding->modifiers.size() > 4096 || !std::isfinite(binding->scale) ||
                !std::isfinite(binding->deadzone) || binding->deadzone < 0 ||
                binding->deadzone >= 1 || !std::isfinite(binding->press_threshold) ||
                !std::isfinite(binding->release_threshold) || binding->press_threshold <= 0 ||
                binding->release_threshold < 0 ||
                binding->release_threshold >= binding->press_threshold)
            {
                return fail("Invalid input binding processing parameters");
            }
            total_parts += binding->parts.size() + binding->modifiers.size();
            if (total_parts > 65536)
            {
                return fail("Input configuration exceeds 65536 parts and modifiers");
            }
            for (const auto& modifier : binding->modifiers)
            {
                if (!validate_source(modifier) ||
                    mode(modifier.kind()) != schema::InputValueMode::state)
                {
                    return fail("Invalid input binding modifier");
                }
            }
            for (const auto& part : binding->parts)
            {
                if (!part || part->virtual_control.size() > 4096 ||
                    !std::isfinite(part->weight_x) || !std::isfinite(part->weight_y))
                {
                    return fail("Invalid input binding part");
                }
                auto part_mode = schema::InputValueMode::state;
                auto part_unit = schema::InputUnit::normalized;
                if (!part->virtual_control.empty())
                {
                    const auto found = virtuals.find(part->virtual_control);
                    if (part->source.connection_id() != 0 || found == virtuals.end() ||
                        part->virtual_axis > 2)
                    {
                        return fail("Virtual input part must name exactly one valid source");
                    }
                    if ((found->second->kind == schema::InputVirtualKind::button &&
                         part->virtual_axis != 0) ||
                        (found->second->kind != schema::InputVirtualKind::button &&
                         part->virtual_axis == 0))
                    {
                        return fail("Virtual input axis does not match its control kind");
                    }
                    if (found->second->kind == schema::InputVirtualKind::drag)
                    {
                        part_mode = schema::InputValueMode::delta;
                        part_unit = schema::InputUnit::logical;
                    }
                }
                else
                {
                    if (!validate_source(part->source))
                    {
                        return fail("Invalid physical input source");
                    }
                    part_mode = mode(part->source.kind());
                    part_unit = unit(part->source.kind());
                }
                if (part_mode != action->value_mode)
                {
                    return fail("Cannot mix state and delta input");
                }
                if (part_mode == schema::InputValueMode::delta)
                {
                    if (action_unit != schema::InputUnit::none && action_unit != part_unit)
                    {
                        return fail("Cannot combine different delta input units");
                    }
                    action_unit = part_unit;
                }
            }
        }
    }
    return true;
}

void evaluate(State& state, bool baseline, bool canceled)
{
    std::set<Key> consumed;
    std::set<std::uint32_t> evaluated;
    for (auto index : channel_order(state))
    {
        auto& channel = state.channels[index];
        evaluated.insert(index);
        std::set<Key> channel_used;
        for (const auto& definition : channel.data.actions)
        {
            auto& runtime = channel.actions[definition->name];
            const bool delta = definition->value_mode == schema::InputValueMode::delta;
            if (delta && !baseline && runtime.delta_serial == state.sample_serial && !canceled)
            {
                continue;
            }
            runtime.latches.resize(definition->bindings.size());
            runtime.contributors.resize(definition->bindings.size());
            runtime.sources.resize(definition->bindings.size());
            double x = 0;
            double y = 0;
            bool held = false;
            bool unavailable = false;
            for (std::size_t b = 0; b < definition->bindings.size(); ++b)
            {
                const auto& binding = *definition->bindings[b];
                double bx = 0;
                double by = 0;
                bool modifiers = true;
                bool binding_unavailable = false;
                std::set<Key> used;
                std::set<Key> active;
                std::set<std::string> active_virtuals;
                const auto read = [&](const schema::InputControlRef& ref)
                {
                    bool denied = false;
                    const auto value = physical(state, channel, ref, consumed, denied);
                    const auto source_key = key(ref);
                    binding_unavailable |= denied && runtime.sources[b].contains(source_key);
                    if (std::abs(value) > epsilon)
                    {
                        active.insert(source_key);
                    }
                    return value;
                };
                for (const auto& modifier : binding.modifiers)
                {
                    if (std::abs(read(modifier)) <= epsilon)
                    {
                        modifiers = false;
                    }
                    used.insert(key(modifier));
                }
                for (const auto& part : binding.parts)
                {
                    double value = 0;
                    if (part->virtual_control.empty())
                    {
                        value = read(part->source);
                        used.insert(key(part->source));
                    }
                    else
                    {
                        const auto& v = channel.virtuals[part->virtual_control];
                        const auto* control = virtual_control(channel, part->virtual_control);
                        if (control && control->enabled && control->visible)
                        {
                            if (part->virtual_axis == 0)
                            {
                                value = v.held ? 1 : 0;
                            }
                            else if (control->kind == schema::InputVirtualKind::drag)
                            {
                                value = part->virtual_axis == 1 ? v.sample_x : v.sample_y;
                            }
                            else
                            {
                                value = part->virtual_axis == 1 ? v.x : v.y;
                            }
                        }
                    }
                    if (!part->virtual_control.empty() && std::abs(value) > epsilon)
                    {
                        active_virtuals.insert(part->virtual_control);
                        const auto* control = virtual_control(channel, part->virtual_control);
                        used.insert({control->region->connection_id, Kind::touch, 0});
                    }
                    bx += value * part->weight_x;
                    by += value * part->weight_y;
                }
                unavailable |= binding_unavailable;
                if (!modifiers)
                {
                    bx = by = 0;
                }
                if (definition->kind != schema::InputActionKind::axis2d)
                {
                    by = 0;
                }
                double magnitude = std::hypot(bx, by);
                if (definition->value_mode == schema::InputValueMode::state)
                {
                    if (magnitude <= binding.deadzone)
                    {
                        bx = by = 0;
                    }
                    else if (binding.deadzone > 0)
                    {
                        const auto scaled = (std::min(1.0, magnitude) - binding.deadzone) /
                                            (1 - binding.deadzone) / magnitude;
                        bx *= scaled;
                        by *= scaled;
                    }
                    if (binding.normalize)
                    {
                        limit(bx, by, definition->kind == schema::InputActionKind::axis2d);
                    }
                }
                bx *= binding.scale;
                by *= binding.scale;
                magnitude = std::hypot(bx, by);
                if (definition->kind == schema::InputActionKind::button)
                {
                    const bool latch = runtime.latches[b];
                    runtime.latches[b] = latch ? magnitude > binding.release_threshold
                                               : magnitude >= binding.press_threshold;
                    held |= runtime.latches[b];
                    bx = runtime.latches[b] ? 1 : 0;
                }
                runtime.contributors[b] = std::abs(bx) > epsilon || std::abs(by) > epsilon;
                if (runtime.contributors[b])
                {
                    channel_used.insert(used.begin(), used.end());
                    if (delta)
                    {
                        runtime.delta_sources.insert(active.begin(), active.end());
                        runtime.delta_virtuals.insert(active_virtuals.begin(),
                                                      active_virtuals.end());
                    }
                }
                else
                {
                    active.clear();
                }
                runtime.sources[b] = std::move(active);
                if (definition->combine == schema::InputCombine::sum_clamped)
                {
                    x += bx;
                    y += by;
                }
                else if (std::hypot(bx, by) > std::hypot(x, y))
                {
                    x = bx;
                    y = by;
                }
            }
            if (definition->kind == schema::InputActionKind::button)
            {
                x = held ? 1 : 0;
                y = 0;
            }
            else
            {
                if (definition->combine == schema::InputCombine::sum_clamped &&
                    definition->value_mode == schema::InputValueMode::state)
                {
                    limit(x, y, definition->kind == schema::InputActionKind::axis2d);
                }
                held = std::hypot(x, y) > epsilon;
            }
            auto& old = runtime.state;
            if (delta)
            {
                if (baseline)
                {
                    x = y = 0;
                    held = false;
                    runtime.delta_sources.clear();
                    runtime.delta_virtuals.clear();
                }
                else
                {
                    bool invalidated = false;
                    if (canceled)
                    {
                        for (const auto& control_key : runtime.delta_sources)
                        {
                            bool denied = false;
                            (void)physical(state, channel, source(control_key), consumed, denied);
                            invalidated |= denied;
                        }
                        for (const auto& id : runtime.delta_virtuals)
                        {
                            invalidated |=
                                !channel.virtuals.contains(id) || !channel.virtuals[id].held;
                        }
                    }
                    if (invalidated)
                    {
                        x = y = 0;
                        held = false;
                    }
                    else
                    {
                        if (runtime.delta_serial == state.sample_serial)
                        {
                            x = y = 0;
                        }
                        x += old.value_x();
                        y += old.value_y();
                        held = std::hypot(x, y) > epsilon;
                    }
                }
                runtime.delta_serial = state.sample_serial;
            }
            const bool lost = old.held() && !held;
            const bool canceled_edge = lost && (canceled || unavailable);
            old = {state.frame_id,
                   x,
                   y,
                   held,
                   old.pressed() || (!baseline && !old.held() && held),
                   old.released() || (!baseline && lost && !canceled_edge),
                   old.canceled() || (!baseline && canceled_edge)};
        }
        if (channel.data.consume_inputs)
        {
            consumed.insert(channel_used.begin(), channel_used.end());
        }
    }
    for (std::uint32_t index = 0; index < state.channels.size(); ++index)
    {
        auto& channel = state.channels[index];
        if (!channel.alive || evaluated.contains(index))
        {
            continue;
        }
        for (auto& [name, action] : channel.actions)
        {
            (void)name;
            const auto old = action.state;
            action.state = {state.frame_id,
                            0,
                            0,
                            false,
                            old.pressed(),
                            old.released(),
                            old.canceled() || (!baseline && old.held())};
            std::fill(action.latches.begin(), action.latches.end(), false);
            std::fill(action.contributors.begin(), action.contributors.end(), false);
        }
    }
}

void cancel_pointers(State& state, std::uint32_t channel)
{
    bool any = false;
    for (auto it = state.pointers.begin(); it != state.pointers.end();)
    {
        if (it->second.channel == channel)
        {
            state.contacts[it->first].suppressed = true;
            auto current = it++;
            release_pointer(state, current);
            any = true;
        }
        else
        {
            ++it;
        }
    }
    if (channel < state.channels.size())
    {
        for (auto& [id, value] : state.channels[channel].virtuals)
        {
            (void)id;
            value = {};
        }
    }
    if (any)
    {
        ++state.cancel_revision;
    }
}

void cancel(State& state, bool keyboard, bool pointer, bool all)
{
    ++state.cancel_revision;
    for (auto& [control_key, control] : state.controls)
    {
        const auto kind = std::get<1>(control_key);
        if (all || (keyboard && kind == Kind::key) || (pointer && pointer_kind(kind)))
        {
            for (auto& channel : state.channels)
            {
                channel.neutralized.erase(control_key);
            }
            if (std::abs(control.value) > epsilon)
            {
                control.canceled = true;
                control.suppressed = true;
            }
            if (control.info.value_mode == schema::InputValueMode::delta)
            {
                control.value = 0;
            }
        }
    }
    if (pointer || all)
    {
        for (std::uint32_t i = 0; i < state.channels.size(); ++i)
        {
            cancel_pointers(state, i);
        }
        for (auto& [id, contact] : state.contacts)
        {
            (void)id;
            contact.suppressed = true;
        }
    }
    evaluate(state, false, true);
}

void touch(State& state, SDL_Window* window, const SDL_TouchFingerEvent& event, bool claimed)
{
    auto device =
        std::find_if(state.devices.begin(), state.devices.end(), [&event](const Device& d)
                     { return d.data.kind == DeviceKind::touch && d.native_id == event.touchID; });
    if (device == state.devices.end())
    {
        return;
    }
    const ContactKey id{device->data.connection_id, event.fingerID};
    auto& contact = state.contacts[id];
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    auto& data = contact.data;
    data.source = {id.first, Kind::touch, 0};
    data.contact_id = id.second;
    data.normalized_x = event.x;
    data.normalized_y = event.y;
    data.logical_x = event.x * width;
    data.logical_y = event.y * height;
    data.pressure = event.pressure;
    const bool down = event.type == SDL_EVENT_FINGER_DOWN;
    const bool up = event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED;
    data.held = !up;
    if (down)
    {
        contact.suppressed = claimed || state.routing.pointer_claimed;
    }
    auto capture = state.pointers.find(id);
    if (down && capture == state.pointers.end() && !contact.suppressed)
    {
        for (const auto index : channel_order(state))
        {
            auto& channel = state.channels[index];
            std::vector<const schema::InputVirtualControlDataT*> candidates;
            for (const auto& control : channel.data.virtual_controls)
            {
                if (control->enabled && control->visible &&
                    control->region->connection_id == id.first)
                {
                    candidates.push_back(control.get());
                }
            }
            std::stable_sort(candidates.begin(), candidates.end(),
                             [](auto* a, auto* b) { return a->priority > b->priority; });
            for (const auto* control : candidates)
            {
                if (std::any_of(
                        state.pointers.begin(), state.pointers.end(),
                        [index, control](const auto& p)
                        { return p.second.channel == index && p.second.control == control->id; }))
                {
                    continue;
                }
                double x = 0, y = 0, w = 0, h = 0;
                region(state, window, *control->region, x, y, w, h);
                if (!inside(*control->region, data.logical_x, data.logical_y, x, y, w, h))
                {
                    continue;
                }
                PointerCapture owner;
                owner.channel = index;
                owner.control = control->id;
                owner.center_x = control->region->floating ? data.logical_x : x + w * 0.5;
                owner.center_y = control->region->floating ? data.logical_y : y + h * 0.5;
                owner.previous_x = data.logical_x;
                owner.previous_y = data.logical_y;
                state.pointers.emplace(id, std::move(owner));
                capture = state.pointers.find(id);
                break;
            }
            if (capture != state.pointers.end())
            {
                break;
            }
        }
    }
    bool cancellation = event.type == SDL_EVENT_FINGER_CANCELED;
    if (capture != state.pointers.end())
    {
        auto& channel = state.channels[capture->second.channel];
        const auto* control = virtual_control(channel, capture->second.control);
        auto& value = channel.virtuals[capture->second.control];
        double x = 0, y = 0, w = 0, h = 0;
        region(state, window, *control->region, x, y, w, h);
        cancellation |= control->region->cancel_on_exit &&
                        !inside(*control->region, data.logical_x, data.logical_y, x, y, w, h);
        if (cancellation)
        {
            value = {};
            contact.suppressed = true;
            release_pointer(state, capture);
            ++state.cancel_revision;
        }
        else if (up)
        {
            release_pointer(state, capture);
        }
        else
        {
            value.held = true;
            if (control->kind == schema::InputVirtualKind::stick)
            {
                value.x = (data.logical_x - capture->second.center_x) / (std::min(w, h) * 0.5);
                value.y = (data.logical_y - capture->second.center_y) / (std::min(w, h) * 0.5);
                limit(value.x, value.y, true);
                const auto length = std::hypot(value.x, value.y);
                const auto deadzone = control->region->deadzone;
                if (length <= deadzone)
                {
                    value.x = value.y = 0;
                }
                else if (deadzone > 0)
                {
                    const auto scale = (length - deadzone) / (1 - deadzone) / length;
                    value.x *= scale;
                    value.y *= scale;
                }
            }
            else if (control->kind == schema::InputVirtualKind::drag && !down)
            {
                value.dx += data.logical_x - capture->second.previous_x;
                value.dy += data.logical_y - capture->second.previous_y;
                value.sample_x += data.logical_x - capture->second.previous_x;
                value.sample_y += data.logical_y - capture->second.previous_y;
            }
            capture->second.previous_x = data.logical_x;
            capture->second.previous_y = data.logical_y;
        }
    }
    auto& raw = state.controls[{id.first, Kind::touch, 0}];
    const bool previous = raw.value > 0;
    const bool held =
        std::any_of(state.contacts.begin(), state.contacts.end(), [id](const auto& entry)
                    { return entry.first.first == id.first && entry.second.data.held; });
    raw.value = held ? 1 : 0;
    raw.pressed |= !previous && held;
    raw.released |= previous && !held && !cancellation;
    raw.canceled |= cancellation;
    raw.suppressed =
        false; // Eligibility is aggregated per contact in physical(), independently of raw facts.
    evaluate(state, false, cancellation);
    if (up)
    {
        state.contacts.erase(id);
    }
}

void capture_candidates(State& state)
{
    for (auto& capture : state.captures)
    {
        if (!capture.alive || capture.pending ||
            capture.state.status != schema::InputCaptureStatus::waiting)
        {
            continue;
        }
        for (const auto& [control_key, control] : state.controls)
        {
            if (std::abs(control.value) <= capture.options.neutral_threshold)
            {
                capture.blocked.erase(control_key);
            }
            const auto ref = source(control_key);
            const auto& options = capture.options;
            const bool modifier_key = ref.kind() == Kind::key && ref.code() >= SDL_SCANCODE_LCTRL &&
                                      ref.code() <= SDL_SCANCODE_RGUI;
            const bool modifier_released = options.include_modifiers && modifier_key &&
                                           capture.modifiers.contains(control_key) &&
                                           control.released;
            if (control.suppressed || control.repeat || capture.blocked.contains(control_key) ||
                (std::abs(control.value) < options.threshold && !modifier_released) ||
                (!options.devices.empty() &&
                 std::find(options.devices.begin(), options.devices.end(), ref.connection_id()) ==
                     options.devices.end()) ||
                (!options.kinds.empty() && std::find(options.kinds.begin(), options.kinds.end(),
                                                     ref.kind()) == options.kinds.end()))
            {
                continue;
            }
            if (ref.kind() == Kind::mouse_position && options.kinds.empty())
            {
                continue;
            }
            if (ref.kind() == Kind::touch &&
                !std::any_of(state.contacts.begin(), state.contacts.end(),
                             [&ref](const auto& entry)
                             {
                                 return entry.first.first == ref.connection_id() &&
                                        entry.second.data.held && !entry.second.suppressed;
                             }))
            {
                continue;
            }
            if (modifier_key && options.include_modifiers && !modifier_released)
            {
                capture.modifiers.insert(control_key);
                continue;
            }
            capture.state.candidates.clear();
            if (options.include_modifiers)
            {
                for (const auto& [modifier_key, modifier] : state.controls)
                {
                    const auto code = std::get<2>(modifier_key);
                    const auto device = std::find_if(
                        state.devices.begin(), state.devices.end(), [&modifier_key](const Device& d)
                        { return d.data.connection_id == std::get<0>(modifier_key); });
                    if (options.devices.empty() && ref.kind() != Kind::key &&
                        device != state.devices.end() && device->native_id != 0)
                    {
                        continue;
                    }
                    if (std::get<1>(modifier_key) == Kind::key && code >= SDL_SCANCODE_LCTRL &&
                        code <= SDL_SCANCODE_RGUI && modifier.value > 0 && !modifier.suppressed &&
                        modifier_key != control_key &&
                        (ref.kind() != Kind::key ||
                         std::get<0>(modifier_key) == ref.connection_id()) &&
                        (options.devices.empty() ||
                         std::find(options.devices.begin(), options.devices.end(),
                                   std::get<0>(modifier_key)) != options.devices.end()))
                    {
                        capture.state.candidates.push_back(source(modifier_key));
                    }
                }
            }
            capture.state.candidates.push_back(ref);
            capture.state.status = schema::InputCaptureStatus::completed;
            capture.state.frame_id = state.frame_id;
            break;
        }
    }
}
} // namespace engine::input_detail
