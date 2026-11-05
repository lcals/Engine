#include "runtime/scene/mesh_renderer.hpp"

#include "runtime/scene/material_state.hpp"
#include "runtime/scene/mesh_renderer_runtime.hpp"

#include "runtime/scene/ecs_storage.hpp"

#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/node.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/utils/mesh_utils.hpp"

#include <algorithm>
#include <cmath>
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

bool MeshRenderer::register_type(ComponentRegistry& registry)
{
    return register_component_storage<MeshRenderer, Data, Runtime>(registry);
}

MeshRenderer::Runtime* MeshRenderer::runtime() const noexcept
{
    return runtime_state<Runtime>();
}

bool MeshRenderer::cast_shadows() const noexcept
{
    if (!valid())
    {
        return false;
    }
    return read_data<Data>()->cast_shadows;
}

void MeshRenderer::set_cast_shadows(bool enabled) noexcept
{
    if (!writable())
    {
        return;
    }
    write_data<Data>()->cast_shadows = enabled;
    modified();
}

bool MeshRenderer::static_instancing_enabled() const noexcept
{
    const auto* data = read_data<Data>();
    return data && data->static_instancing_enabled;
}

bool MeshRenderer::set_static_instancing_enabled(bool enabled) noexcept
{
    if (!writable())
    {
        return false;
    }
    if (write_data<Data>()->static_instancing_enabled != enabled)
    {
        write_data<Data>()->static_instancing_enabled = enabled;
        modified();
    }
    return true;
}

bool MeshRenderer::static_lod_enabled() const noexcept
{
    const auto* data = read_data<Data>();
    return data && data->static_lod_enabled;
}

bool MeshRenderer::set_static_lod_enabled(bool enabled) noexcept
{
    if (!writable())
    {
        return false;
    }
    if (write_data<Data>()->static_lod_enabled != enabled)
    {
        write_data<Data>()->static_lod_enabled = enabled;
        modified();
    }
    return true;
}

bool MeshRenderer::bake_static() const noexcept
{
    if (!valid())
    {
        return false;
    }
    return read_data<Data>()->bake_static;
}

bool MeshRenderer::set_bake_static(bool enabled) noexcept
{
    if (!writable())
    {
        return false;
    }
    if (enabled && (runtime()->skin_joint_count_ != 0 || !runtime()->deformed_vertices_.empty() ||
                    !runtime()->skinning_palette_.empty()))
    {
        return false;
    }
    write_data<Data>()->bake_static = enabled;
    modified();
    return true;
}

std::shared_ptr<const Resource> MeshRenderer::lightmap_resource() const noexcept
{
    if (!valid())
    {
        return {};
    }
    const auto& settings = *read_data<Data>();
    const auto* mesh = mesh_data();
    auto transform = node().get_component<Transform>();
    std::array<float, 16> matrix{};
    if (!runtime()->lightmap_resource_ || !runtime()->mesh_resource_ || !mesh ||
        mesh->lightmap_texcoords.size() != mesh->vertices.size() ||
        settings.lightmap_mesh_hash != runtime()->mesh_resource_->info().content_hash ||
        settings.lightmap_transform.size() != matrix.size() || runtime()->skin_joint_count_ != 0 ||
        !runtime()->deformed_vertices_.empty() || !runtime()->skinning_palette_.empty() ||
        !transform || !transform.world_matrix(matrix.data()))
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
    return runtime()->lightmap_resource_;
}

std::string_view MeshRenderer::lightmap_path() const noexcept
{
    if (!valid())
    {
        return {};
    }
    const auto resource = lightmap_resource();
    return resource ? std::string_view(resource->info().path) : std::string_view{};
}

