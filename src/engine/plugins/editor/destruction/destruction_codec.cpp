#include "plugins/editor/destruction/destruction_codec.hpp"

#include "runtime/resource/builtin_resources.hpp"

#include "editor/asset_pipeline/importer_utils.hpp"
#include "editor/asset_pipeline/toml_helpers.hpp"
#include "plugins/runtime/destruction/destruction_component.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <utility>

namespace engine::plugins::destruction
{

schema::ComponentPayload DestructionCodec::type() const noexcept
{
    return schema::ComponentPayload::DestructionData;
}

std::string_view DestructionCodec::name() const noexcept
{
    return "destruction";
}

bool DestructionCodec::validate(const schema::ComponentDataT& component, std::string& error) const
{
    const auto* value = component.payload.AsDestructionData();
    if (!value || !DestructionComponent::valid_settings(*value))
    {
        error = "Invalid destruction resource GUIDs or physical settings";
        return false;
    }
    return true;
}

bool DestructionCodec::read(const toml::table& table, schema::ComponentDataT& component,
                            std::string& error) const
{
    constexpr std::array<std::string_view, 7> fields = {
        "type", "enabled", "destruction", "material", "density", "friction", "restitution"};
    for (const auto& [name, field] : table)
    {
        static_cast<void>(field);
        if (std::find(fields.begin(), fields.end(), name.str()) == fields.end())
        {
            error = "Unknown destruction component field: " + std::string(name.str());
            return false;
        }
    }
    schema::DestructionDataT value;
    for (const auto& [name, destination] : {std::pair{"destruction", &value.destruction_guid},
                                            std::pair{"material", &value.material_guid}})
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
    for (const auto& [name, destination] :
         {std::pair{"density", &value.density}, std::pair{"friction", &value.friction},
          std::pair{"restitution", &value.restitution}})
    {
        if (table.contains(name) &&
            !editor::asset_pipeline::float_number(table.get(name), *destination))
        {
            error = std::string("Destruction ") + name + " must be a finite float";
            return false;
        }
    }
    if (!DestructionComponent::valid_settings(value))
    {
        error = "Invalid destruction GUIDs, density, friction or restitution";
        return false;
    }
    if (!value.destruction_guid.empty() && value.material_guid.empty())
    {
        for (const auto& entry : builtin_resource_catalog().entries)
        {
            if (entry->path == "builtin://materials/default-pbr")
            {
                value.material_guid = entry->guid;
                break;
            }
        }
        if (value.material_guid.empty())
        {
            error = "Default material is unavailable";
            return false;
        }
    }
    component.payload.Set(std::move(value));
    return true;
}

bool DestructionCodec::scan_dependencies(const schema::ComponentDataT& component,
                                         const editor::asset_pipeline::ImportContext& context,
                                         std::vector<Guid>& dependencies, std::string& error) const
{
    if (!validate(component, error))
    {
        return false;
    }
    const auto& value = *component.payload.AsDestructionData();
    return (value.destruction_guid.empty() ||
            editor::asset_pipeline::reference(value.destruction_guid,
                                              schema::AssetKind::destruction, context, dependencies,
                                              error)) &&
           (value.material_guid.empty() ||
            editor::asset_pipeline::reference(value.material_guid, schema::AssetKind::material,
                                              context, dependencies, error));
}

std::span<const std::string_view> DestructionCodec::resource_fields() const noexcept
{
    static constexpr std::string_view fields[]{"destruction", "material"};
    return fields;
}

void DestructionCodec::write(const schema::ComponentDataT& component, toml::table& table) const
{
    const auto& value = *component.payload.AsDestructionData();
    table.insert("type", "destruction");
    table.insert("destruction", value.destruction_guid);
    table.insert("material", value.material_guid);
    table.insert("density", value.density);
    table.insert("friction", value.friction);
    table.insert("restitution", value.restitution);
}

bool register_destruction_codec()
{
    if (editor::asset_pipeline::find_scene_component_codec(
            schema::ComponentPayload::DestructionData))
    {
        return true;
    }
    std::unique_ptr<editor::asset_pipeline::SceneComponentCodec> codec =
        std::make_unique<DestructionCodec>();
    return editor::asset_pipeline::register_scene_component_codec(codec);
}

} // namespace engine::plugins::destruction
