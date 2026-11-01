#include "plugins/runtime/script/bindings/binding_context.hpp"

#include "runtime/app/app.hpp"
#include "runtime/input/input_system.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/navigation/navigation_system.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::plugins::script::bindings
{
BindingContext::BindingContext(WorldSystem* world, RenderSystem* renderer, UiSystem* ui,
                               PhysicsSystem* physics, NavigationSystem* navigation,
                               InputSystem* input) noexcept
    : world_(world), renderer_(renderer), ui_(ui), physics_(physics), navigation_(navigation),
      input_(input)
{
}

BindingContext::~BindingContext() noexcept
{
    shutdown();
}

void BindingContext::activate() noexcept
{
    active_ = true;
}

void BindingContext::begin_call(double delta_time) noexcept
{
    callable_ = active_;
    delta_time_ = std::isfinite(delta_time) ? delta_time : 0.0;
}

void BindingContext::end_call() noexcept
{
    callable_ = false;
    delta_time_ = 0.0;
}

void BindingContext::shutdown() noexcept
{
    active_ = false;
    end_call();
    if (input_ && input_->is_initialized())
    {
        for (const auto channel : input_channels_)
        {
            if (!input_->destroy_channel(channel))
            {
                LogSystem::write(LogLevel::warning, "Cannot release a script input channel");
            }
        }
    }
    input_channels_.clear();
    input_captures_.clear();
    input_error_.clear();
    if (world_ != nullptr && world_->scene() != nullptr && world_->scene()->id() == scene_id_)
    {
        if (navigation_)
        {
            navigation_->clear();
        }
        world_->destroy_scene();
    }
    scene_id_ = 0;
}

WorldSystem* BindingContext::world() const noexcept
{
    return world_;
}

RenderSystem* BindingContext::renderer() const noexcept
{
    return callable_ ? renderer_ : nullptr;
}

UiSystem* BindingContext::ui() const noexcept
{
    return callable_ ? ui_ : nullptr;
}

PhysicsSystem* BindingContext::physics() const noexcept
{
    return callable_ ? physics_ : nullptr;
}

NavigationSystem* BindingContext::navigation() const noexcept
{
    return callable_ ? navigation_ : nullptr;
}

InputSystem* BindingContext::input() const noexcept
{
    return callable_ ? input_ : nullptr;
}

void BindingContext::remember_input_channel(schema::InputChannelHandle channel)
{
    input_channels_.push_back(channel);
}

bool BindingContext::owns_input_channel(schema::InputChannelHandle channel) const noexcept
{
    return std::any_of(
        input_channels_.begin(), input_channels_.end(), [channel](const auto& value)
        { return value.slot() == channel.slot() && value.generation() == channel.generation(); });
}

void BindingContext::forget_input_channel(schema::InputChannelHandle channel)
{
    const auto matches = [channel](const auto& value)
    { return value.slot() == channel.slot() && value.generation() == channel.generation(); };
    std::erase_if(input_channels_, matches);
    std::erase_if(input_captures_, [&matches](const auto& value) { return matches(value.first); });
}

void BindingContext::remember_input_capture(schema::InputChannelHandle channel,
                                            schema::InputCaptureHandle capture)
{
    std::erase_if(input_captures_,
                  [channel](const auto& value)
                  {
                      return value.first.slot() == channel.slot() &&
                             value.first.generation() == channel.generation();
                  });
    input_captures_.emplace_back(channel, capture);
}

bool BindingContext::owns_input_capture(schema::InputCaptureHandle capture) const noexcept
{
    return std::any_of(input_captures_.begin(), input_captures_.end(),
                       [capture](const auto& value)
                       {
                           return value.second.slot() == capture.slot() &&
                                  value.second.generation() == capture.generation();
                       });
}

void BindingContext::set_input_error(std::string_view error)
{
    input_error_ = error;
}

const std::string& BindingContext::input_error() const noexcept
{
    return input_error_;
}

std::uint64_t BindingContext::scene_id() const noexcept
{
    return scene_id_;
}

void BindingContext::set_scene_id(std::uint64_t value) noexcept
{
    if (active_ && scene_id_ != value && navigation_)
    {
        navigation_->clear();
    }
    scene_id_ = value;
}

double BindingContext::delta_time() const noexcept
{
    return callable_ ? delta_time_ : 0.0;
}

KeyValueStore& BindingContext::storage()
{
    if (!storage_attempted_)
    {
        storage_attempted_ = true;
        const auto directory = App::is_initialized() ? App::persistent_data_path() / "userdata"
                                                     : std::filesystem::path{};
        if (!storage_.load(directory))
        {
            return storage_;
        }
    }
    return storage_;
}

void BindingContext::set_storage_error(std::string_view error)
{
    storage_error_ = error;
}

const std::string& BindingContext::storage_error()
{
    return storage_error_.empty() ? storage().last_error() : storage_error_;
}

BindingContext* BindingContext::current(const Arguments& arguments, bool require_callable)
{
    auto* active = static_cast<BindingContext*>(arguments.owner());
    if (active == nullptr || !active->active_ || (require_callable && !active->callable_))
    {
        return nullptr;
    }
    return active;
}

Scene* BindingContext::scene(const Arguments& arguments)
{
    auto* binding = current(arguments);
    std::uint64_t id = 0;
    if (binding == nullptr || binding->world_ == nullptr || !binding->world_->is_initialized() ||
        !identifier(arguments, 0, id) || binding->scene_id_ == 0 || binding->scene_id_ != id)
    {
        return nullptr;
    }
    auto* value = binding->world_->scene();
    return value != nullptr && value->id() == id ? value : nullptr;
}

Node* BindingContext::node(const Arguments& arguments)
{
    auto* value = scene(arguments);
    std::uint64_t id = 0;
    return value != nullptr && identifier(arguments, 1, id) ? value->find_node(id) : nullptr;
}

Component* BindingContext::component(const Arguments& arguments)
{
    auto* value = node(arguments);
    if (value == nullptr || arguments.count() < 3 || !arguments.is_int32(2))
    {
        return nullptr;
    }
    const auto kind = arguments.integer(2);
    if (kind <= 0 || kind > std::numeric_limits<std::uint8_t>::max())
    {
        return nullptr;
    }
    return value->get_component(static_cast<ComponentType>(kind));
}

bool BindingContext::identifier(const Arguments& arguments, int index, std::uint64_t& output)
{
    output = 0;
    return arguments.identifier(index, output);
}

bool BindingContext::number(const Arguments& arguments, int index, double& output)
{
    output = 0.0;
    return arguments.number(index, output);
}

bool BindingContext::scalar(const Arguments& arguments, int index, float& output)
{
    output = 0.0F;
    double value = 0.0;
    if (!number(arguments, index, value) || std::abs(value) > std::numeric_limits<float>::max())
    {
        return false;
    }
    output = static_cast<float>(value);
    return true;
}

bool BindingContext::text(const Arguments& arguments, int index, std::string& output)
{
    output.clear();
    return arguments.text(index, output);
}

} // namespace engine::plugins::script::bindings
