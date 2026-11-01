#include "runtime/input/input_system.hpp"

#include "runtime/input/input_state.hpp"
#include "runtime/window/window_system.hpp"

#include <algorithm>
#include <cmath>

namespace engine
{
using namespace input_detail;

InputSystem::InputSystem(WindowSystem& window) noexcept : window_(window)
{
}
InputSystem::~InputSystem() noexcept
{
    shutdown();
}

bool InputSystem::on_initialize(const EngineConfig&)
{
    last_error_.clear();
    if (!window_.is_initialized() || window_.native_window() == nullptr)
    {
        last_error_ = "Input requires an initialized window";
        return false;
    }
    state_ = std::make_unique<State>();
    state_->next_connection = next_connection_;
    state_->next_generation = next_generation_;
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK))
    {
        last_error_ = SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK);
        state_.reset();
        return false;
    }
    state_->sdl_initialized = true;
    if (!enumerate(*state_, last_error_))
    {
        on_shutdown();
        return false;
    }
    publish(*state_);
    frame_ = {0, state_->device_revision, 0};
    return true;
}

void InputSystem::on_shutdown() noexcept
{
    if (state_)
    {
        next_connection_ = state_->next_connection;
        next_generation_ = state_->next_generation;
        close_devices(*state_);
        if (state_->sdl_initialized)
        {
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK);
        }
        state_.reset();
    }
    frame_ = {};
}

bool InputSystem::begin_frame()
{
    last_error_.clear();
    if (!is_initialized() || !state_ || state_->frame_open)
    {
        last_error_ = "Input begin_frame requires an initialized, closed frame";
        return false;
    }
    auto& state = *state_;
    state.frame_open = true;
    ++state.frame_id;
    for (auto& [key_value, control] : state.controls)
    {
        (void)key_value;
        control.pressed = control.released = control.canceled = control.repeat = false;
        control.sample = 0;
        if (control.info.value_mode == schema::InputValueMode::delta)
        {
            control.value = 0;
            control.suppressed = false;
        }
    }
    for (std::uint32_t index = 0; index < state.channels.size(); ++index)
    {
        auto& channel = state.channels[index];
        if (!channel.alive)
        {
            continue;
        }
        for (auto& [name, action] : channel.actions)
        {
            (void)name;
            action.state.mutate_pressed(false);
            action.state.mutate_released(false);
            action.state.mutate_canceled(false);
        }
        for (auto& [name, value] : channel.virtuals)
        {
            (void)name;
            value.dx = value.dy = 0;
            value.sample_x = value.sample_y = 0;
        }
        if (channel.pending)
        {
            const auto previous = channel.actions;
            const bool had_contribution =
                std::any_of(previous.begin(), previous.end(),
                            [](const auto& item) { return item.second.state.held(); });
            cancel_pointers(state, index);
            channel.data = std::move(*channel.pending);
            channel.pending.reset();
            channel.actions.clear();
            channel.virtuals.clear();
            channel.blocked.clear();
            channel.neutral.clear();
            channel.neutralized.clear();
            for (const auto& action : channel.data.actions)
            {
                channel.actions[action->name].latches.resize(action->bindings.size());
                for (const auto& binding : action->bindings)
                {
                    for (const auto& part : binding->parts)
                    {
                        if (part->virtual_control.empty())
                        {
                            const auto source_key = key(part->source);
                            const double threshold = action->kind == schema::InputActionKind::button
                                                         ? binding->release_threshold
                                                         : binding->deadzone;
                            const auto found = channel.neutral.find(source_key);
                            if (found == channel.neutral.end())
                            {
                                channel.neutral[source_key] = threshold;
                            }
                            else
                            {
                                found->second = std::min(found->second, threshold);
                            }
                        }
                    }
                }
                const auto old = previous.find(action->name);
                if (old != previous.end() && old->second.state.held())
                {
                    channel.actions[action->name].state = {state.frame_id, 0,     0,   false,
                                                           false,          false, true};
                }
            }
            if (had_contribution)
            {
                ++state.cancel_revision;
            }
            for (const auto& control : channel.data.virtual_controls)
            {
                channel.virtuals.emplace(control->id, VirtualValue{});
            }
            if (channel.data.activation == schema::InputActivation::wait_for_neutral)
            {
                for (const auto& [control_key, control] : state.controls)
                {
                    if (std::abs(control.value) > 0.000001)
                    {
                        channel.blocked.insert(control_key);
                    }
                }
            }
        }
    }
    for (auto& capture : state.captures)
    {
        if (capture.alive && capture.pending)
        {
            capture.pending = false;
            for (const auto& [control_key, control] : state.controls)
            {
                if (std::abs(control.value) > capture.options.neutral_threshold)
                {
                    capture.blocked.insert(control_key);
                }
            }
        }
    }
    // Opening a frame establishes a baseline: configuration and reset deltas never press actions.
    evaluate(state, true);
    return true;
}