bool MeshRenderer::set_lightmap(std::string_view path)
{
    if (!writable())
    {
        return false;
    }
    auto& settings = *write_data<Data>();
    if (path.empty())
    {
        runtime()->lightmap_resource_.reset();
        settings.lightmap_guid.clear();
        settings.lightmap_mesh_hash.clear();
        settings.lightmap_transform.clear();
        modified();
        return true;
    }
    const auto* mesh = mesh_data();
    auto transform = node().get_component<Transform>();
    auto* resources = ResourceSystem::instance();
    ResourcePtr replacement;
    Guid guid;
    std::array<float, 16> matrix{};
    if (!resources || !runtime()->mesh_resource_ || !mesh || runtime()->skin_joint_count_ != 0 ||
        !runtime()->deformed_vertices_.empty() || !runtime()->skinning_palette_.empty() ||
        mesh->lightmap_texcoords.size() != mesh->vertices.size() || !transform ||
        !transform.world_matrix(matrix.data()) || !ResourceSystem::path_to_guid(path, guid) ||
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
    settings.lightmap_mesh_hash = runtime()->mesh_resource_->info().content_hash;
    settings.lightmap_transform.assign(matrix.begin(), matrix.end());
    runtime()->lightmap_resource_ = std::move(replacement);
    modified();
    return true;
}

bool MeshRenderer::initialize()
{
    if (!writable())
    {
        return false;
    }
    return set_mesh("builtin://meshes/cube") &&
           material().set_type("builtin://materials/default-pbr");
}

std::string_view MeshRenderer::mesh_path() const noexcept
{
    if (!valid())
    {
        return {};
    }
    // 借用由本组件持有的网格资源，目录更新不会使这个字符串悬空。
    return runtime()->mesh_resource_ ? std::string_view(runtime()->mesh_resource_->info().path)
                                     : std::string_view{};
}

bool MeshRenderer::set_mesh(std::string_view path)
{
    if (!writable())
    {
        return false;
    }
    auto* resources = ResourceSystem::instance();
    Guid guid;
    ResourcePtr replacement;
    std::uint32_t joint_count = 0;
    if (runtime()->mesh_revision_ == std::numeric_limits<std::uint64_t>::max() ||
        resources == nullptr || !ResourceSystem::path_to_guid(path, guid) ||
        !resources->loader().load(guid, replacement) || replacement->mesh() == nullptr ||
        !MeshUtils::validate(*replacement->mesh()) ||
        !skin_joint_count(*replacement->mesh(), joint_count) ||
        !set_material_count(std::max(material_count(), required_materials(*replacement->mesh()))))
    {
        return false;
    }
    write_data<Data>()->mesh_guid = std::move(guid);
    runtime()->mesh_resource_ = std::move(replacement);
    runtime()->lightmap_resource_.reset();
    auto& settings = *write_data<Data>();
    settings.lightmap_guid.clear();
    settings.lightmap_mesh_hash.clear();
    settings.lightmap_transform.clear();
    runtime()->dynamic_mesh_.reset();
    runtime()->skin_joint_count_ = joint_count;
    clear_deformation();
    ++runtime()->mesh_revision_;
    modified();
    return true;
}

bool MeshRenderer::set_mesh_data(const schema::MeshDataT& data)
{
    if (!writable())
    {
        return false;
    }
    std::uint32_t joint_count = 0;
    if (runtime()->mesh_revision_ == std::numeric_limits<std::uint64_t>::max() ||
        !MeshUtils::validate(data) || !skin_joint_count(data, joint_count))
    {
        return false;
    }
    auto replacement = std::make_unique<schema::MeshDataT>(data);
    if (!set_material_count(std::max(material_count(), required_materials(data))))
    {
        return false;
    }
    runtime()->dynamic_mesh_ = std::move(replacement);
    runtime()->mesh_resource_.reset();
    runtime()->lightmap_resource_.reset();
    auto& settings = *write_data<Data>();
    settings.lightmap_guid.clear();
    settings.lightmap_mesh_hash.clear();
    settings.lightmap_transform.clear();
    runtime()->skin_joint_count_ = joint_count;
    write_data<Data>()->mesh_guid.clear();
    clear_deformation();
    ++runtime()->mesh_revision_;
    modified();
    return true;
}

const schema::MeshDataT* MeshRenderer::mesh_data() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->dynamic_mesh_    ? runtime()->dynamic_mesh_.get()
           : runtime()->mesh_resource_ ? runtime()->mesh_resource_->mesh()
                                       : nullptr;
}

