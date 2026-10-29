#pragma once

#include "editor/commands/editor_commands.hpp"
#include "editor/commands/editor_edit.hpp"
#include "runtime/scene/component_registry.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace engine::editor::components
{
using Json = nlohmann::json;
using NodeAliases = std::map<std::uint64_t, std::shared_ptr<std::uint64_t>>;
[[nodiscard]] Node* target(WorldSystem& world, const Json& arguments, std::string& error);
[[nodiscard]] bool fields(const Json& arguments, std::initializer_list<std::string_view> names,
                          std::string& error);
[[nodiscard]] bool number(const Json& input, float& value);
[[nodiscard]] bool vector(const Json& input, Vec3& value);

class ComponentEdit final : public EditorEdit
{
public:
    ComponentEdit(WorldSystem& world, std::uint64_t scene, std::shared_ptr<std::uint64_t> node,
                  std::optional<schema::ComponentDataT> before, schema::ComponentDataT after);
    [[nodiscard]] bool apply(std::string& error) override;
    [[nodiscard]] bool revert(std::string& error) override;

private:
    [[nodiscard]] bool replace(const std::optional<schema::ComponentDataT>& expected,
                               const std::optional<schema::ComponentDataT>& value,
                               std::string& error);
    WorldSystem& world_;
    std::uint64_t scene_;
    std::shared_ptr<std::uint64_t> node_;
    ComponentType type_;
    std::optional<schema::ComponentDataT> before_;
    schema::ComponentDataT after_;
};

// 每类文件提供读写函数；共用主线程事务和场景身份检查，不持有组件借用。
template <typename T, typename Data, typename Read, typename Write>
[[nodiscard]] bool register_component(WorldSystem& world, EditorCommands& commands,
                                      std::string_view name, NodeAliases& aliases, Read read,
                                      Write write)
{
    return commands.register_command(
               std::string("set_") + std::string(name), true,
               [&world, &aliases, read, write](const Json& arguments, Json& result,
                                               std::unique_ptr<EditorEdit>& edit,
                                               std::string& error)
               {
                   auto* node = target(world, arguments, error);
                   if (node == nullptr)
                   {
                       return false;
                   }
                   auto* component = node->get_component<T>();
                   std::optional<schema::ComponentDataT> before;
                   if (component != nullptr)
                   {
                       before.emplace();
                       if (!component->build_data(*before))
                       {
                           error = "Cannot snapshot component";
                           return false;
                       }
                   }
                   Data data = component != nullptr ? component->settings() : Data{};
                   if (!read(arguments, data, error) || !T::valid_settings(data))
                   {
                       if (error.empty())
                       {
                           error = "Invalid component settings";
                       }
                       return false;
                   }
                   schema::ComponentDataT after;
                   after.payload.Set(data);
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
                   result = write(data);
                   result["enabled"] = after.enabled;
                   result["sceneId"] = std::to_string(node->scene().id());
                   result["nodeId"] = std::to_string(node->id());
                   const auto alias = aliases.find(node->id());
                   auto identity = alias != aliases.end() && *alias->second == node->id()
                                       ? alias->second
                                       : std::make_shared<std::uint64_t>(node->id());
                   edit = std::make_unique<ComponentEdit>(world, node->scene().id(),
                                                          std::move(identity), std::move(before),
                                                          std::move(after));
                   return true;
               }) &&
           commands.register_command(
               std::string("get_") + std::string(name), false,
               [&world, write](const Json& arguments, Json& result, std::unique_ptr<EditorEdit>&,
                               std::string& error)
               {
                   if (!fields(arguments, {}, error))
                   {
                       return false;
                   }
                   const auto* node = target(world, arguments, error);
                   const auto* component = node == nullptr ? nullptr : node->get_component<T>();
                   if (component == nullptr)
                   {
                       if (error.empty())
                       {
                           error = "Component not found";
                       }
                       return false;
                   }
                   result = write(component->settings());
                   result["enabled"] = component->enabled();
                   result["sceneId"] = std::to_string(node->scene().id());
                   result["nodeId"] = std::to_string(node->id());
                   return true;
               });
}
[[nodiscard]] bool register_character_motor(WorldSystem& world, EditorCommands& commands,
                                            NodeAliases& aliases);
[[nodiscard]] bool register_collider(WorldSystem& world, EditorCommands& commands,
                                     NodeAliases& aliases);
[[nodiscard]] bool register_rigid_body(WorldSystem& world, EditorCommands& commands,
                                       NodeAliases& aliases);
[[nodiscard]] bool register_audio_source(WorldSystem& world, EditorCommands& commands,
                                         NodeAliases& aliases);
[[nodiscard]] bool register_audio_listener(WorldSystem& world, EditorCommands& commands,
                                           NodeAliases& aliases);
[[nodiscard]] bool register_animator(WorldSystem& world, EditorCommands& commands,
                                     NodeAliases& aliases);
[[nodiscard]] bool prepare_model(WorldSystem& world, NodeAliases& aliases, const Json& arguments,
                                 Json& result, std::unique_ptr<EditorEdit>& edit,
                                 std::string& error);
} // namespace engine::editor::components
