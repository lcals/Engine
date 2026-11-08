#include "plugins/editor/character_controller/character_visual_commands.hpp"

#include "editor/commands/component_commands.hpp"
#include "plugins/runtime/character_controller/character_visual.hpp"
#include "runtime/world/world_system.hpp"

namespace engine::plugins::character_controller
{
namespace
{

using Json = nlohmann::json;

bool read(WorldSystem&, Node&, const Json& arguments, schema::CharacterVisualDataT& value,
          std::string& error)
{
    if (!editor::components::fields(arguments, {"enabled", "kind"}, error))
    {
        return false;
    }
    if (arguments.contains("kind"))
    {
        if (arguments["kind"] == "body")
        {
            value.kind = schema::CharacterVisualKind::body;
        }
        else if (arguments["kind"] == "head")
        {
            value.kind = schema::CharacterVisualKind::head;
        }
        else if (arguments["kind"] == "first_person")
        {
            value.kind = schema::CharacterVisualKind::first_person;
        }
        else
        {
            error = "Invalid character visual kind";
            return false;
        }
    }
    return true;
}

Json write(const WorldSystem&, const schema::CharacterVisualDataT& value)
{
    return {{"kind", schema::EnumNameCharacterVisualKind(value.kind)}};
}

} // namespace

bool register_character_visual_commands(WorldSystem& world, editor::SceneCommands& scene_commands,
                                        editor::EditorCommands& commands)
{
    return editor::components::register_component<CharacterVisual, schema::CharacterVisualDataT>(
        world, scene_commands, commands, "character_visual",
        [&world](Node node, const auto& args, auto& data, auto&, auto& error)
        { return read(world, node, args, data, error); },
        [&world](auto, const auto& data) { return write(world, data); });
}

} // namespace engine::plugins::character_controller
