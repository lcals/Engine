#include "plugins/runtime/character_controller/character_controller.hpp"

#include "plugins/runtime/character_controller/character_controller_runtime.hpp"

#include "runtime/scene/ecs_storage.hpp"

#include "runtime/scene/character_motor.hpp"
#include "runtime/scene/node.hpp"

#include <cmath>

namespace engine::plugins::character_controller
{
namespace
{

bool bounded(float value, float minimum, float maximum) noexcept
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

} // namespace

bool CharacterController::register_type(ComponentRegistry& registry)
{
    return register_component_storage<CharacterController, Data, Runtime>(registry);
}

CharacterController::Runtime* CharacterController::runtime() const noexcept
{
    return runtime_state<Runtime>();
}

bool CharacterController::valid_settings(const schema::CharacterControllerDataT& value) noexcept
{
    return bounded(value.move_speed, 0.0F, 1000.0F) && bounded(value.jump_speed, 0.01F, 1000.0F) &&
           bounded(value.look_sensitivity, 0.0F, 100.0F);
}

schema::CharacterControllerDataT CharacterController::settings() const noexcept
{
    const auto* value = read_data<Data>();
    return value ? *value : Data{};
}

bool CharacterController::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}

bool CharacterController::set_settings(const schema::CharacterControllerDataT& value) noexcept
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

bool CharacterController::set_input(const CharacterInput& input) noexcept
{
    if (!writable())
    {
        return false;
    }
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
    runtime()->input_ = input;
    return true;
}

void CharacterController::clear_input() noexcept
{
    if (!writable())
    {
        return;
    }
    runtime()->input_ = {};
    auto motor = node().get_component<CharacterMotor>();
    if (motor.valid() && motor.instance_id() == runtime()->driven_motor_)
    {
        motor.clear_input();
    }
    runtime()->driven_motor_ = 0;
}

bool CharacterController::restore_data(const schema::ComponentDataT& value)
{
    if (!value.flags)
    {
        return false;
    }
    if (!writable())
    {
        return false;
    }
    const auto* settings = value.payload.AsCharacterControllerData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    if (!restore_flags(value))
    {
        return false;
    }
    clear_input();
    return true;
}

bool CharacterController::remap_node_references(
    schema::ComponentDataT& data, std::span<const schema::NodeIdMappingT> mapping) const
{
    auto* settings = data.payload.AsCharacterControllerData();
    if (!settings)
    {
        return false;
    }
    if (settings->camera_node_id == 0)
    {
        return true;
    }
    for (const auto& entry : mapping)
    {
        if (entry.source_id == settings->camera_node_id)
        {
            if (entry.target_id == 0 || entry.retired)
            {
                return false;
            }
            settings->camera_node_id = entry.target_id;
            return true;
        }
    }
    return false;
}

} // namespace engine::plugins::character_controller
