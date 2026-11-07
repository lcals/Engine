#include "editor/commands/components/component_edit.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/audio_listener.hpp"
namespace engine::editor::components
{
namespace
{
bool read(const Json& arguments, schema::AudioListenerDataT& value, std::vector<ResourceReference>&,
          std::string& error)
{
    if (!fields(arguments, {"enabled", "volume"}, error))
    {
        return false;
    }
    if (arguments.contains("volume") && !number(arguments["volume"], value.volume))
    {
        error = "Invalid volume";
        return false;
    }
    return true;
}
Json write(const schema::AudioListenerDataT& value)
{
    Json result = Json::object();
    result["volume"] = value.volume;
    return result;
}
} // namespace
bool register_audio_listener(WorldSystem& world, EditorCommands& commands,
                             SceneCommands& scene_commands)
{
    if (!register_component<engine::AudioListener, schema::AudioListenerDataT>(
            world, scene_commands, commands, "audio_listener",
            [](Node, const auto& args, auto& data, auto& prepared, auto& error)
            { return read(args, data, prepared, error); },
            [](auto, const auto& data) { return write(data); }))
    {
        return false;
    }
    return true;
}
} // namespace engine::editor::components
