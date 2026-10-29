#include "runtime/scene/mesh_renderer.hpp"

#include "runtime/resource/resource_system.hpp"

#include <string>
#include <utility>

namespace engine
{
namespace
{

schema::ComponentDataT make_mesh_renderer_data()
{
    schema::MeshRendererDataT settings;
    schema::ComponentDataT data;
    data.payload.Set(std::move(settings));
    return data;
}

} // namespace

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
