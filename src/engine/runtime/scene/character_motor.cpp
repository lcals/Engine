#include "runtime/scene/character_motor.hpp"

#include "runtime/scene/character_motor_runtime.hpp"

#include "runtime/scene/ecs_storage.hpp"

#include <cmath>
#include <utility>

namespace engine
{
namespace
{

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

bool CharacterMotor::register_type(ComponentRegistry& registry)
{
    return register_component_storage<CharacterMotor, Data, Runtime>(registry);
}

CharacterMotor::Runtime* CharacterMotor::runtime() const noexcept
{
    return runtime_state<Runtime>();
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

schema::CharacterMotorDataT CharacterMotor::settings() const noexcept
{
    const auto* value = read_data<Data>();
    return value ? *value : Data{};
}

bool CharacterMotor::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}

bool CharacterMotor::set_settings(const schema::CharacterMotorDataT& value) noexcept
{
    if (!writable())
    {
        return false;
    }
    if (!valid_settings(value))
    {
        return false;
    }
    *write_data<Data>() = value;
    modified();
    return true;
}

bool CharacterMotor::restore_data(const schema::ComponentDataT& value)
{
    if (!value.flags)
    {
        return false;
    }
    if (!writable())
    {
        return false;
    }
    const auto* settings = value.payload.AsCharacterMotorData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    runtime()->desired_velocity_ = {};
    runtime()->velocity_ = {};
    runtime()->pending_velocity_ = {};
    runtime()->pending_jump_ = 0.0F;
    runtime()->grounded_ = false;
    runtime()->reset_pending_ = true;
    if (!restore_flags(value))
    {
        return false;
    }
    return true;
}

bool CharacterMotor::set_velocity(const Vec3& value) noexcept
{
    if (!writable())
    {
        return false;
    }
    if (!active() || value.y() != 0.0 || !valid_velocity(value))
    {
        return false;
    }
    runtime()->desired_velocity_ = value;
    return true;
}

void CharacterMotor::clear_input() noexcept
{
    if (!writable())
    {
        return;
    }
    runtime()->desired_velocity_ = {};
    runtime()->pending_jump_ = 0.0F;
}

bool CharacterMotor::add_velocity(const Vec3& delta) noexcept
{
    if (!writable())
    {
        return false;
    }
    if (!active() || !valid_velocity(delta))
    {
        return false;
    }
    const Vec3 sum(runtime()->pending_velocity_.x() + delta.x(),
                   runtime()->pending_velocity_.y() + delta.y(),
                   runtime()->pending_velocity_.z() + delta.z());
    if (!valid_velocity(sum))
    {
        return false;
    }
    runtime()->pending_velocity_ = sum;
    return true;
}

Vec3 CharacterMotor::velocity() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->velocity_;
}

bool CharacterMotor::jump(float speed) noexcept
{
    if (!writable())
    {
        return false;
    }
    if (!active() || !runtime()->grounded_ || runtime()->pending_jump_ != 0.0F ||
        !bounded(speed, 0.01F, 1000.0F))
    {
        return false;
    }
    runtime()->pending_jump_ = speed;
    return true;
}

bool CharacterMotor::grounded() const noexcept
{
    if (!valid())
    {
        return false;
    }
    return active() && runtime()->grounded_;
}

} // namespace engine
