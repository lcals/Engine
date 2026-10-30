#include "runtime/scene/collider.hpp"

#include <cmath>

namespace engine
{
namespace
{

bool bounded(double value, double minimum, double maximum) noexcept
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

} // namespace

Collider::Collider(Node& node) : Component(node)
{
    mutable_data().payload.Set(schema::ColliderDataT{});
}

bool Collider::valid_settings(const schema::ColliderDataT& value) noexcept
{
    return value.shape >= schema::ColliderShape::box &&
           value.shape <= schema::ColliderShape::capsule &&
           bounded(value.half_extents.x(), 0.0001, 100000.0) &&
           bounded(value.half_extents.y(), 0.0001, 100000.0) &&
           bounded(value.half_extents.z(), 0.0001, 100000.0) &&
           bounded(value.radius, 0.0001F, 100000.0) && bounded(value.half_height, 0.0, 100000.0) &&
           bounded(value.center.x(), -100000.0, 100000.0) &&
           bounded(value.center.y(), -100000.0, 100000.0) &&
           bounded(value.center.z(), -100000.0, 100000.0) && bounded(value.friction, 0.0, 1.0) &&
           bounded(value.restitution, 0.0, 1.0);
}

const schema::ColliderDataT& Collider::settings() const noexcept
{
    return *data().payload.AsColliderData();
}

bool Collider::set_settings(const schema::ColliderDataT& value) noexcept
{
    if (!valid_settings(value))
    {
        return false;
    }
    *mutable_data().payload.AsColliderData() = value;
    return true;
}

bool Collider::restore_data(const schema::ComponentDataT& value)
{
    const auto* settings = value.payload.AsColliderData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    set_enabled(value.enabled);
    return true;
}

} // namespace engine
