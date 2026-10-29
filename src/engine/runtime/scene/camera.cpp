#include "runtime/scene/camera.hpp"

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
        settings.far_clip <= settings.near_clip)
    {
        return false;
    }
    *mutable_data().payload.AsCameraData() = settings;
    return true;
}

} // namespace engine
