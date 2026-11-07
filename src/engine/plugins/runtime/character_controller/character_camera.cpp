#include "plugins/runtime/character_controller/character_camera.hpp"

#include "plugins/runtime/character_controller/character_camera_runtime.hpp"

#include "runtime/scene/ecs_storage.hpp"

#include "runtime/scene/node.hpp"
#include "runtime/scene/transform.hpp"

#include <algorithm>
#include <cmath>

namespace engine::plugins::character_controller
{
bool CharacterCamera::register_type(ComponentRegistry& registry)
{
    return register_component_storage<CharacterCamera, Data, Runtime>(registry);
}

CharacterCamera::Runtime* CharacterCamera::runtime() const noexcept
{
    return runtime_state<Runtime>();
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

schema::CharacterCameraDataT CharacterCamera::settings() const noexcept
{
    Data output{};
    (void)get_settings(output);
    return output;
}

bool CharacterCamera::get_settings(Data& output) const noexcept
{
    const auto* value = read_data<Data>();
    output = value ? *value : Data{};
    return value != nullptr;
}

bool CharacterCamera::set_settings(const schema::CharacterCameraDataT& value) noexcept
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
    runtime()->pitch_ = std::clamp(runtime()->pitch_, static_cast<double>(value.pitch_min),
                                   static_cast<double>(value.pitch_max));
    return true;
}

bool CharacterCamera::restore_data(const schema::ComponentDataT& value)
{
    if (!value.flags)
    {
        return false;
    }
    if (!writable())
    {
        return false;
    }
    const auto* settings = value.payload.AsCharacterCameraData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    runtime()->angles_initialized_ = false;
    if (!restore_flags(value))
    {
        return false;
    }
    return true;
}

bool CharacterCamera::remap_node_references(schema::ComponentDataT& data,
                                            std::span<const schema::NodeIdMappingT> mapping) const
{
    auto* settings = data.payload.AsCharacterCameraData();
    if (!settings)
    {
        return false;
    }
    if (settings->target_node_id == 0)
    {
        return true;
    }
    for (const auto& entry : mapping)
    {
        if (entry.source_id == settings->target_node_id)
        {
            if (entry.target_id == 0 || entry.retired)
            {
                return false;
            }
            settings->target_node_id = entry.target_id;
            return true;
        }
    }
    return false;
}

bool CharacterCamera::synchronize_angles() noexcept
{
    if (!writable())
    {
        return false;
    }
    if (runtime()->angles_initialized_)
    {
        return true;
    }
    auto transform = node().get_component<Transform>();
    if (!transform.valid())
    {
        return false;
    }
    const auto& rotation = transform.settings().rotation;
    return set_angles(rotation.y(), rotation.x());
}

bool CharacterCamera::set_angles(double yaw, double pitch) noexcept
{
    if (!writable())
    {
        return false;
    }
    if (!std::isfinite(yaw) || !std::isfinite(pitch))
    {
        return false;
    }
    runtime()->yaw_ = std::remainder(yaw, 360.0);
    runtime()->pitch_ = std::clamp(pitch, static_cast<double>(settings().pitch_min),
                                   static_cast<double>(settings().pitch_max));
    runtime()->angles_initialized_ = true;
    return true;
}

double CharacterCamera::yaw() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->yaw_;
}

double CharacterCamera::pitch() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->pitch_;
}

} // namespace engine::plugins::character_controller
