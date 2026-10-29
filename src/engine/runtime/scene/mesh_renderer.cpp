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
    // 借用由本组件持有的网格资源，目录更新不会使这个字符串悬空。
    return mesh_resource_ ? std::string_view(mesh_resource_->info().path) : std::string_view{};
}

bool MeshRenderer::set_mesh(std::string_view path)
{
    auto* resources = ResourceSystem::instance();
    Guid guid;
    ResourcePtr replacement;
    if (resources == nullptr || !ResourceSystem::path_to_guid(path, guid) ||
        !resources->load(guid, replacement) || replacement->mesh() == nullptr)
    {
        return false;
    }
    mutable_data().payload.AsMeshRendererData()->mesh_guid = std::move(guid);
    mesh_resource_ = std::move(replacement);
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
