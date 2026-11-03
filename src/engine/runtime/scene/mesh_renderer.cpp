#include "runtime/scene/mesh_renderer.hpp"

#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/utils/mesh_utils.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <new>
#include <string>
#include <utility>

namespace engine
{
namespace
{

std::uint64_t next_mesh_instance_id() noexcept
{
    static std::atomic<std::uint64_t> sequence{0};
    auto current = sequence.load(std::memory_order_relaxed);
    while (current != std::numeric_limits<std::uint64_t>::max())
    {
        if (sequence.compare_exchange_weak(current, current + 1, std::memory_order_relaxed))
        {
            return current + 1;
        }
    }
    return 0;
}

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

bool skin_joint_count(const schema::MeshDataT& data, std::uint32_t& count) noexcept
{
    count = 0;
    if (!data.skin.empty() && data.skin.size() != data.vertices.size())
    {
        return false;
    }
    for (const auto& skin : data.skin)
    {
        const std::array<float, 4> weights = {skin.w0(), skin.w1(), skin.w2(), skin.w3()};
        const float sum = skin.w0() + skin.w1() + skin.w2() + skin.w3();
        if (!std::all_of(weights.begin(), weights.end(), [](float weight)
                         { return std::isfinite(weight) && weight >= 0.0F && weight <= 1.0F; }) ||
            !std::isfinite(sum) || std::abs(sum - 1.0F) > 0.0001F)
        {
            return false;
        }
        count = std::max(count, 1U + std::max({skin.j0(), skin.j1(), skin.j2(), skin.j3()}));
    }
    return true;
}

} // namespace

MeshRenderer::MeshRenderer(Node& node)
    : Component(node, make_mesh_renderer_data()), mesh_instance_id_(next_mesh_instance_id())
{
}

std::uint64_t MeshRenderer::mesh_instance_id() const noexcept
{
    return mesh_instance_id_;
}

bool MeshRenderer::visible() const noexcept
{
    return visible_;
}

void MeshRenderer::set_visible(bool visible) noexcept
{
    visible_ = visible;
}

bool MeshRenderer::cast_shadows() const noexcept
{
    return data().payload.AsMeshRendererData()->cast_shadows;
}

void MeshRenderer::set_cast_shadows(bool enabled) noexcept
{
    mutable_data().payload.AsMeshRendererData()->cast_shadows = enabled;
}

bool MeshRenderer::bake_static() const noexcept
{
    return data().payload.AsMeshRendererData()->bake_static;
}

bool MeshRenderer::set_bake_static(bool enabled) noexcept
{
    if (enabled &&
        (skin_joint_count_ != 0 || !deformed_vertices_.empty() || !skinning_palette_.empty()))
    {
        return false;
    }
    mutable_data().payload.AsMeshRendererData()->bake_static = enabled;
    return true;
}

std::shared_ptr<const Resource> MeshRenderer::lightmap_resource() const noexcept
{
    const auto& settings = *data().payload.AsMeshRendererData();
    const auto* mesh = mesh_data();
    const auto* transform = node().get_component<Transform>();
    std::array<float, 16> matrix{};
    if (!lightmap_resource_ || !mesh_resource_ || !mesh ||
        mesh->lightmap_texcoords.size() != mesh->vertices.size() ||
        settings.lightmap_mesh_hash != mesh_resource_->info().content_hash ||
        settings.lightmap_transform.size() != matrix.size() || skin_joint_count_ != 0 ||
        !deformed_vertices_.empty() || !skinning_palette_.empty() || !transform ||
        !transform->world_matrix(matrix.data()))
    {
        return nullptr;
    }
    for (std::size_t index = 0; index < matrix.size(); ++index)
    {
        if (std::abs(matrix[index] - settings.lightmap_transform[index]) > 0.00001F)
        {
            return nullptr;
        }
    }
    return lightmap_resource_;
}

std::string_view MeshRenderer::lightmap_path() const noexcept
{
    const auto resource = lightmap_resource();
    return resource ? std::string_view(resource->info().path) : std::string_view{};
}

bool MeshRenderer::set_lightmap(std::string_view path)
{
    auto& settings = *mutable_data().payload.AsMeshRendererData();
    if (path.empty())
    {
        lightmap_resource_.reset();
        settings.lightmap_guid.clear();
        settings.lightmap_mesh_hash.clear();
        settings.lightmap_transform.clear();
        return true;
    }
    const auto* mesh = mesh_data();
    const auto* transform = node().get_component<Transform>();
    auto* resources = ResourceSystem::instance();
    ResourcePtr replacement;
    Guid guid;
    std::array<float, 16> matrix{};
    if (!resources || !mesh_resource_ || !mesh || skin_joint_count_ != 0 ||
        !deformed_vertices_.empty() || !skinning_palette_.empty() ||
        mesh->lightmap_texcoords.size() != mesh->vertices.size() || !transform ||
        !transform->world_matrix(matrix.data()) || !ResourceSystem::path_to_guid(path, guid) ||
        !resources->loader().load(guid, replacement))
    {
        return false;
    }
    const auto* texture = replacement->texture();
    if (!texture || texture->cube_map || texture->srgb)
    {
        return false;
    }
    settings.lightmap_guid = std::move(guid);
    settings.lightmap_mesh_hash = mesh_resource_->info().content_hash;
    settings.lightmap_transform.assign(matrix.begin(), matrix.end());
    lightmap_resource_ = std::move(replacement);
    return true;
}

bool MeshRenderer::initialize()
{
    return mesh_instance_id_ != 0 && set_mesh("builtin://meshes/cube") &&
           material_.set_type("builtin://materials/default-pbr");
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
    std::uint32_t joint_count = 0;
    if (mesh_revision_ == std::numeric_limits<std::uint64_t>::max() || resources == nullptr ||
        !ResourceSystem::path_to_guid(path, guid) || !resources->loader().load(guid, replacement) ||
        replacement->mesh() == nullptr || !MeshUtils::validate(*replacement->mesh()) ||
        !skin_joint_count(*replacement->mesh(), joint_count) ||
        !set_material_count(std::max(material_count(), required_materials(*replacement->mesh()))))
    {
        return false;
    }
    mutable_data().payload.AsMeshRendererData()->mesh_guid = std::move(guid);
    mesh_resource_ = std::move(replacement);
    lightmap_resource_.reset();
    auto& settings = *mutable_data().payload.AsMeshRendererData();
    settings.lightmap_guid.clear();
    settings.lightmap_mesh_hash.clear();
    settings.lightmap_transform.clear();
    dynamic_mesh_.reset();
    skin_joint_count_ = joint_count;
    clear_deformation();
    ++mesh_revision_;
    return true;
}

bool MeshRenderer::set_mesh_data(const schema::MeshDataT& data)
{
    std::uint32_t joint_count = 0;
    if (mesh_revision_ == std::numeric_limits<std::uint64_t>::max() || !MeshUtils::validate(data) ||
        !skin_joint_count(data, joint_count))
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
    lightmap_resource_.reset();
    auto& settings = *mutable_data().payload.AsMeshRendererData();
    settings.lightmap_guid.clear();
    settings.lightmap_mesh_hash.clear();
    settings.lightmap_transform.clear();
    skin_joint_count_ = joint_count;
    mutable_data().payload.AsMeshRendererData()->mesh_guid.clear();
    clear_deformation();
    ++mesh_revision_;
    return true;
}

const schema::MeshDataT* MeshRenderer::mesh_data() const noexcept
{
    return dynamic_mesh_ ? dynamic_mesh_.get() : mesh_resource_ ? mesh_resource_->mesh() : nullptr;
}

std::shared_ptr<const Resource> MeshRenderer::mesh_resource() const noexcept
{
    return mesh_resource_;
}

std::uint64_t MeshRenderer::mesh_revision() const noexcept
{
    return mesh_revision_;
}

bool MeshRenderer::set_deformation(std::span<const schema::MeshVertex> vertices,
                                   std::span<const Vec4> tangents)
{
    const auto* source = mesh_data();
    if (source == nullptr || vertices.size() != source->vertices.size() || vertices.empty() ||
        tangents.size() != vertices.size() ||
        deformation_revision_ == std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }
    for (std::size_t index = 0; index < vertices.size(); ++index)
    {
        const auto& vertex = vertices[index];
        const auto& tangent = tangents[index];
        const auto valid = [](double value)
        { return std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max(); };
        if (!valid(vertex.x()) || !valid(vertex.y()) || !valid(vertex.z()) || !valid(vertex.nx()) ||
            !valid(vertex.ny()) || !valid(vertex.nz()) || !valid(tangent.x()) ||
            !valid(tangent.y()) || !valid(tangent.z()) ||
            (tangent.w() != -1.0 && tangent.w() != 1.0))
        {
            return false;
        }
        const double normal_length =
            std::hypot(static_cast<double>(vertex.nx()), static_cast<double>(vertex.ny()),
                       static_cast<double>(vertex.nz()));
        const double cross_length =
            std::hypot(vertex.ny() * tangent.z() - vertex.nz() * tangent.y(),
                       vertex.nz() * tangent.x() - vertex.nx() * tangent.z(),
                       vertex.nx() * tangent.y() - vertex.ny() * tangent.x());
        if (!(normal_length > 0.0) || !(cross_length > 0.0))
        {
            return false;
        }
    }
    std::vector<schema::MeshVertex> replacement_vertices(vertices.begin(), vertices.end());
    std::vector<Vec4> replacement_tangents(tangents.begin(), tangents.end());
    deformed_vertices_.swap(replacement_vertices);
    deformed_tangents_.swap(replacement_tangents);
    skinning_palette_.clear();
    skinning_normal_palette_.clear();
    ++deformation_revision_;
    return true;
}

void MeshRenderer::clear_deformation() noexcept
{
    if (deformed_vertices_.empty() && skinning_palette_.empty())
    {
        return;
    }
    deformed_vertices_.clear();
    deformed_tangents_.clear();
    skinning_palette_.clear();
    skinning_normal_palette_.clear();
    if (deformation_revision_ != std::numeric_limits<std::uint64_t>::max())
    {
        ++deformation_revision_;
    }
}

bool MeshRenderer::set_skinning_palette(std::span<const std::array<float, 16>> transforms,
                                        std::span<const std::array<float, 16>> normal_transforms)
{
    if (skin_joint_count_ == 0 || transforms.size() < skin_joint_count_ ||
        transforms.size() > 65536 || transforms.size() != normal_transforms.size() ||
        deformation_revision_ == std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }
    for (std::size_t index = 0; index < transforms.size(); ++index)
    {
        const auto& matrix = transforms[index];
        const auto& normal = normal_transforms[index];
        const auto finite = [](float value) { return std::isfinite(value); };
        if (!std::all_of(matrix.begin(), matrix.end(), finite) ||
            !std::all_of(normal.begin(), normal.end(), finite) || std::abs(matrix[3]) > 1.0e-5F ||
            std::abs(matrix[7]) > 1.0e-5F || std::abs(matrix[11]) > 1.0e-5F ||
            std::abs(matrix[15] - 1.0F) > 1.0e-5F)
        {
            return false;
        }
    }
    std::vector<std::array<float, 16>> positions(transforms.begin(), transforms.end());
    std::vector<std::array<float, 16>> normals(normal_transforms.begin(), normal_transforms.end());
    skinning_palette_.swap(positions);
    skinning_normal_palette_.swap(normals);
    deformed_vertices_.clear();
    deformed_tangents_.clear();
    ++deformation_revision_;
    return true;
}

std::span<const std::array<float, 16>> MeshRenderer::skinning_palette() const noexcept
{
    return skinning_palette_;
}

std::span<const std::array<float, 16>> MeshRenderer::skinning_normal_palette() const noexcept
{
    return skinning_normal_palette_;
}

std::span<const schema::MeshVertex> MeshRenderer::deformed_vertices() const noexcept
{
    return deformed_vertices_;
}

std::span<const Vec4> MeshRenderer::deformed_tangents() const noexcept
{
    return deformed_tangents_;
}

std::uint64_t MeshRenderer::deformation_revision() const noexcept
{
    return deformation_revision_;
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
        if (!material || !material->set_type("builtin://materials/default-pbr"))
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

bool MeshRenderer::build_data(schema::ComponentDataT& output) const
{
    output = {};
    schema::ComponentDataT snapshot;
    if (!Component::build_data(snapshot))
    {
        return false;
    }
    auto* settings = snapshot.payload.AsMeshRendererData();
    if (settings->mesh_guid.empty() ||
        !ResourceSystem::path_to_guid(material_.type_path(), settings->material_guid))
    {
        return false;
    }
    if (!lightmap_resource())
    {
        settings->lightmap_guid.clear();
        settings->lightmap_mesh_hash.clear();
        settings->lightmap_transform.clear();
    }
    settings->additional_material_guids.clear();
    for (const auto& material : additional_materials_)
    {
        std::string guid;
        if (!ResourceSystem::path_to_guid(material->type_path(), guid))
        {
            return false;
        }
        settings->additional_material_guids.push_back(std::move(guid));
    }
    output = std::move(snapshot);
    return true;
}

bool MeshRenderer::restore_data(const schema::ComponentDataT& data)
{
    const auto* settings = data.payload.AsMeshRendererData();
    if (!settings || mesh_revision_ == std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }
    MeshRenderer replacement(node());
    std::string mesh_path;
    std::string material_path;
    if (!ResourceSystem::guid_to_path(settings->mesh_guid, mesh_path) ||
        !ResourceSystem::guid_to_path(settings->material_guid, material_path) ||
        !replacement.set_mesh(mesh_path) || !replacement.material_.set_type(material_path) ||
        !replacement.set_material_count(1 + settings->additional_material_guids.size()))
    {
        return false;
    }
    for (std::size_t slot = 0; slot < settings->additional_material_guids.size(); ++slot)
    {
        if (!ResourceSystem::guid_to_path(settings->additional_material_guids[slot],
                                          material_path) ||
            !replacement.material(slot + 1)->set_type(material_path))
        {
            return false;
        }
    }
    if (!settings->lightmap_guid.empty())
    {
        std::string lightmap_path;
        if (!ResourceSystem::guid_to_path(settings->lightmap_guid, lightmap_path) ||
            !replacement.set_lightmap(lightmap_path) || settings->lightmap_mesh_hash.empty() ||
            settings->lightmap_transform.size() != 16 ||
            !std::all_of(settings->lightmap_transform.begin(), settings->lightmap_transform.end(),
                         [](float value) { return std::isfinite(value); }))
        {
            return false;
        }
    }
    else if (!settings->lightmap_mesh_hash.empty() || !settings->lightmap_transform.empty())
    {
        return false;
    }
    // 所有读取和校验成功后整体替换；旧资源由局部对象反序释放。
    mutable_data() = data;
    mesh_resource_.swap(replacement.mesh_resource_);
    lightmap_resource_.swap(replacement.lightmap_resource_);
    dynamic_mesh_.swap(replacement.dynamic_mesh_);
    std::swap(skin_joint_count_, replacement.skin_joint_count_);
    material_.swap(replacement.material_);
    additional_materials_.swap(replacement.additional_materials_);
    clear_deformation();
    ++mesh_revision_;
    return true;
}

} // namespace engine
