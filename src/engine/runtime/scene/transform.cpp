#include "runtime/scene/transform.hpp"

#include "runtime/scene/ecs_storage.hpp"

#include "runtime/scene/node.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace engine
{
namespace
{

constexpr std::array<double, 16> identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

bool finite_vector(const Vec3& value)
{
    return std::isfinite(value.x()) && std::isfinite(value.y()) && std::isfinite(value.z());
}

} // namespace

bool Transform::register_type(ComponentRegistry& registry)
{
    return register_component_storage<Transform, Data>(registry);
}

TransformSettings Transform::settings() const noexcept
{
    const auto* value = read_data<Data>();
    return value ? *value : Data{};
}

bool Transform::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}

bool Transform::set_settings(const TransformSettings& transform) noexcept
{
    if (!writable())
    {
        return false;
    }
    if (!finite_vector(transform.position) || !finite_vector(transform.rotation) ||
        !finite_vector(transform.scale) || transform.scale.x() == 0.0 ||
        transform.scale.y() == 0.0 || transform.scale.z() == 0.0)
    {
        return false;
    }
    *write_data<Data>() = transform;
    modified();
    return true;
}

bool Transform::world_matrix(float* output) const noexcept
{
    if (output == nullptr)
    {
        return false;
    }
    std::transform(identity.begin(), identity.end(), output,
                   [](double value) { return static_cast<float>(value); });
    const auto owner = node();
    if (!valid() || !owner.world()->refresh_scene_state())
    {
        return false;
    }
    flecs::world world(owner.world()->native_world());
    const auto* transform = world.entity(owner.id()).try_get<ecs_detail::WorldTransform>();
    if (!transform || !transform->valid)
    {
        return false;
    }
    std::copy(transform->matrix.begin(), transform->matrix.end(), output);
    return true;
}

bool Transform::restore_data(const schema::ComponentDataT& data)
{
    if (!data.flags)
    {
        return false;
    }
    if (!writable())
    {
        return false;
    }
    const auto* settings = data.payload.AsTransformData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    if (!restore_flags(data))
    {
        return false;
    }
    return true;
}

} // namespace engine
