#include "runtime/scene/rigid_body.hpp"

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

RigidBody::RigidBody(Node& node) : Component(node)
{
    mutable_data().payload.Set(schema::RigidBodyDataT{});
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

const schema::RigidBodyDataT& RigidBody::settings() const noexcept
{
    return *data().payload.AsRigidBodyData();
}

bool RigidBody::set_settings(const schema::RigidBodyDataT& value) noexcept
{
    if (!valid_settings(value))
    {
        return false;
    }
    const auto* collider = node().get_component<Collider>();
    if (value.motion != schema::BodyMotion::static_body && collider != nullptr &&
        collider->settings().shape == schema::ColliderShape::mesh)
    {
        return false;
    }
    *mutable_data().payload.AsRigidBodyData() = value;
    if (value.motion != schema::BodyMotion::dynamic)
    {
        pending_impulse_ = {};
    }
    return true;
}

bool RigidBody::restore_data(const schema::ComponentDataT& value)
{
    const auto* settings = value.payload.AsRigidBodyData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    pending_impulse_ = {};
    set_enabled(value.enabled);
    return true;
}

bool RigidBody::add_impulse(const Vec3& value) noexcept
{
    if (!enabled() || settings().motion != schema::BodyMotion::dynamic ||
        !vector_bounded(value, 1000000.0))
    {
        return false;
    }
    const Vec3 sum(pending_impulse_.x() + value.x(), pending_impulse_.y() + value.y(),
                   pending_impulse_.z() + value.z());
    if (!vector_bounded(sum, 1000000.0))
    {
        return false;
    }
    pending_impulse_ = sum;
    return true;
}

bool RigidBody::set_linear_velocity(const Vec3& value) noexcept
{
    auto replacement = settings();
    replacement.linear_velocity = value;
    return set_settings(replacement);
}

const Vec3& RigidBody::linear_velocity() const noexcept
{
    return settings().linear_velocity;
}

} // namespace engine
