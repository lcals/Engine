#include "plugins/runtime/character_controller/character_camera.hpp"

#include "runtime/scene/node.hpp"
#include "runtime/scene/transform.hpp"

#include <algorithm>
#include <cmath>

namespace engine::plugins::character_controller
{
namespace
{

schema::ComponentDataT make_data()
{
    schema::ComponentDataT data;
    data.payload.Set(schema::CharacterCameraDataT{});
    return data;
}

} // namespace

CharacterCamera::CharacterCamera(Node& node) : Component(node, make_data())
{
}

bool CharacterCamera::valid_settings(const schema::CharacterCameraDataT& value) noexcept
{
    const auto bounded = [](float v, float low, float high)
    { return std::isfinite(v) && v >= low && v <= high; };
    return value.mode >= schema::CharacterView::MIN && value.mode <= schema::CharacterView::MAX &&
           bounded(value.eye_height, 0.01F, 1000.0F) &&
           bounded(value.camera_distance, 0.01F, 10000.0F) &&
           bounded(value.pitch_min, -89.0F, 89.0F) && bounded(value.pitch_max, -89.0F, 89.0F) &&
           value.pitch_min <= value.pitch_max && bounded(value.collision_radius, 0.01F, 100.0F) &&
           bounded(value.collision_margin, 0.0F, 100.0F);
}

const schema::CharacterCameraDataT& CharacterCamera::settings() const noexcept
{
    return *data().payload.AsCharacterCameraData();
}

bool CharacterCamera::set_settings(const schema::CharacterCameraDataT& value) noexcept
{
    if (!valid_settings(value))
    {
        return false;
    }
    *mutable_data().payload.AsCharacterCameraData() = value;
    pitch_ = std::clamp(pitch_, static_cast<double>(value.pitch_min),
                        static_cast<double>(value.pitch_max));
    return true;
}

bool CharacterCamera::restore_data(const schema::ComponentDataT& value)
{
    const auto* settings = value.payload.AsCharacterCameraData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    angles_initialized_ = false;
    set_enabled(value.enabled);
    return true;
}

bool CharacterCamera::synchronize_angles() noexcept
{
    if (angles_initialized_)
    {
        return true;
    }
    const auto* transform = node().get_component<Transform>();
    if (transform == nullptr)
    {
        return false;
    }
    const auto& rotation = transform->settings().rotation;
    return set_angles(rotation.y(), rotation.x());
}

bool CharacterCamera::set_angles(double yaw, double pitch) noexcept
{
    if (!std::isfinite(yaw) || !std::isfinite(pitch))
    {
        return false;
    }
    yaw_ = std::remainder(yaw, 360.0);
    pitch_ = std::clamp(pitch, static_cast<double>(settings().pitch_min),
                        static_cast<double>(settings().pitch_max));
    angles_initialized_ = true;
    return true;
}

double CharacterCamera::yaw() const noexcept
{
    return yaw_;
}

double CharacterCamera::pitch() const noexcept
{
    return pitch_;
}

} // namespace engine::plugins::character_controller
