#include "plugins/runtime/character_controller/character_controller.hpp"

#include "runtime/scene/character_motor.hpp"
#include "runtime/scene/node.hpp"

#include <cmath>

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

bool bounded(float value, float minimum, float maximum) noexcept
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

} // namespace

CharacterController::CharacterController(Node& node) : Component(node, make_data())
{
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

bool CharacterController::set_input(const CharacterInput& input) noexcept
{
    const auto valid = [](const Vec2& value)
    {
        return std::isfinite(value.x()) && std::isfinite(value.y()) &&
               std::abs(value.x()) <= 1000000.0 && std::abs(value.y()) <= 1000000.0;
    };
    if (!active())
    {
        clear_input();
        return false;
    }
    if (!valid(input.move) || !valid(input.look))
    {
        return false;
    }
    input_ = input;
    return true;
}

void CharacterController::clear_input() noexcept
{
    input_ = {};
    auto* motor = node().get_component<CharacterMotor>();
    if (motor != nullptr && motor->instance_id() == driven_motor_)
    {
        motor->clear_input();
    }
    driven_motor_ = 0;
}

bool CharacterController::restore_data(const schema::ComponentDataT& value)
{
    const auto* settings = value.payload.AsCharacterControllerData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    set_enabled(value.enabled);
    set_visible(value.visible);
    clear_input();
    return true;
}

} // namespace engine::plugins::character_controller
