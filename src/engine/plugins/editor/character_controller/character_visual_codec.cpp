#include "plugins/editor/character_controller/character_visual_codec.hpp"

#include "editor/asset_pipeline/toml_helpers.hpp"
#include "plugins/runtime/character_controller/character_visual.hpp"

#include <memory>
#include <utility>

namespace engine::plugins::character_controller
{

schema::ComponentPayload CharacterVisualCodec::type() const noexcept
{
    return schema::ComponentPayload::CharacterVisualData;
}

std::string_view CharacterVisualCodec::name() const noexcept
{
    return "character_visual";
}

bool CharacterVisualCodec::validate(const schema::ComponentDataT& component,
                                    std::string& error) const
{
    const auto* value = component.payload.AsCharacterVisualData();
    if (value == nullptr || !CharacterVisual::valid_settings(*value))
    {
        error = "invalid character_visual settings";
        return false;
    }
    return true;
}

bool CharacterVisualCodec::read(const toml::table& table, schema::ComponentDataT& component,
                                std::string& error) const
{
    schema::CharacterVisualDataT value;
    if (table.contains("kind"))
    {
        const auto kind = table["kind"].value<std::string_view>();
        if (kind == "body")
        {
            value.kind = schema::CharacterVisualKind::body;
        }
        else if (kind == "head")
        {
            value.kind = schema::CharacterVisualKind::head;
        }
        else if (kind == "first_person")
        {
            value.kind = schema::CharacterVisualKind::first_person;
        }
        else
        {
            error = "invalid character_visual.kind";
            return false;
        }
    }
    if (!CharacterVisual::valid_settings(value))
    {
        error = "invalid character_visual settings";
        return false;
    }
    component.payload.Set(std::move(value));
    return true;
}

void CharacterVisualCodec::write(const schema::ComponentDataT& component, toml::table& table) const
{
    const auto* value = component.payload.AsCharacterVisualData();
    table.insert("type", "character_visual");
    table.insert("kind", schema::EnumNameCharacterVisualKind(value->kind));
}

bool register_character_visual_codec()
{
    if (editor::asset_pipeline::find_scene_component_codec(
            schema::ComponentPayload::CharacterVisualData))
    {
        return true;
    }
    std::unique_ptr<editor::asset_pipeline::SceneComponentCodec> codec =
        std::make_unique<CharacterVisualCodec>();
    return editor::asset_pipeline::register_scene_component_codec(codec);
}

} // namespace engine::plugins::character_controller
