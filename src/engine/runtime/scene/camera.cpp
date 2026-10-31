#include "runtime/scene/camera.hpp"

#include <algorithm>
#include <cmath>

namespace engine
{

Camera::Camera(Node& node) : Component(node)
{
    mutable_data().payload.Set(CameraSettings{});
}

const CameraSettings& Camera::settings() const noexcept
{
    return *data().payload.AsCameraData();
}

bool Camera::set_settings(const CameraSettings& settings) noexcept
{
    if (!std::isfinite(settings.fov_degrees) || settings.fov_degrees <= 0.0F ||
        settings.fov_degrees >= 180.0F || !std::isfinite(settings.near_clip) ||
        settings.near_clip <= 0.0F || !std::isfinite(settings.far_clip) ||
        settings.far_clip <= settings.near_clip ||
        (settings.projection != schema::Projection::perspective &&
         settings.projection != schema::Projection::orthographic) ||
        !std::isfinite(settings.orthographic_size) || settings.orthographic_size <= 0.0F)
    {
        return false;
    }
    *mutable_data().payload.AsCameraData() = settings;
    return true;
}

bool Camera::set_hidden_meshes(std::span<const std::uint64_t> mesh_instance_ids)
{
    if (std::find(mesh_instance_ids.begin(), mesh_instance_ids.end(), 0) != mesh_instance_ids.end())
    {
        return false;
    }
    std::vector<std::uint64_t> candidate(mesh_instance_ids.begin(), mesh_instance_ids.end());
    std::sort(candidate.begin(), candidate.end());
    candidate.erase(std::unique(candidate.begin(), candidate.end()), candidate.end());
    hidden_meshes_.swap(candidate);
    return true;
}

void Camera::clear_hidden_meshes() noexcept
{
    hidden_meshes_.clear();
}

bool Camera::mesh_visible(std::uint64_t mesh_instance_id) const noexcept
{
    return mesh_instance_id != 0 &&
           !std::binary_search(hidden_meshes_.begin(), hidden_meshes_.end(), mesh_instance_id);
}

bool Camera::restore_data(const schema::ComponentDataT& data)
{
    const auto* settings = data.payload.AsCameraData();
    if (settings == nullptr || !set_settings(*settings))
    {
        return false;
    }
    set_enabled(data.enabled);
    clear_hidden_meshes();
    return true;
}

} // namespace engine
