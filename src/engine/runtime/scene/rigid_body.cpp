#include "runtime/scene/rigid_body.hpp"

#include "runtime/scene/ecs_storage.hpp"

#include "runtime/scene/collider.hpp"
#include "runtime/scene/node.hpp"

#include <cmath>

namespace engine
{
namespace
{

bool bounded(double value, double minimum, double maximum) noexcept
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

bool vector_bounded(const Vec3& value, double limit) noexcept
{
    return bounded(value.x(), -limit, limit) && bounded(value.y(), -limit, limit) &&
           bounded(value.z(), -limit, limit);
}

bool zero(const Vec3& value) noexcept
{
    return value.x() == 0.0 && value.y() == 0.0 && value.z() == 0.0;
}

} // namespace

bool RigidBody::register_type(ComponentRegistry& registry)
{
    return register_component_storage<RigidBody, Data, Runtime>(registry);
}

RigidBody::Runtime* RigidBody::runtime() const noexcept
{
    return runtime_state<Runtime>();
}

bool RigidBody::valid_settings(const schema::RigidBodyDataT& value) noexcept
{
    return value.motion >= schema::BodyMotion::static_body &&
           value.motion <= schema::BodyMotion::dynamic && bounded(value.mass, 0.0001F, 1000000.0) &&
           bounded(value.gravity_factor, 0.0, 100.0) && bounded(value.linear_damping, 0.0, 1.0) &&
           bounded(value.angular_damping, 0.0, 1.0) &&
           vector_bounded(value.linear_velocity, 10000.0) &&
           vector_bounded(value.angular_velocity, 10000.0) &&
           (value.motion != schema::BodyMotion::static_body ||
            (zero(value.linear_velocity) && zero(value.angular_velocity)));
}

schema::RigidBodyDataT RigidBody::settings() const noexcept
{
    const auto* value = read_data<Data>();
    return value ? *value : Data{};
}

bool RigidBody::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}

bool RigidBody::set_settings(const schema::RigidBodyDataT& value) noexcept
{
    if (!writable())
    {
        return false;
    }
    if (!valid_settings(value))
    {
        return false;
    }
    auto collider = node().get_component<Collider>();
    if (value.motion != schema::BodyMotion::static_body && collider.valid() &&
        collider.settings().shape == schema::ColliderShape::mesh)
    {
        return false;
    }
    *write_data<Data>() = value;
    modified();
    if (value.motion != schema::BodyMotion::dynamic)
    {
        runtime()->pending_impulse_ = {};
    }
    return true;
}

bool RigidBody::restore_data(const schema::ComponentDataT& value)
{
    if (!value.flags)
    {
        return false;
    }
    if (!writable())
    {
        return false;
    }
    const auto* settings = value.payload.AsRigidBodyData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    runtime()->pending_impulse_ = {};
    if (!restore_flags(value))
    {
        return false;
    }
    return true;
}

bool RigidBody::add_impulse(const Vec3& value) noexcept
{
    if (!writable())
    {
        return false;
    }
    if (!active() || settings().motion != schema::BodyMotion::dynamic ||
        !vector_bounded(value, 1000000.0))
    {
        return false;
    }
    const Vec3 sum(runtime()->pending_impulse_.x() + value.x(),
                   runtime()->pending_impulse_.y() + value.y(),
                   runtime()->pending_impulse_.z() + value.z());
    if (!vector_bounded(sum, 1000000.0))
    {
        return false;
    }
    runtime()->pending_impulse_ = sum;
    return true;
}

bool RigidBody::set_linear_velocity(const Vec3& value) noexcept
{
    if (!writable())
    {
        return false;
    }
    auto replacement = settings();
    replacement.linear_velocity = value;
    return set_settings(replacement);
}

Vec3 RigidBody::linear_velocity() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return settings().linear_velocity;
}

} // namespace engine
