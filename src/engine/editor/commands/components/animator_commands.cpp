#include "editor/commands/components/component_edit.hpp"

#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/animator.hpp"

namespace engine::editor::components
{
namespace
{
bool read(ResourceLoader* loader, const Json& arguments, schema::AnimatorDataT& value,
          std::vector<ResourceReference>& prepared, std::string& error)
{
    if (!fields(arguments, {"enabled", "animation", "speed", "loop", "playOnAwake"}, error))
    {
        return false;
    }
    if (arguments.contains("animation"))
    {
        if (!arguments["animation"].is_string())
        {
            error = "animation must be a resource path";
            return false;
        }
        const auto path = arguments["animation"].get<std::string>();
        if (path.empty())
        {
            value.animation_guid.clear();
        }
        else if (!resource_access::prepare_guid(loader, path, value.animation_guid, prepared))
        {
            error = "Animation resource path not found";
            return false;
        }
    }
    if (arguments.contains("speed") && !number(arguments["speed"], value.speed))
    {
        error = "Invalid animation speed";
        return false;
    }
    for (const auto& field : {"loop", "playOnAwake"})
    {
        if (arguments.contains(field) && !arguments[field].is_boolean())
        {
            error = std::string(field) + " must be boolean";
            return false;
        }
    }
    if (arguments.contains("loop"))
    {
        value.loop = arguments["loop"].get<bool>();
    }
    if (arguments.contains("playOnAwake"))
    {
        value.play_on_awake = arguments["playOnAwake"].get<bool>();
    }
    return true;
}

Json write(ResourceLoader* loader, const schema::AnimatorDataT& value)
{
    std::string path;
    if (!value.animation_guid.empty())
    {
        (void)resource_access::path(loader, value.animation_guid, path);
    }
    return {{"animationGuid", value.animation_guid},
            {"animation", path},
            {"speed", value.speed},
            {"loop", value.loop},
            {"playOnAwake", value.play_on_awake}};
}

Json state(const Animator& value)
{
    const char* playback = "stopped";
    switch (value.playback())
    {
    case AnimationPlayback::stopped:
        break;
    case AnimationPlayback::playing:
        playback = "playing";
        break;
    case AnimationPlayback::paused:
        playback = "paused";
        break;
    }
    return {{"playback", playback},
            {"playing", value.playback() == AnimationPlayback::playing},
            {"time", value.time()}};
}
} // namespace

bool register_animator(WorldSystem& world, EditorCommands& commands, NodeAliases& aliases)
{
    if (!register_component<Animator, schema::AnimatorDataT>(
            world, commands, "animator", aliases,
            [&world](const auto& args, auto& data, auto& prepared, auto& error)
            { return read(world.resource_loader(), args, data, prepared, error); },
            [&world](const auto& data) { return write(world.resource_loader(), data); }))
    {
        return false;
    }
    const auto control =
        [&world, &commands](const char* name, bool (Animator::*operation)() noexcept)
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
                auto node = target(world, arguments, error);
                auto value = node ? node.get_component<Animator>() : Animator{};
                if (!value || (operation && !(value.*operation)()))
                {
                    if (error.empty())
                    {
                        error = "Animation control requires a valid Animator and animation";
                    }
                    return false;
                }
                result = state(value);
                return true;
            });
    };
    return control("play_animation", &Animator::play) &&
           control("pause_animation", &Animator::pause) &&
           control("stop_animation", &Animator::stop) && control("get_animation_state", nullptr) &&
           commands.register_command(
               "seek_animation", false,
               [&world](const Json& arguments, Json& result, std::unique_ptr<EditorEdit>&,
                        std::string& error)
               {
                   if (!fields(arguments, {"seconds"}, error) || !arguments.contains("seconds") ||
                       !arguments["seconds"].is_number())
                   {
                       if (error.empty())
                       {
                           error = "seconds must be a finite number within the animation";
                       }
                       return false;
                   }
                   auto node = target(world, arguments, error);
                   auto value = node ? node.get_component<Animator>() : Animator{};
                   if (!value || !value.seek(arguments["seconds"].get<double>()))
                   {
                       if (error.empty())
                       {
                           error = "Cannot seek animation at the requested time";
                       }
                       return false;
                   }
                   result = state(value);
                   return true;
               });
}
} // namespace engine::editor::components
