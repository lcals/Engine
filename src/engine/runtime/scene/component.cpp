#include "runtime/scene/component.hpp"

#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"

#include <cmath>
#include <memory>
#include <utility>

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
    return unit_value(color.r()) && unit_value(color.g()) && unit_value(color.b()) &&
           unit_value(color.a());
}

schema::ComponentDataT make_mesh_renderer_data()
{
    schema::MeshRendererDataT settings;
    schema::ComponentDataT data;
    data.payload.Set(std::move(settings));
    return data;
}

} // namespace

Component::Component(Node& node, schema::ComponentDataT data) noexcept
    : node_(node), data_(std::move(data))
{
}

Component::~Component() noexcept = default;

bool Component::initialize()
{
    return true;
}

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
    return data_.payload.type;
}

bool Component::enabled() const noexcept
{
    return data_.enabled;
}

void Component::set_enabled(bool enabled) noexcept
{
    data_.enabled = enabled;
}

const schema::ComponentDataT& Component::data() const noexcept
{
    return data_;
}

schema::ComponentDataT& Component::mutable_data() noexcept
{
    return data_;
}

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

Light::Light(Node& node) : Component(node)
{
    mutable_data().payload.Set(LightSettings{});
}

const LightSettings& Light::settings() const noexcept
{
    return *data().payload.AsLightData();
}

bool Light::set_settings(const LightSettings& settings) noexcept
{
    if (!valid_color(settings.color) || !std::isfinite(settings.intensity) ||
        settings.intensity < 0.0F)
    {
        return false;
    }
    *mutable_data().payload.AsLightData() = settings;
    return true;
}

MeshRenderer::MeshRenderer(Node& node) : Component(node, make_mesh_renderer_data())
{
}

bool MeshRenderer::initialize()
{
    return set_mesh("builtin/meshes/cube") && material_.set_type("builtin/materials/default-pbr");
}

std::string_view MeshRenderer::mesh_path() const noexcept
{
    // 路径由注册资源拥有，返回生命周期受 ResourceSystem 管理的借用。
    std::string path;
    if (!ResourceSystem::guid_to_path(data().payload.AsMeshRendererData()->mesh_guid, path))
    {
        return {};
    }
    const auto* mesh = ResourceSystem::find_mesh(path);
    return mesh == nullptr ? std::string_view{} : mesh->path;
}

bool MeshRenderer::set_mesh(std::string_view path)
{
    Guid guid;
    if (ResourceSystem::find_mesh(path) == nullptr || !ResourceSystem::path_to_guid(path, guid))
    {
        return false;
    }
    mutable_data().payload.AsMeshRendererData()->mesh_guid = std::move(guid);
    return true;
}

Material& MeshRenderer::material() noexcept
{
    return material_;
}

const Material& MeshRenderer::material() const noexcept
{
    return material_;
}

} // namespace engine
