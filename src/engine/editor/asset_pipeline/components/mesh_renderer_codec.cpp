#include "editor/asset_pipeline/components/mesh_renderer_codec.hpp"

#include "runtime/core/guid_map.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace engine::editor::asset_pipeline
{

schema::ComponentPayload MeshRendererCodec::type() const noexcept
{
    return schema::ComponentPayload::MeshRendererData;
}

std::string_view MeshRendererCodec::name() const noexcept
{
    return "mesh_renderer";
}

bool MeshRendererCodec::validate(const schema::ComponentDataT& component, std::string& error) const
{

    const auto* value = component.payload.AsMeshRendererData();
    if (value == nullptr || !GuidMap::valid_guid(value->mesh_guid) ||
        !GuidMap::valid_guid(value->material_guid) ||
        value->additional_material_guids.size() > 15 ||
        std::any_of(value->additional_material_guids.begin(),
                    value->additional_material_guids.end(),
                    [](const auto& guid) { return !GuidMap::valid_guid(guid); }))
    {
        error = "mesh renderer requires mesh and material GUIDs";
        return false;
    }

    if (value->lightmap_guid.empty()
            ? (!value->lightmap_mesh_hash.empty() || !value->lightmap_transform.empty())
            : (!GuidMap::valid_guid(value->lightmap_guid) || value->lightmap_mesh_hash.empty() ||
               value->lightmap_transform.size() != 16 ||
               !std::all_of(value->lightmap_transform.begin(), value->lightmap_transform.end(),
                            [](float element) { return std::isfinite(element); })))
    {
        error = "mesh renderer lightmap requires GUID, mesh content hash and baked world transform";
        return false;
    }
    return true;
}

bool MeshRendererCodec::read(const toml::table& table, schema::ComponentDataT& component,
                             std::string& error) const
{

    schema::MeshRendererDataT value;
    value.mesh_guid = table["mesh"].value_or(std::string{});
    value.material_guid = table["material"].value_or(std::string{});
    if (table.contains("cast_shadows") && !table["cast_shadows"].is_boolean())
    {
        error = "mesh renderer cast_shadows must be boolean";
        return false;
    }
    value.cast_shadows = table["cast_shadows"].value_or(true);
    if (table.contains("bake_static") && !table["bake_static"].is_boolean())
    {
        error = "mesh renderer bake_static must be boolean";
        return false;
    }
    value.bake_static = table["bake_static"].value_or(false);
    for (const auto* field : {"lightmap", "lightmap_mesh_hash"})
    {
        if (table.contains(field) && !table[field].is_string())
        {
            error = std::string("mesh renderer ") + field + " must be a string";
            return false;
        }
    }
    value.lightmap_guid = table["lightmap"].value_or(std::string{});
    value.lightmap_mesh_hash = table["lightmap_mesh_hash"].value_or(std::string{});
    if (table.contains("lightmap_transform"))
    {
        std::array<double, 16> matrix{};
        if (!numbers(table.get("lightmap_transform"), matrix))
        {
            error = "mesh renderer lightmap_transform requires 16 finite floats";
            return false;
        }
        for (double element : matrix)
        {
            if (std::abs(element) > std::numeric_limits<float>::max())
            {
                error = "mesh renderer lightmap_transform exceeds float range";
                return false;
            }
            value.lightmap_transform.push_back(static_cast<float>(element));
        }
    }
    if (table.contains("additional_materials"))
    {
        const auto* materials = table["additional_materials"].as_array();
        if (materials == nullptr)
        {
            error = "mesh renderer additional_materials must be a GUID array";
            return false;
        }
        for (const auto& item : *materials)
        {
            const auto guid = item.value<std::string>();
            if (!guid)
            {
                error = "mesh renderer additional_materials must contain GUID strings";
                return false;
            }
            value.additional_material_guids.push_back(*guid);
        }
    }
    component.payload.Set(std::move(value));

    return true;
}

void MeshRendererCodec::write(const schema::ComponentDataT& component, toml::table& value) const
{

    const auto* mesh = component.payload.AsMeshRendererData();
    value.insert("type", "mesh_renderer");
    value.insert("mesh", mesh->mesh_guid);
    value.insert("material", mesh->material_guid);
    value.insert("cast_shadows", mesh->cast_shadows);
    value.insert("bake_static", mesh->bake_static);
    if (!mesh->lightmap_guid.empty())
    {
        value.insert("lightmap", mesh->lightmap_guid);
        value.insert("lightmap_mesh_hash", mesh->lightmap_mesh_hash);
        toml::array matrix;
        for (float element : mesh->lightmap_transform)
        {
            matrix.push_back(element);
        }
        value.insert("lightmap_transform", std::move(matrix));
    }
    if (!mesh->additional_material_guids.empty())
    {
        toml::array materials;
        for (const auto& guid : mesh->additional_material_guids)
        {
            materials.push_back(guid);
        }
        value.insert("additional_materials", std::move(materials));
    }
}

} // namespace engine::editor::asset_pipeline
