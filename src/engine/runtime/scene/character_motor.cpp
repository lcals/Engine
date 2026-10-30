#include "runtime/scene/character_motor.hpp"

#include <atomic>
#include <cmath>
#include <limits>
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

std::uint64_t next_instance_id() noexcept
{
    static std::atomic<std::uint64_t> sequence{0};
    auto current = sequence.load(std::memory_order_relaxed);
    while (current != std::numeric_limits<std::uint64_t>::max())
    {
        if (sequence.compare_exchange_weak(current, current + 1, std::memory_order_relaxed))
        {
            return current + 1;
        }
    }
    return 0;
}

bool bounded(float value, float minimum, float maximum) noexcept
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

} // namespace

CharacterMotor::CharacterMotor(Node& node)
    : Component(node, make_data()), instance_id_(next_instance_id())
{
}

bool CharacterMotor::initialize()
{
    return instance_id_ != 0;
}

bool CharacterMotor::valid_settings(const schema::CharacterMotorDataT& value) noexcept
{
    return bounded(value.radius, 0.01F, 1000.0F) && bounded(value.half_height, 0.01F, 1000.0F) &&
           bounded(value.max_slope_degrees, 0.0F, 89.0F) &&
           bounded(value.step_height, 0.0F, 1000.0F) &&
           bounded(value.ground_snap_distance, 0.0F, 1000.0F) &&
           bounded(value.gravity_factor, 0.0F, 1000.0F);
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
    pending_jump_ = 0.0F;
    grounded_ = false;
    set_enabled(value.enabled);
    return true;
}

bool CharacterMotor::set_velocity(const Vec3& value) noexcept
{
    if (!enabled() || value.y() != 0.0 || !std::isfinite(value.x()) || !std::isfinite(value.z()) ||
        std::abs(value.x()) > 10000.0 || std::abs(value.z()) > 10000.0)
    {
        return false;
    }
    desired_velocity_ = value;
    return true;
}

const Vec3& CharacterMotor::velocity() const noexcept
{
    return velocity_;
}

bool CharacterMotor::jump(float speed) noexcept
{
    if (!enabled() || !grounded_ || pending_jump_ != 0.0F || !bounded(speed, 0.01F, 1000.0F))
    {
        return false;
    }
    pending_jump_ = speed;
    return true;
}

bool CharacterMotor::grounded() const noexcept
{
    return enabled() && grounded_;
}

} // namespace engine
