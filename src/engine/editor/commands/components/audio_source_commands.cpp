#include "editor/commands/components/component_edit.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/audio_source.hpp"
namespace engine::editor::components
{
namespace
{
bool read(const Json& arguments, schema::AudioSourceDataT& value, std::string& error)
{
    if (!fields(arguments,
                {"enabled", "clip", "volume", "pitch", "loop", "playOnAwake", "spatial",
                 "minDistance", "maxDistance"},
                error))
    {
        return false;
    }
    if (arguments.contains("clip"))
    {
        if (!arguments["clip"].is_string())
        {
            error = "clip must be a resource path";
            return false;
        }
        const auto path = arguments["clip"].get<std::string>();
        if (path.empty())
        {
            value.clip_guid.clear();
        }
        else if (!ResourceSystem::path_to_guid(path, value.clip_guid))
        {
            error = "Audio resource path not found";
            return false;
        }
    }
    if (arguments.contains("volume") && !number(arguments["volume"], value.volume))
    {
        error = "Invalid volume";
        return false;
    }
    if (arguments.contains("pitch") && !number(arguments["pitch"], value.pitch))
    {
        error = "Invalid pitch";
        return false;
    }
    if (arguments.contains("loop"))
    {
        if (!arguments["loop"].is_boolean())
        {
            error = "loop must be boolean";
            return false;
        }
        value.loop = arguments["loop"].get<bool>();
    }
    if (arguments.contains("playOnAwake"))
    {
        if (!arguments["playOnAwake"].is_boolean())
        {
            error = "playOnAwake must be boolean";
            return false;
        }
        value.play_on_awake = arguments["playOnAwake"].get<bool>();
    }
    if (arguments.contains("spatial"))
    {
        if (!arguments["spatial"].is_boolean())
        {
            error = "spatial must be boolean";
            return false;
        }
        value.spatial = arguments["spatial"].get<bool>();
    }
    if (arguments.contains("minDistance") && !number(arguments["minDistance"], value.min_distance))
    {
        error = "Invalid minDistance";
        return false;
    }
    if (arguments.contains("maxDistance") && !number(arguments["maxDistance"], value.max_distance))
    {
        error = "Invalid maxDistance";
        return false;
    }
    return true;
}
Json write(const schema::AudioSourceDataT& value)
{
    Json result = Json::object();
    result["clipGuid"] = value.clip_guid;
    std::string path;
    if (!value.clip_guid.empty())
    {
        (void)ResourceSystem::guid_to_path(value.clip_guid, path);
    }
    result["clip"] = path;
    result["volume"] = value.volume;
    result["pitch"] = value.pitch;
    result["loop"] = value.loop;
    result["playOnAwake"] = value.play_on_awake;
    result["spatial"] = value.spatial;
    result["minDistance"] = value.min_distance;
    result["maxDistance"] = value.max_distance;
    return result;
}
} // namespace
bool register_audio_source(WorldSystem& world, EditorCommands& commands, NodeAliases& aliases)
{
    if (!register_component<engine::AudioSource, schema::AudioSourceDataT>(
            world, commands, "audio_source", aliases, read, write))
    {
        return false;
    }
    const auto control =
        [&world, &commands](const char* name, bool (AudioSource::*operation)() noexcept)
    {
        return commands.register_command(
            name, false,
            [&world, operation](const Json& arguments, Json& result, std::unique_ptr<EditorEdit>&,
                                std::string& error)
            {
                if (!fields(arguments, {}, error))
                {
                    return false;
                }
                auto* node = target(world, arguments, error);
                auto* source = node ? node->get_component<AudioSource>() : nullptr;
                if (!source || (operation && !(source->*operation)()))
                {
                    if (error.empty())
                    {
                        error = "Audio control requires a valid source and clip";
                    }
                    return false;
                }
                result = {{"playback", static_cast<int>(source->playback())},
                          {"playing", source->playing()}};
                return true;
            });
    };
    return control("play_audio", &AudioSource::play) &&
           control("pause_audio", &AudioSource::pause) &&
           control("stop_audio", &AudioSource::stop) && control("get_audio_state", nullptr);
}
} // namespace engine::editor::components