std::shared_ptr<const Resource> MeshRenderer::mesh_resource() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->mesh_resource_;
}

std::uint64_t MeshRenderer::mesh_revision() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->mesh_revision_;
}

bool MeshRenderer::set_deformation(std::span<const schema::MeshVertex> vertices,
                                   std::span<const Vec4> tangents)
{
    if (!writable())
    {
        return false;
    }
    const auto* source = mesh_data();
    if (source == nullptr || vertices.size() != source->vertices.size() || vertices.empty() ||
        tangents.size() != vertices.size() ||
        runtime()->deformation_revision_ == std::numeric_limits<std::uint64_t>::max())
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
    runtime()->deformed_vertices_.swap(replacement_vertices);
    runtime()->deformed_tangents_.swap(replacement_tangents);
    runtime()->skinning_palette_.clear();
    runtime()->skinning_normal_palette_.clear();
    ++runtime()->deformation_revision_;
    return true;
}

void MeshRenderer::clear_deformation() noexcept
{
    if (!writable())
    {
        return;
    }
    if (runtime()->deformed_vertices_.empty() && runtime()->skinning_palette_.empty())
    {
        return;
    }
    runtime()->deformed_vertices_.clear();
    runtime()->deformed_tangents_.clear();
    runtime()->skinning_palette_.clear();
    runtime()->skinning_normal_palette_.clear();
    if (runtime()->deformation_revision_ != std::numeric_limits<std::uint64_t>::max())
    {
        ++runtime()->deformation_revision_;
    }
}

bool MeshRenderer::set_skinning_palette(std::span<const std::array<float, 16>> transforms,
                                        std::span<const std::array<float, 16>> normal_transforms)
{
    if (!writable())
    {
        return false;
    }
    if (runtime()->skin_joint_count_ == 0 || transforms.size() < runtime()->skin_joint_count_ ||
        transforms.size() > 65536 || transforms.size() != normal_transforms.size() ||
        runtime()->deformation_revision_ == std::numeric_limits<std::uint64_t>::max())
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
    runtime()->skinning_palette_.swap(positions);
    runtime()->skinning_normal_palette_.swap(normals);
    runtime()->deformed_vertices_.clear();
    runtime()->deformed_tangents_.clear();
    ++runtime()->deformation_revision_;
    return true;
}

std::span<const std::array<float, 16>> MeshRenderer::skinning_palette() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->skinning_palette_;
}

std::span<const std::array<float, 16>> MeshRenderer::skinning_normal_palette() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->skinning_normal_palette_;
}

std::span<const schema::MeshVertex> MeshRenderer::deformed_vertices() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->deformed_vertices_;
}

std::span<const Vec4> MeshRenderer::deformed_tangents() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->deformed_tangents_;
}

std::uint64_t MeshRenderer::deformation_revision() const noexcept
{
    if (!valid())
    {
        return {};
    }
    return runtime()->deformation_revision_;
}

std::size_t MeshRenderer::material_count() const noexcept
{
    const auto* state = runtime();
    return state ? state->materials_.size() : 0;
}

Material MeshRenderer::material(std::size_t slot) const noexcept
{
    const auto* state = runtime();
    if (!state || slot >= state->materials_.size())
    {
        return {};
    }
    return Material(*this, slot, state->materials_[slot].identity);
}

