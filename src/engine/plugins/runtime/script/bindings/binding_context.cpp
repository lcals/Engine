#include "plugins/runtime/script/bindings/binding_context.hpp"

#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <cmath>
#include <limits>

namespace engine::plugins::script::bindings
{
BindingContext::BindingContext(WorldSystem* world, RenderSystem* renderer, UiSystem* ui) noexcept
    : world_(world), renderer_(renderer), ui_(ui)
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
    if (world_ != nullptr && world_->scene() != nullptr && world_->scene()->id() == scene_id_)
    {
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

std::uint64_t BindingContext::scene_id() const noexcept
{
    return scene_id_;
}

void BindingContext::set_scene_id(std::uint64_t value) noexcept
{
    scene_id_ = value;
}

double BindingContext::delta_time() const noexcept
{
    return callable_ ? delta_time_ : 0.0;
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
