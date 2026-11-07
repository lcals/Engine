#include "plugins/editor/effects/effect_commands.hpp"
#include "editor/resource_access.hpp"

#include "editor/commands/components/component_edit.hpp"
#include "editor/commands/scene_commands.hpp"
#include "plugins/runtime/effects/effect_component.hpp"
#include "runtime/resource/resource_system.hpp"

namespace engine::plugins::effects
{
namespace
{

using Json = nlohmann::json;

bool read(ResourceLoader* loader, const Json& arguments, schema::EffectDataT& value,
          std::string& error)
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
            editor::resource_access::ResourceReference resource;
            if (!editor::resource_access::load(loader, path, resource) ||
                resource->kind() != schema::AssetKind::effect)
            {
                error = "effect path must reference an imported Effekseer resource";
                return false;
            }
            auto guid = resource->info().guid;
            value.effect_guid = std::move(guid);
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

Json write(const EffectComponent& component)
{
    const auto& value = component.settings();
    std::string path;
    if (!value.effect_guid.empty())
    {
        static_cast<void>(editor::resource_access::path(
            component.node().scene()->world().resource_loader(), value.effect_guid, path));
    }
    return {{"effect", path},
            {"effectGuid", value.effect_guid},
            {"speed", value.speed},
            {"loop", value.loop},
            {"playOnAwake", value.play_on_awake},
            {"enabled", component.enabled()},
            {"playing", component.playing()},
            {"sceneId", std::to_string(component.node().scene()->id())},
            {"nodeId", std::to_string(component.node().id())}};
}

} // namespace

bool register_effect_commands(WorldSystem& world, editor::SceneCommands& scene_commands,
                              editor::EditorCommands& commands)
{
    if (!commands.register_command(
            "set_effect", true,
            [&world, &scene_commands](const Json& arguments, Json& result,
                                      std::unique_ptr<editor::EditorEdit>& edit, std::string& error)
            {
                auto node = editor::components::target(world, arguments, error);
                if (!node)
                {
                    return false;
                }
                const auto component = node.get_component<EffectComponent>();
                std::optional<schema::ComponentDataT> before;
                if (component.valid())
                {
                    before.emplace();
                    if (!component.build_data(*before))
                    {
                        error = "Cannot snapshot effect component";
                        return false;
                    }
                }
                auto value = component.valid() ? component.settings() : schema::EffectDataT{};
                if (!read(world.resource_loader(), arguments, value, error))
                {
                    return false;
                }
                schema::ComponentDataT after;
                after.flags = std::make_unique<schema::ComponentFlagsT>();
                after.flags->enabled = !component || component.enabled();
                after.flags->visible = !component || component.visible();
                if (arguments.contains("enabled"))
                {
                    if (!arguments["enabled"].is_boolean())
                    {
                        error = "enabled must be boolean";
                        return false;
                    }
                    after.flags->enabled = arguments["enabled"].get<bool>();
                }
                auto identity = scene_commands.node_identity(node.id());
                if (!identity)
                {
                    error = "Cannot resolve stable node identity";
                    return false;
                }
                std::string path;
                if (!value.effect_guid.empty())
                {
                    static_cast<void>(editor::resource_access::path(world.resource_loader(),
                                                                    value.effect_guid, path));
                }
                result = {{"effect", path},
                          {"effectGuid", value.effect_guid},
                          {"speed", value.speed},
                          {"loop", value.loop},
                          {"playOnAwake", value.play_on_awake},
                          {"enabled", after.flags->enabled},
                          {"sceneId", std::to_string(node.scene()->id())},
                          {"nodeId", std::to_string(node.id())}};
                after.payload.Set(std::move(value));
                std::vector<schema::AssetEntryT> versions;
                if ((before &&
                     !editor::components::resource_versions(world, *before, versions, error)) ||
                    !editor::components::resource_versions(world, after, versions, error))
                {
                    return false;
                }
                edit = std::make_unique<editor::components::ComponentEdit>(
                    world, node.scene()->id(), std::move(identity), std::move(before),
                    std::move(after));
                return true;
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
                result = write(component);
                return true;
            });
    };
    return control("get_effect", nullptr) && control("play_effect", &EffectComponent::play) &&
           control("pause_effect", &EffectComponent::pause) &&
           control("stop_effect", &EffectComponent::stop) && control("get_effect_state", nullptr);
}

} // namespace engine::plugins::effects
