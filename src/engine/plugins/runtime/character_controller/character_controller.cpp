#include "plugins/runtime/character_controller/character_controller.hpp"

#include <atomic>
#include <cmath>
#include <limits>

namespace engine::plugins::character_controller
{
namespace
{

schema::ComponentDataT make_data()
{
    schema::ComponentDataT data;
    data.payload.Set(schema::CharacterControllerDataT{});
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

CharacterController::CharacterController(Node& node)
    : Component(node, make_data()), instance_id_(next_instance_id())
{
}

bool CharacterController::initialize()
{
    return instance_id_ != 0;
}

std::uint64_t CharacterController::instance_id() const noexcept
{
    return instance_id_;
}

bool CharacterController::valid_settings(const schema::CharacterControllerDataT& value) noexcept
{
    return bounded(value.move_speed, 0.0F, 1000.0F) && bounded(value.jump_speed, 0.01F, 1000.0F) &&
           bounded(value.look_sensitivity, 0.0F, 100.0F);
}

const schema::CharacterControllerDataT& CharacterController::settings() const noexcept
{
    return *data().payload.AsCharacterControllerData();
}

bool CharacterController::set_settings(const schema::CharacterControllerDataT& value) noexcept
{
    if (!valid_settings(value))
    {
        return false;
    }
    *mutable_data().payload.AsCharacterControllerData() = value;
    return true;
}

bool CharacterController::restore_data(const schema::ComponentDataT& value)
{
    const auto* settings = value.payload.AsCharacterControllerData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    set_enabled(value.enabled);
    return true;
}

} // namespace engine::plugins::character_controller
