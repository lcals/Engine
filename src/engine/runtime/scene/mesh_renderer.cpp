#include "runtime/scene/mesh_renderer.hpp"

#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/mesh_utils.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <string>
#include <utility>

namespace engine
{
namespace
{

std::size_t required_materials(const schema::MeshDataT& data) noexcept
{
    std::size_t count = 1;
    for (const auto& submesh : data.submeshes)
    {
        count = std::max(count, static_cast<std::size_t>(submesh.material_slot()) + 1);
    }
    return count;
}

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
    if (mesh_revision_ == std::numeric_limits<std::uint64_t>::max() || resources == nullptr ||
        !ResourceSystem::path_to_guid(path, guid) || !resources->load(guid, replacement) ||
        replacement->mesh() == nullptr || !MeshUtils::validate(*replacement->mesh()) ||
        !set_material_count(std::max(material_count(), required_materials(*replacement->mesh()))))
    {
        return false;
    }
    mutable_data().payload.AsMeshRendererData()->mesh_guid = std::move(guid);
    mesh_resource_ = std::move(replacement);
    dynamic_mesh_.reset();
    ++mesh_revision_;
    return true;
}

bool MeshRenderer::set_mesh_data(const schema::MeshDataT& data)
{
    if (mesh_revision_ == std::numeric_limits<std::uint64_t>::max() || !MeshUtils::validate(data))
    {
        return false;
    }
    auto replacement = std::make_unique<schema::MeshDataT>(data);
    if (!set_material_count(std::max(material_count(), required_materials(data))))
    {
        return false;
    }
    dynamic_mesh_ = std::move(replacement);
    mesh_resource_.reset();
    mutable_data().payload.AsMeshRendererData()->mesh_guid.clear();
    ++mesh_revision_;
    return true;
}

const schema::MeshDataT* MeshRenderer::mesh_data() const noexcept
{
    return dynamic_mesh_ ? dynamic_mesh_.get() : mesh_resource_ ? mesh_resource_->mesh() : nullptr;
}

std::uint64_t MeshRenderer::mesh_revision() const noexcept
{
    return mesh_revision_;
}

std::size_t MeshRenderer::material_count() const noexcept
{
    return 1 + additional_materials_.size();
}

Material* MeshRenderer::material(std::size_t slot) noexcept
{
    return const_cast<Material*>(std::as_const(*this).material(slot));
}

const Material* MeshRenderer::material(std::size_t slot) const noexcept
{
    return slot == 0                 ? &material_
           : slot < material_count() ? additional_materials_[slot - 1].get()
                                     : nullptr;
}

bool MeshRenderer::set_material_count(std::size_t count)
{
    if (count == 0 || count > 16 || (mesh_data() && count < required_materials(*mesh_data())))
    {
        return false;
    }
    std::vector<std::unique_ptr<Material>> replacements;
    while (material_count() + replacements.size() < count)
    {
        std::unique_ptr<Material> material(new (std::nothrow) Material());
        if (!material || !material->set_type("builtin/materials/default-pbr"))
        {
            return false;
        }
        replacements.push_back(std::move(material));
    }
    for (auto& material : replacements)
    {
        additional_materials_.push_back(std::move(material));
    }
    additional_materials_.resize(count - 1);
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
