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
