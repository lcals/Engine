#include "plugins/editor/effects/effect_codec.hpp"

#include "editor/asset_pipeline/importer_utils.hpp"
#include "editor/asset_pipeline/toml_helpers.hpp"
#include "plugins/runtime/effects/effect_component.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <utility>

namespace engine::plugins::effects
{

schema::ComponentPayload EffectCodec::type() const noexcept
{
    return schema::ComponentPayload::EffectData;
}

std::string_view EffectCodec::name() const noexcept
{
    return "effect";
}

bool EffectCodec::validate(const schema::ComponentDataT& component, std::string& error) const
{
    const auto* value = component.payload.AsEffectData();
    if (value == nullptr || !EffectComponent::valid_settings(*value))
    {
        error = "invalid effect settings";
        return false;
    }
    return true;
}

bool EffectCodec::read(const toml::table& table, schema::ComponentDataT& component,
                       std::string& error) const
{
    constexpr std::array<std::string_view, 6> fields = {"type",  "enabled", "effect",
                                                        "speed", "loop",    "play_on_awake"};
    for (const auto& [name, field] : table)
    {
        static_cast<void>(field);
        if (std::find(fields.begin(), fields.end(), name.str()) == fields.end())
        {
            error = "unknown effect component field: " + std::string(name.str());
            return false;
        }
    }
    schema::EffectDataT value;
    if (table.contains("effect"))
    {
        const auto guid = table["effect"].value<std::string>();
        if (!guid)
        {
            error = "effect must be a resource GUID string";
            return false;
        }
        value.effect_guid = *guid;
    }
    if (table.contains("speed") &&
        !editor::asset_pipeline::float_number(table.get("speed"), value.speed))
    {
        error = "effect speed must be a finite number";
        return false;
    }
    for (const auto& [name, destination] :
         {std::pair{"loop", &value.loop}, std::pair{"play_on_awake", &value.play_on_awake}})
    {
        if (table.contains(name))
        {
            const auto enabled = table[name].value_exact<bool>();
            if (!enabled)
            {
                error = std::string("effect ") + name + " must be boolean";
                return false;
            }
            *destination = *enabled;
        }
    }
    if (!EffectComponent::valid_settings(value))
    {
        error = "invalid effect GUID or speed outside [0,16]";
        return false;
    }
    component.payload.Set(std::move(value));
    return true;
}

bool EffectCodec::scan_dependencies(const schema::ComponentDataT& component,
                                    const editor::asset_pipeline::ImportContext& context,
                                    std::vector<Guid>& dependencies, std::string& error) const
{
    if (!validate(component, error))
    {
        return false;
    }
    const auto& guid = component.payload.AsEffectData()->effect_guid;
    return guid.empty() || editor::asset_pipeline::reference(guid, schema::AssetKind::effect,
                                                             context, dependencies, error);
}

std::span<const std::string_view> EffectCodec::resource_fields() const noexcept
{
    static constexpr std::string_view fields[]{"effect"};
    return fields;
}

void EffectCodec::write(const schema::ComponentDataT& component, toml::table& table) const
{
    const auto& value = *component.payload.AsEffectData();
    table.insert("type", "effect");
    table.insert("effect", value.effect_guid);
    table.insert("speed", value.speed);
    table.insert("loop", value.loop);
    table.insert("play_on_awake", value.play_on_awake);
}

bool register_effect_codec()
{
    if (editor::asset_pipeline::find_scene_component_codec(schema::ComponentPayload::EffectData))
    {
        return true;
    }
    std::unique_ptr<editor::asset_pipeline::SceneComponentCodec> codec =
        std::make_unique<EffectCodec>();
    return editor::asset_pipeline::register_scene_component_codec(codec);
}

} // namespace engine::plugins::effects
