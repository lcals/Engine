#include "plugins/editor/character_controller/character_camera_codec.hpp"

#include "editor/asset_pipeline/toml_helpers.hpp"
#include "plugins/runtime/character_controller/character_camera.hpp"

#include <memory>
#include <utility>

namespace engine::plugins::character_controller
{

schema::ComponentPayload CharacterCameraCodec::type() const noexcept
{
    return schema::ComponentPayload::CharacterCameraData;
}

std::string_view CharacterCameraCodec::name() const noexcept
{
    return "character_camera";
}

bool CharacterCameraCodec::validate(const schema::ComponentDataT& component,
                                    std::string& error) const
{
    const auto* value = component.payload.AsCharacterCameraData();
    if (value == nullptr || !CharacterCamera::valid_settings(*value))
    {
        error = "invalid character_camera settings";
        return false;
    }
    return true;
}

bool CharacterCameraCodec::read(const toml::table& table, schema::ComponentDataT& component,
                                std::string& error) const
{
    schema::CharacterCameraDataT value;
    if (table.contains("target_node_id") &&
        !editor::asset_pipeline::identifier(table.get("target_node_id"), value.target_node_id))
    {
        error = "invalid character_camera.target_node_id";
        return false;
    }
    if (table.contains("mode"))
    {
        const auto mode = table["mode"].value<std::string_view>();
        if (mode == "first_person")
        {
            value.mode = schema::CharacterView::first_person;
        }
        else if (mode == "third_person")
        {
            value.mode = schema::CharacterView::third_person;
        }
        else
        {
            error = "invalid character_camera.mode";
            return false;
        }
    }
    const auto number = [&](std::string_view name, float& output)
    {
        if (table.contains(name) && !editor::asset_pipeline::float_number(table.get(name), output))
        {
            error = "invalid character_camera." + std::string(name);
            return false;
        }
        return true;
    };
    if (!number("eye_height", value.eye_height) ||
        !number("camera_distance", value.camera_distance) ||
        !number("pitch_min", value.pitch_min) || !number("pitch_max", value.pitch_max) ||
        !number("collision_radius", value.collision_radius) ||
        !number("collision_margin", value.collision_margin))
    {
        return false;
    }
    if (!CharacterCamera::valid_settings(value))
    {
        error = "invalid character_camera settings";
        return false;
    }
    component.payload.Set(std::move(value));
    return true;
}

void CharacterCameraCodec::write(const schema::ComponentDataT& component, toml::table& table) const
{
    const auto* value = component.payload.AsCharacterCameraData();
    table.insert("type", "character_camera");
    table.insert("target_node_id", std::to_string(value->target_node_id));
    table.insert("mode", schema::EnumNameCharacterView(value->mode));
    table.insert("eye_height", value->eye_height);
    table.insert("camera_distance", value->camera_distance);
    table.insert("pitch_min", value->pitch_min);
    table.insert("pitch_max", value->pitch_max);
    table.insert("collision_radius", value->collision_radius);
    table.insert("collision_margin", value->collision_margin);
}

bool register_character_camera_codec()
{
    if (editor::asset_pipeline::find_scene_component_codec(
            schema::ComponentPayload::CharacterCameraData))
    {
        return true;
    }
    std::unique_ptr<editor::asset_pipeline::SceneComponentCodec> codec =
        std::make_unique<CharacterCameraCodec>();
    return editor::asset_pipeline::register_scene_component_codec(codec);
}

} // namespace engine::plugins::character_controller