bool InputSystem::set_routing_state(const schema::InputRoutingDataT& routing)
{
    last_error_.clear();
    const double fields[] = {routing.viewport_x,      routing.viewport_y, routing.viewport_width,
                             routing.viewport_height, routing.safe_left,  routing.safe_top,
                             routing.safe_right,      routing.safe_bottom};
    if (!state_ || !state_->frame_open ||
        !std::all_of(std::begin(fields), std::end(fields),
                     [](double v) { return std::isfinite(v); }) ||
        routing.viewport_width < 0 || routing.viewport_height < 0 || routing.safe_left < 0 ||
        routing.safe_top < 0 || routing.safe_right < 0 || routing.safe_bottom < 0)
    {
        last_error_ = "Invalid input routing state or no open frame";
        return false;
    }
    auto& state = *state_;
    const auto& old = state.routing;
    const bool layout =
        old.viewport_x != routing.viewport_x || old.viewport_y != routing.viewport_y ||
        old.viewport_width != routing.viewport_width ||
        old.viewport_height != routing.viewport_height || old.safe_left != routing.safe_left ||
        old.safe_top != routing.safe_top || old.safe_right != routing.safe_right ||
        old.safe_bottom != routing.safe_bottom;
    const bool keyboard = routing.keyboard_claimed && !old.keyboard_claimed;
    const bool pointer = routing.pointer_claimed && !old.pointer_claimed;
    state.routing = routing;
    if (keyboard || pointer || layout)
    {
        cancel(state, keyboard, pointer || layout, false);
    }
    return true;
}

bool InputSystem::owns_pointer(const SDL_Event& event_value) const noexcept
{
    if (!state_)
    {
        return false;
    }
    if (event_value.type == SDL_EVENT_FINGER_DOWN || event_value.type == SDL_EVENT_FINGER_UP ||
        event_value.type == SDL_EVENT_FINGER_MOTION ||
        event_value.type == SDL_EVENT_FINGER_CANCELED)
    {
        if (event_value.tfinger.windowID != SDL_GetWindowID(window_.native_window()))
        {
            return false;
        }
        for (const auto& device : state_->devices)
        {
            if (device.data.kind == DeviceKind::touch &&
                device.native_id == event_value.tfinger.touchID)
            {
                return state_->pointers.contains(
                    {device.data.connection_id, event_value.tfinger.fingerID});
            }
        }
    }
    return false;
}

bool InputSystem::process_event(const SDL_Event& value, bool claimed)
{
    last_error_.clear();
    if (!state_ || !state_->frame_open)
    {
        last_error_ = "Input event requires begin_frame";
        return false;
    }
    return event(*state_, window_.native_window(), value, claimed, last_error_);
}

bool InputSystem::on_tick(double delta_time)
{
    last_error_.clear();
    if (!state_ || !state_->frame_open || !std::isfinite(delta_time) || delta_time < 0)
    {
        last_error_ = "Input tick requires an open frame and finite nonnegative delta time";
        return false;
    }
    evaluate(*state_);
    publish(*state_);
    frame_ = {state_->frame_id, state_->device_revision, state_->cancel_revision};
    state_->frame_open = false;
    return true;
}

const schema::InputFrameState& InputSystem::frame_state() const noexcept
{
    return frame_;
}
std::span<const schema::InputDeviceDataT> InputSystem::devices() const noexcept
{
    return state_ ? std::span<const schema::InputDeviceDataT>(state_->published_devices)
                  : std::span<const schema::InputDeviceDataT>();
}

bool InputSystem::controls(std::uint64_t device,
                           std::vector<schema::InputControlInfoT>& output) const
{
    output.clear();
    last_error_.clear();
    if (!state_ ||
        !std::any_of(state_->published_devices.begin(), state_->published_devices.end(),
                     [device](const auto& data) { return data.connection_id == device; }))
    {
        last_error_ = "Unknown input device";
        return false;
    }
    // Metadata is copied on publication along with values, so hotplug during the pump is invisible.
    for (const auto& info : state_->published_info)
    {
        if (info.source.connection_id() == device)
        {
            output.push_back(info);
        }
    }
    return true;
}

