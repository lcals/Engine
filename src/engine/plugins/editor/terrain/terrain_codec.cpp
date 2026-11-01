#include "plugins/editor/terrain/terrain_codec.hpp"

#include "editor/asset_pipeline/importer_utils.hpp"
#include "editor/asset_pipeline/toml_helpers.hpp"
#include "plugins/runtime/terrain/terrain_component.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <utility>

namespace engine::plugins::terrain
{

schema::ComponentPayload TerrainCodec::type() const noexcept
{
    return schema::ComponentPayload::TerrainData;
}

std::string_view TerrainCodec::name() const noexcept
{
    return "terrain";
}

bool TerrainCodec::validate(const schema::ComponentDataT& component, std::string& error) const
{
    const auto* value = component.payload.AsTerrainData();
    if (value == nullptr || !TerrainComponent::valid_settings(*value))
    {
        error = "invalid terrain component resource GUIDs, colors or UV scale";
        return false;
    }
    return true;
}

bool TerrainCodec::read(const toml::table& table, schema::ComponentDataT& component,
                        std::string& error) const
{
    constexpr std::array<std::string_view, 8> fields = {"type",       "enabled",   "terrain",
                                                        "material",   "collision", "surface_color",
                                                        "wall_color", "uv_scale"};
    for (const auto& [name, field] : table)
    {
        static_cast<void>(field);
        if (std::find(fields.begin(), fields.end(), name.str()) == fields.end())
        {
            error = "unknown terrain component field: " + std::string(name.str());
            return false;
        }
    }
    schema::TerrainDataT value;
    for (const auto& [name, destination] :
         {std::pair{"terrain", &value.terrain_guid}, std::pair{"material", &value.material_guid}})
    {
        if (table.contains(name))
        {
            const auto guid = table[name].value<std::string>();
            if (!guid)
            {
                error = std::string(name) + " must be a resource GUID string";
                return false;
            }
            *destination = *guid;
        }
    }
    if (table.contains("collision"))
    {
        const auto collision = table["collision"].value_exact<bool>();
        if (!collision)
        {
            error = "terrain collision must be boolean";
            return false;
        }
        value.collision = *collision;
    }
    if (table.contains("uv_scale") &&
        !editor::asset_pipeline::float_number(table.get("uv_scale"), value.uv_scale))
    {
        error = "terrain uv_scale must be a finite float";
        return false;
    }
    for (const auto& [name, destination] : {std::pair{"surface_color", &value.surface_color},
                                            std::pair{"wall_color", &value.wall_color}})
    {
        if (table.contains(name))
        {
            std::array<double, 4> values{};
            if (!editor::asset_pipeline::numbers(table.get(name), values) ||
                !std::all_of(values.begin(), values.end(),
                             [](double number) { return number >= 0 && number <= 1; }))
            {
                error = std::string("terrain ") + name + " requires four values in [0,1]";
                return false;
            }
            *destination =
                schema::Color(static_cast<float>(values[0]), static_cast<float>(values[1]),
                              static_cast<float>(values[2]), static_cast<float>(values[3]));
        }
    }
    if (!TerrainComponent::valid_settings(value))
    {
        error = "invalid terrain GUIDs, colors or UV scale";
        return false;
    }
    component.payload.Set(std::move(value));
    return true;
}

bool TerrainCodec::scan_dependencies(const schema::ComponentDataT& component,
                                     const editor::asset_pipeline::ImportContext& context,
                                     std::vector<Guid>& dependencies, std::string& error) const
{
    if (!validate(component, error))
    {
        return false;
    }
    const auto& value = *component.payload.AsTerrainData();
    return (value.terrain_guid.empty() ||
            editor::asset_pipeline::reference(value.terrain_guid, schema::AssetKind::terrain,
                                              context, dependencies, error)) &&
           (value.material_guid.empty() ||
            editor::asset_pipeline::reference(value.material_guid, schema::AssetKind::material,
                                              context, dependencies, error));
}

void TerrainCodec::write(const schema::ComponentDataT& component, toml::table& table) const
{
    const auto& value = *component.payload.AsTerrainData();
    table.insert("type", "terrain");
    table.insert("terrain", value.terrain_guid);
    table.insert("material", value.material_guid);
    table.insert("collision", value.collision);
    table.insert("surface_color", toml::array{value.surface_color.r(), value.surface_color.g(),
                                              value.surface_color.b(), value.surface_color.a()});
    table.insert("wall_color", toml::array{value.wall_color.r(), value.wall_color.g(),
                                           value.wall_color.b(), value.wall_color.a()});
    table.insert("uv_scale", value.uv_scale);
}

bool register_terrain_codec()
{
    if (editor::asset_pipeline::find_scene_component_codec(schema::ComponentPayload::TerrainData))
    {
        return true;
    }
    std::unique_ptr<editor::asset_pipeline::SceneComponentCodec> codec =
        std::make_unique<TerrainCodec>();
    return editor::asset_pipeline::register_scene_component_codec(codec);
}

} // namespace engine::plugins::terrain
