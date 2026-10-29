#include "runtime/scene/component.hpp"

#include <cmath>

namespace engine
{
namespace
{

bool unit_value(float value)
{
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}

bool valid_color(const Color& color)
{
    return unit_value(color.r) && unit_value(color.g) && unit_value(color.b) && unit_value(color.a);
}

} // namespace

Component::Component(Node& node, ComponentType type) noexcept : node_(node), type_(type)
{
}

Component::~Component() noexcept = default;

Node& Component::node() noexcept
{
    return node_;
}

const Node& Component::node() const noexcept
{
    return node_;
}

ComponentType Component::type() const noexcept
{
    return type_;
}

bool Component::enabled() const noexcept
{
    return enabled_;
}

void Component::set_enabled(bool enabled) noexcept
{
    enabled_ = enabled;
}

Camera::Camera(Node& node) noexcept : Component(node, ComponentType::camera)
{
}

const CameraSettings& Camera::settings() const noexcept
{
    return settings_;
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
    settings_ = settings;
    return true;
}

Light::Light(Node& node) noexcept : Component(node, ComponentType::light)
{
}

const LightSettings& Light::settings() const noexcept
{
    return settings_;
}

bool Light::set_settings(const LightSettings& settings) noexcept
{
    if (!valid_color(settings.color) || !std::isfinite(settings.intensity) ||
        settings.intensity < 0.0F)
    {
        return false;
    }
    settings_ = settings;
    return true;
}

MeshRenderer::MeshRenderer(Node& node) noexcept : Component(node, ComponentType::mesh_renderer)
{
}

MeshKind MeshRenderer::mesh() const noexcept
{
    return MeshKind::cube;
}

const Material& MeshRenderer::material() const noexcept
{
    return material_;
}

bool MeshRenderer::set_material(const Material& material) noexcept
{
    if (!valid_color(material.base_color) || !unit_value(material.metallic) ||
        !unit_value(material.roughness))
    {
        return false;
    }
    material_ = material;
    return true;
}

} // namespace engine