bool InputSystem::read_control(const schema::InputControlRef& control,
                               schema::InputControlState& output) const
{
    output = {};
    last_error_.clear();
    if (state_)
    {
        const auto found = state_->published_controls.find(key(control));
        if (found != state_->published_controls.end())
        {
            output = found->second;
            return true;
        }
    }
    last_error_ = "Unknown input control";
    return false;
}

bool InputSystem::read_contacts(std::vector<schema::InputContactStateT>& output) const
{
    output.clear();
    last_error_.clear();
    if (!state_)
    {
        last_error_ = "Input is not initialized";
        return false;
    }
    output = state_->published_contacts;
    return true;
}

bool InputSystem::create_channel(schema::InputChannelHandle& output)
{
    output = {};
    last_error_.clear();
    if (!state_)
    {
        last_error_ = "Input is not initialized";
        return false;
    }
    std::uint32_t index = 0;
    while (index < state_->channels.size() && state_->channels[index].alive)
    {
        ++index;
    }
    if (state_->next_generation == 0 || index >= 4096)
    {
        last_error_ = "Input channel capacity or generation exhausted";
        return false;
    }
    if (index == state_->channels.size())
    {
        state_->channels.emplace_back();
    }
    auto& channel = state_->channels[index];
    const auto generation = state_->next_generation++;
    channel = {};
    channel.generation = generation;
    channel.alive = true;
    channel.order = state_->next_order++;
    output = {index, channel.generation};
    return true;
}

namespace
{
Channel* find_channel(State* state, schema::InputChannelHandle handle)
{
    if (state && handle.slot() < state->channels.size())
    {
        auto& channel = state->channels[handle.slot()];
        if (channel.alive && channel.generation == handle.generation())
        {
            return &channel;
        }
    }
    return nullptr;
}

Capture* find_capture(State* state, schema::InputCaptureHandle handle)
{
    if (state && handle.slot() < state->captures.size())
    {
        auto& capture = state->captures[handle.slot()];
        if (capture.alive && capture.generation == handle.generation())
        {
            return &capture;
        }
    }
    return nullptr;
}
} // namespace

bool InputSystem::configure_channel(schema::InputChannelHandle handle,
                                    const schema::InputChannelDataT& data)
{
    last_error_.clear();
    auto* channel = find_channel(state_.get(), handle);
    if (!channel)
    {
        last_error_ = "Invalid input channel handle";
        return false;
    }
    if (!validate(data, last_error_))
    {
        return false;
    }
    const auto valid_device_kind = [this](const schema::InputControlRef& ref)
    {
        const auto device =
            std::find_if(state_->devices.begin(), state_->devices.end(), [&ref](const Device& d)
                         { return d.data.connection_id == ref.connection_id(); });
        if (device == state_->devices.end())
        {
            return true;
        } // Temporarily unavailable sources stay neutral.
        const auto kind = ref.kind();
        switch (device->data.kind)
        {
        case DeviceKind::keyboard:
            return kind == Kind::key;
        case DeviceKind::mouse:
            return kind >= Kind::mouse_button && kind <= Kind::mouse_wheel;
        case DeviceKind::gamepad:
            return kind == Kind::gamepad_button || kind == Kind::gamepad_axis ||
                   kind == Kind::gamepad_touch;
        case DeviceKind::joystick:
            return kind >= Kind::joystick_button && kind <= Kind::joystick_hat;
        case DeviceKind::touch:
            return kind == Kind::touch;
        }
        return false;
    };
    for (const auto& action : data.actions)
    {
        for (const auto& binding : action->bindings)
        {
            for (const auto& modifier : binding->modifiers)
            {
                if (!valid_device_kind(modifier))
                {
                    last_error_ = "Input source does not match device kind";
                    return false;
                }
            }
            for (const auto& part : binding->parts)
            {
                if (part->virtual_control.empty() && !valid_device_kind(part->source))
                {
                    last_error_ = "Input source does not match device kind";
                    return false;
                }
            }
        }
    }
    for (const auto& control : data.virtual_controls)
    {
        if (!valid_device_kind({control->region->connection_id, Kind::touch, 0}))
        {
            last_error_ = "Virtual input region requires a screen-touch device";
            return false;
        }
    }
    channel->pending = std::make_unique<schema::InputChannelDataT>(data);
    return true;
}