bool MeshRenderer::set_material_count(std::size_t count)
{
    auto* state = runtime();
    if (!writable() || !state || count == 0 || count > 16 ||
        (mesh_data() && count < required_materials(*mesh_data())))
    {
        return false;
    }
    std::vector<MaterialState> replacements;
    auto next_identity = state->next_material_identity_;
    while (state->materials_.size() + replacements.size() < count)
    {
        if (next_identity == std::numeric_limits<std::uint64_t>::max())
        {
            return false;
        }
        MaterialState value;
        if (!value.set_type("builtin://materials/default-pbr"))
        {
            return false;
        }
        value.identity = next_identity++;
        replacements.push_back(std::move(value));
    }
    for (auto& value : replacements)
    {
        state->materials_.push_back(std::move(value));
    }
    state->materials_.resize(count);
    state->next_material_identity_ = next_identity;
    modified();
    return true;
}

bool MeshRenderer::build_data(schema::ComponentDataT& output) const
{
    output = {};
    schema::ComponentDataT snapshot;
    if (!build_storage_data(snapshot))
    {
        return false;
    }
    auto* settings = snapshot.payload.AsMeshRendererData();
    if (settings->mesh_guid.empty() ||
        !ResourceSystem::path_to_guid(material().type_path(), settings->material_guid))
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
    for (std::size_t slot = 1; slot < material_count(); ++slot)
    {
        std::string guid;
        if (!ResourceSystem::path_to_guid(material(slot).type_path(), guid))
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
    auto* current = runtime();
    auto* resources = ResourceSystem::instance();
    if (!writable() || !data.flags || !settings || !current || !resources ||
        current->mesh_revision_ == std::numeric_limits<std::uint64_t>::max() ||
        settings->additional_material_guids.size() > 15)
    {
        return false;
    }
    Runtime replacement;
    if (!resources->loader().load(settings->mesh_guid, replacement.mesh_resource_) ||
        !replacement.mesh_resource_ || !replacement.mesh_resource_->mesh() ||
        !MeshUtils::validate(*replacement.mesh_resource_->mesh()) ||
        !skin_joint_count(*replacement.mesh_resource_->mesh(), replacement.skin_joint_count_) ||
        required_materials(*replacement.mesh_resource_->mesh()) >
            1 + settings->additional_material_guids.size())
    {
        return false;
    }
    replacement.next_material_identity_ = current->next_material_identity_;
    for (std::size_t slot = 0; slot <= settings->additional_material_guids.size(); ++slot)
    {
        if (replacement.next_material_identity_ == std::numeric_limits<std::uint64_t>::max())
        {
            return false;
        }
        const auto& guid =
            slot == 0 ? settings->material_guid : settings->additional_material_guids[slot - 1];
        std::string path;
        MaterialState value;
        if (!ResourceSystem::guid_to_path(guid, path) || !value.set_type(path))
        {
            return false;
        }
        value.identity = replacement.next_material_identity_++;
        replacement.materials_.push_back(std::move(value));
    }
    if (!settings->lightmap_guid.empty())
    {
        const auto* mesh = replacement.mesh_resource_->mesh();
        if (!resources->loader().load(settings->lightmap_guid, replacement.lightmap_resource_) ||
            !replacement.lightmap_resource_ || !replacement.lightmap_resource_->texture() ||
            replacement.lightmap_resource_->texture()->cube_map ||
            replacement.lightmap_resource_->texture()->srgb || replacement.skin_joint_count_ != 0 ||
            mesh->lightmap_texcoords.size() != mesh->vertices.size() ||
            settings->lightmap_mesh_hash.empty() || settings->lightmap_transform.size() != 16 ||
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
    replacement.mesh_revision_ = current->mesh_revision_ + 1;
    replacement.deformation_revision_ = current->deformation_revision_;
    if ((!current->deformed_vertices_.empty() || !current->skinning_palette_.empty()) &&
        replacement.deformation_revision_ != std::numeric_limits<std::uint64_t>::max())
    {
        ++replacement.deformation_revision_;
    }
    *write_data<Data>() = *settings;
    *runtime() = std::move(replacement);
    modified();
    return restore_flags(data);
}

} // namespace engine
