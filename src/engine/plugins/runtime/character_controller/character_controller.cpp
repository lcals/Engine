#include "plugins/runtime/character_controller/character_controller.hpp"

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
    return (value.mode == schema::CharacterView::first_person ||
            value.mode == schema::CharacterView::third_person) &&
           bounded(value.move_speed, 0.0F, 1000.0F) && bounded(value.jump_speed, 0.01F, 1000.0F) &&
           bounded(value.look_sensitivity, 0.0F, 100.0F) &&
           bounded(value.eye_height, 0.01F, 1000.0F) &&
           bounded(value.camera_distance, 0.01F, 10000.0F) &&
           bounded(value.pitch_min, -89.0F, 89.0F) && bounded(value.pitch_max, -89.0F, 89.0F) &&
           value.pitch_min <= value.pitch_max;
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
    if (settings().camera_node_id != value.camera_node_id)
    {
        view_initialized_ = false;
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
    view_initialized_ = false;
    driving_ = false;
    set_enabled(value.enabled);
    return true;
}

} // namespace engine::plugins::character_controller
