#include "runtime/scene/character_motor.hpp"

#include <cmath>
#include <utility>

namespace engine
{
namespace
{

schema::ComponentDataT make_data()
{
    schema::ComponentDataT data;
    data.payload.Set(schema::CharacterMotorDataT{});
    return data;
}

bool bounded(float value, float minimum, float maximum) noexcept
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

bool valid_velocity(const Vec3& value) noexcept
{
    return std::isfinite(value.x()) && std::isfinite(value.y()) && std::isfinite(value.z()) &&
           std::abs(value.x()) <= 10000.0 && std::abs(value.y()) <= 10000.0 &&
           std::abs(value.z()) <= 10000.0;
}

} // namespace

CharacterMotor::CharacterMotor(Node& node) : Component(node, make_data())
{
}


bool CharacterMotor::valid_settings(const schema::CharacterMotorDataT& value) noexcept
{
    return bounded(value.radius, 0.01F, 1000.0F) && bounded(value.half_height, 0.01F, 1000.0F) &&
           bounded(value.max_slope_degrees, 0.0F, 89.0F) &&
           bounded(value.step_height, 0.0F, 1000.0F) &&
           bounded(value.ground_snap_distance, 0.0F, 1000.0F) &&
           bounded(value.gravity_factor, 0.0F, 1000.0F) &&
           bounded(value.external_damping, 0.0F, 1000.0F);
}

const schema::CharacterMotorDataT& CharacterMotor::settings() const noexcept
{
    return *data().payload.AsCharacterMotorData();
}

bool CharacterMotor::set_settings(const schema::CharacterMotorDataT& value) noexcept
{
    if (!valid_settings(value))
    {
        return false;
    }
    *mutable_data().payload.AsCharacterMotorData() = value;
    return true;
}

bool CharacterMotor::restore_data(const schema::ComponentDataT& value)
{
    const auto* settings = value.payload.AsCharacterMotorData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    desired_velocity_ = {};
    velocity_ = {};
    pending_velocity_ = {};
    pending_jump_ = 0.0F;
    grounded_ = false;
    reset_pending_ = true;
    set_enabled(value.enabled);
    set_visible(value.visible);
    return true;
}


bool CharacterMotor::set_velocity(const Vec3& value) noexcept
{
    if (!active() || value.y() != 0.0 || !valid_velocity(value))
    {
        return false;
    }
    desired_velocity_ = value;
    return true;
}

void CharacterMotor::clear_input() noexcept
{
    desired_velocity_ = {};
    pending_jump_ = 0.0F;
}

bool CharacterMotor::add_velocity(const Vec3& delta) noexcept
{
    if (!active() || !valid_velocity(delta))
    {
        return false;
    }
    const Vec3 sum(pending_velocity_.x() + delta.x(), pending_velocity_.y() + delta.y(),
                   pending_velocity_.z() + delta.z());
    if (!valid_velocity(sum))
    {
        return false;
    }
    pending_velocity_ = sum;
    return true;
}

const Vec3& CharacterMotor::velocity() const noexcept
{
    return velocity_;
}

bool CharacterMotor::jump(float speed) noexcept
{
    if (!active() || !grounded_ || pending_jump_ != 0.0F || !bounded(speed, 0.01F, 1000.0F))
    {
        return false;
    }
    pending_jump_ = speed;
    return true;
}

bool CharacterMotor::grounded() const noexcept
{
    return active() && grounded_;
}

} // namespace engine
