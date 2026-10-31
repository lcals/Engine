#include "plugins/editor/character_controller/character_controller_codec.hpp"

#include "editor/asset_pipeline/toml_helpers.hpp"
#include "plugins/editor/character_controller/character_camera_codec.hpp"
#include "plugins/editor/character_controller/character_visual_codec.hpp"
#include "plugins/runtime/character_controller/character_controller.hpp"

#include <memory>
#include <utility>

namespace engine::plugins::character_controller
{

schema::ComponentPayload CharacterControllerCodec::type() const noexcept
{
    return schema::ComponentPayload::CharacterControllerData;
}

std::string_view CharacterControllerCodec::name() const noexcept
{
    return "character_controller";
}

bool CharacterControllerCodec::validate(const schema::ComponentDataT& component,
                                        std::string& error) const
{
    const auto* value = component.payload.AsCharacterControllerData();
    if (value == nullptr || !CharacterController::valid_settings(*value))
    {
        error = "invalid character_controller settings";
        return false;
    }
    return true;
}

bool CharacterControllerCodec::read(const toml::table& table, schema::ComponentDataT& component,
                                    std::string& error) const
{
    schema::CharacterControllerDataT value;
    for (const auto name : {"mode", "eye_height", "camera_distance", "pitch_min", "pitch_max"})
    {
        if (table.contains(name))
        {
            error = "character_controller." + std::string(name) +
                    " moved to character_camera on the referenced Camera node; "
                    "migrate the source scene";
            return false;
        }
    }
    const auto read_number = [&](std::string_view name, float& output)
    {
        if (table.contains(name) && !editor::asset_pipeline::float_number(table.get(name), output))
        {
            error = "invalid character_controller." + std::string(name);
            return false;
        }
        return true;
    };
    if (!read_number("move_speed", value.move_speed) ||
        !read_number("jump_speed", value.jump_speed) ||
        !read_number("look_sensitivity", value.look_sensitivity))
    {
        return false;
    }
    if (table.contains("camera_node_id") &&
        !editor::asset_pipeline::identifier(table.get("camera_node_id"), value.camera_node_id))
    {
        error = "invalid character_controller.camera_node_id";
        return false;
    }
    if (!CharacterController::valid_settings(value))
    {
        error = "invalid character_controller settings";
        return false;
    }
    component.payload.Set(std::move(value));
    return true;
}

void CharacterControllerCodec::write(const schema::ComponentDataT& component,
                                     toml::table& table) const
{
    const auto* value = component.payload.AsCharacterControllerData();
    table.insert("type", "character_controller");
    table.insert("move_speed", value->move_speed);
    table.insert("jump_speed", value->jump_speed);
    table.insert("look_sensitivity", value->look_sensitivity);
    table.insert("camera_node_id", std::to_string(value->camera_node_id));
}

bool register_controller_codec()
{
    if (!editor::asset_pipeline::find_scene_component_codec(
            schema::ComponentPayload::CharacterControllerData))
    {
        std::unique_ptr<editor::asset_pipeline::SceneComponentCodec> codec =
            std::make_unique<CharacterControllerCodec>();
        if (!editor::asset_pipeline::register_scene_component_codec(codec))
        {
            return false;
        }
    }
    return register_character_camera_codec() && register_character_visual_codec();
}

} // namespace engine::plugins::character_controller
