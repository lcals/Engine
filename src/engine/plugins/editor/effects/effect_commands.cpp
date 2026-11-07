#include "plugins/editor/effects/effect_commands.hpp"
#include "editor/resource_access.hpp"

#include "editor/commands/component_commands.hpp"
#include "editor/commands/scene_commands.hpp"
#include "plugins/runtime/effects/effect_component.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/world/world_system.hpp"

namespace engine::plugins::effects
{
namespace
{

using Json = nlohmann::json;

bool read(ResourceLoader* loader, const Json& arguments, schema::EffectDataT& value,
          std::vector<ResourceReference>& prepared, std::string& error)
{
    if (!editor::components::fields(arguments,
                                    {"enabled", "effect", "speed", "loop", "playOnAwake"}, error))
    {
        return false;
    }
    if (arguments.contains("effect"))
    {
        if (!arguments["effect"].is_string())
        {
            error = "effect must be a resource path string";
            return false;
        }
        const auto path = arguments["effect"].get<std::string>();
        if (path.empty())
        {
            value.effect_guid.clear();
        }
        else
        {
            engine::ResourceReference resource;
            if (!editor::resource_access::load(loader, path, resource) ||
                resource->kind() != schema::AssetKind::effect)
            {
                error = "effect path must reference an imported Effekseer resource";
                return false;
            }
            auto guid = resource->info().guid;
            value.effect_guid = std::move(guid);
            prepared.push_back(std::move(resource));
        }
    }
    if (arguments.contains("speed") && !editor::components::number(arguments["speed"], value.speed))
    {
        error = "effect speed must be a finite number";
        return false;
    }
    for (const auto& [name, destination] :
         {std::pair{"loop", &value.loop}, std::pair{"playOnAwake", &value.play_on_awake}})
    {
        if (arguments.contains(name))
        {
            if (!arguments[name].is_boolean())
            {
                error = std::string(name) + " must be boolean";
                return false;
            }
            *destination = arguments[name].get<bool>();
        }
    }
    if (!EffectComponent::valid_settings(value))
    {
        error = "Effect GUID or speed is invalid; speed must be within [0,16]";
        return false;
    }
    return true;
}

Json write(ResourceLoader* loader, const schema::EffectDataT& value)
{
    std::string path;
    if (!value.effect_guid.empty())
    {
        static_cast<void>(editor::resource_access::path(loader, value.effect_guid, path));
    }
    return {{"effect", path},
            {"effectGuid", value.effect_guid},
            {"speed", value.speed},
            {"loop", value.loop},
            {"playOnAwake", value.play_on_awake}};
}

} // namespace

bool register_effect_commands(WorldSystem& world, editor::SceneCommands& scene_commands,
                              editor::EditorCommands& commands)
{
    if (!editor::components::register_component<EffectComponent, schema::EffectDataT>(
            world, scene_commands, commands, "effect",
            [&world](Node, const Json& args, auto& data, auto& prepared, auto& error)
            { return read(world.resource_loader(), args, data, prepared, error); },
            [&world](EffectComponent component, const auto& data)
            {
                auto result = write(world.resource_loader(), data);
                if (component)
                {
                    result["playing"] = component.playing();
                }
                return result;
            }))
    {
        return false;
    }
    const auto control = [&world, &commands](const char* name, bool (EffectComponent::*operation)())
    {
        return commands.register_command(
            name, false,
            [&world, operation](const Json& arguments, Json& result,
                                std::unique_ptr<editor::EditorEdit>&, std::string& error)
            {
                if (!editor::components::fields(arguments, {}, error))
                {
                    return false;
                }
                auto node = editor::components::target(world, arguments, error);
                auto component =
                    node.valid() ? node.get_component<EffectComponent>() : EffectComponent{};
                if (!component || (operation != nullptr && !(component.*operation)()))
                {
                    if (error.empty())
                    {
                        error = "Effect operation requires a valid component and playable resource";
                    }
                    return false;
                }
                schema::EffectDataT data;
                if (!component.get_settings(data))
                {
                    error = "Cannot read component settings";
                    return false;
                }
                result = write(world.resource_loader(), data);
                result["enabled"] = component.enabled();
                result["playing"] = component.playing();
                result["sceneId"] = std::to_string(node.scene()->id());
                result["nodeId"] = std::to_string(node.id());
                return true;
            });
    };
    return control("play_effect", &EffectComponent::play) &&
           control("pause_effect", &EffectComponent::pause) &&
           control("stop_effect", &EffectComponent::stop);
}

} // namespace engine::plugins::effects
