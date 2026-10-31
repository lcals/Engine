#pragma once

#include "editor/commands/components/component_edit.hpp"
#include "editor/commands/scene_commands.hpp"

namespace engine::plugins::character_controller
{

// 各类型提供字段读写；复用编辑器事务和稳定节点身份，不持有组件借用。
template <typename T, typename Data, typename Read, typename Write>
[[nodiscard]] bool register_character_component(WorldSystem& world,
                                                editor::SceneCommands& scene_commands,
                                                editor::EditorCommands& commands,
                                                std::string_view name, Read read, Write write)
{
    using Json = nlohmann::json;
    return commands.register_command(
               std::string("set_") + std::string(name), true,
               [&world, &scene_commands, read, write](const Json& arguments, Json& result,
                                                      std::unique_ptr<editor::EditorEdit>& edit,
                                                      std::string& error)
               {
                   auto* node = editor::components::target(world, arguments, error);
                   if (node == nullptr)
                   {
                       return false;
                   }
                   const auto* component = node->get_component<T>();
                   std::optional<schema::ComponentDataT> before;
                   if (component != nullptr)
                   {
                       before.emplace();
                       if (!component->build_data(*before))
                       {
                           error = "Cannot snapshot character component";
                           return false;
                       }
                   }
                   Data value = component != nullptr ? component->settings() : Data{};
                   if (!read(world, *node, arguments, value, error) || !T::valid_settings(value))
                   {
                       if (error.empty())
                       {
                           error = "Invalid character component settings";
                       }
                       return false;
                   }
                   schema::ComponentDataT after;
                   after.payload.Set(value);
                   after.enabled = component == nullptr || component->enabled();
                   if (arguments.contains("enabled"))
                   {
                       if (!arguments["enabled"].is_boolean())
                       {
                           error = "enabled must be boolean";
                           return false;
                       }
                       after.enabled = arguments["enabled"].get<bool>();
                   }
                   auto identity = scene_commands.node_identity(node->id());
                   if (!identity)
                   {
                       error = "Cannot resolve stable node identity";
                       return false;
                   }
                   result = write(world, value);
                   result["enabled"] = after.enabled;
                   result["sceneId"] = std::to_string(node->scene().id());
                   result["nodeId"] = std::to_string(node->id());
                   edit = std::make_unique<editor::components::ComponentEdit>(
                       world, node->scene().id(), std::move(identity), std::move(before),
                       std::move(after));
                   return true;
               }) &&
           commands.register_command(
               std::string("get_") + std::string(name), false,
               [&world, write](const Json& arguments, Json& result,
                               std::unique_ptr<editor::EditorEdit>&, std::string& error)
               {
                   if (!editor::components::fields(arguments, {}, error))
                   {
                       return false;
                   }
                   const auto* node = editor::components::target(world, arguments, error);
                   const auto* component = node != nullptr ? node->get_component<T>() : nullptr;
                   if (component == nullptr)
                   {
                       if (error.empty())
                       {
                           error = "Character component not found";
                       }
                       return false;
                   }
                   result = write(world, component->settings());
                   result["enabled"] = component->enabled();
                   result["sceneId"] = std::to_string(node->scene().id());
                   result["nodeId"] = std::to_string(node->id());
                   return true;
               });
}

} // namespace engine::plugins::character_controller