bool InputSystem::set_channel_enabled(schema::InputChannelHandle handle, bool enabled,
                                      schema::InputActivation activation)
{
    last_error_.clear();
    auto* channel = find_channel(state_.get(), handle);
    if (!channel || activation < schema::InputActivation::MIN ||
        activation > schema::InputActivation::MAX)
    {
        last_error_ = "Invalid input channel or activation";
        return false;
    }
    if (!channel->pending)
    {
        channel->pending = std::make_unique<schema::InputChannelDataT>(channel->data);
    }
    channel->pending->enabled = enabled;
    channel->pending->activation = activation;
    return true;
}

bool InputSystem::destroy_channel(schema::InputChannelHandle handle)
{
    last_error_.clear();
    auto* channel = find_channel(state_.get(), handle);
    if (!channel)
    {
        last_error_ = "Invalid input channel handle";
        return false;
    }
    cancel_pointers(*state_, handle.slot());
    channel->alive = false;
    channel->pending.reset();
    for (auto& capture : state_->captures)
    {
        if (capture.channel == handle.slot())
        {
            capture.alive = false;
        }
    }
    return true;
}

bool InputSystem::read_action(schema::InputChannelHandle handle, std::string_view name,
                              schema::InputActionState& output) const
{
    output = {};
    last_error_.clear();
    auto* channel = find_channel(state_.get(), handle);
    if (channel)
    {
        const auto found = channel->published.find(name);
        if (found != channel->published.end())
        {
            output = found->second;
            return true;
        }
    }
    last_error_ = "Invalid channel or unknown action";
    return false;
}

bool InputSystem::begin_capture(schema::InputChannelHandle handle,
                                const schema::InputCaptureOptionsT& options,
                                schema::InputCaptureHandle& output)
{
    output = {};
    last_error_.clear();
    if (!find_channel(state_.get(), handle) || state_->next_generation == 0 ||
        options.devices.size() > 4096 || options.kinds.size() > 4096 ||
        !std::isfinite(options.threshold) || !std::isfinite(options.neutral_threshold) ||
        options.threshold <= 0 || options.neutral_threshold < 0 ||
        options.neutral_threshold >= options.threshold ||
        std::any_of(
            options.devices.begin(), options.devices.end(), [](auto v) { return v == 0; }) ||
        std::any_of(
            options.kinds.begin(), options.kinds.end(),
            [](auto v) { return v < Kind::MIN || v > Kind::MAX; }))
    {
        last_error_ = "Invalid input capture options or channel";
        return false;
    }
    std::uint32_t index = 0;
    for (; index < state_->captures.size(); ++index)
    {
        auto& capture = state_->captures[index];
        if (capture.alive && capture.channel == handle.slot())
        {
            if (capture.state.status == schema::InputCaptureStatus::waiting)
            {
                last_error_ = "Channel already has a waiting capture";
                return false;
            }
            break;
        }
    }
    if (index == state_->captures.size())
    {
        const auto available = std::find_if(state_->captures.begin(), state_->captures.end(),
                                            [](const Capture& capture) { return !capture.alive; });
        index = static_cast<std::uint32_t>(available - state_->captures.begin());
    }
    if (index == state_->captures.size())
    {
        state_->captures.emplace_back();
    }
    auto& capture = state_->captures[index];
    auto generation = state_->next_generation++;
    capture = {};
    capture.generation = generation;
    capture.alive = true;
    capture.channel = handle.slot();
    capture.options = options;
    output = {index, capture.generation};
    return true;
}

bool InputSystem::read_capture(schema::InputCaptureHandle handle,
                               schema::InputCaptureStateT& output) const
{
    output = {};
    last_error_.clear();
    auto* capture = find_capture(state_.get(), handle);
    if (!capture)
    {
        last_error_ = "Invalid input capture handle";
        return false;
    }
    output = capture->published;
    return true;
}

bool InputSystem::cancel_capture(schema::InputCaptureHandle handle)
{
    last_error_.clear();
    auto* capture = find_capture(state_.get(), handle);
    if (!capture || capture->state.status == schema::InputCaptureStatus::completed)
    {
        last_error_ = "Invalid or completed input capture";
        return false;
    }
    capture->pending = false;
    capture->state.status = schema::InputCaptureStatus::canceled;
    capture->state.frame_id = state_->frame_id;
    return true;
}

const std::string& InputSystem::last_error() const noexcept
{
    return last_error_;
}
} // namespace engine
