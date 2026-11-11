#pragma once

#include "editor/asset_pipeline/components/scene_component_codec.hpp"
#include "editor/commands/editor_commands.hpp"
#include "editor/commands/editor_edit.hpp"
#include "runtime/resource/resource_reference.hpp"
#include "runtime/scene/scene.hpp"

#include <nlohmann/json.hpp>

#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::editor
{
class SceneCommands;
}

namespace engine::editor::components
{
using Json = nlohmann::json;

[[nodiscard]] Node target(WorldSystem& world, const Json& arguments, std::string& error);
[[nodiscard]] bool fields(const Json& arguments, std::initializer_list<std::string_view> names,
                          std::string& error);
[[nodiscard]] bool number(const Json& input, float& value);
[[nodiscard]] bool vector(const Json& input, Vec3& value);

// 不修改场景。成功转移独占记录；失败清空 output 并释放本次准备的引用。
[[nodiscard]] bool prepare_component_edit(WorldSystem& world, SceneCommands& scene_commands,
                                          Node node, std::optional<schema::ComponentDataT> before,
                                          schema::ComponentDataT after,
                                          std::vector<ResourceReference> prepared,
                                          std::unique_ptr<EditorEdit>& output, std::string& error);

// world、scene_commands 和回调捕获的借用须覆盖命令寿命。read 不得修改世界。
template <typename T, typename Data, typename Read, typename Write>
[[nodiscard]] bool register_component(WorldSystem& world, SceneCommands& scene_commands,
                                      EditorCommands& commands, std::string_view name, Read read,
                                      Write write)
{
    return commands.register_command(
               std::string("set_") + std::string(name), true,
               [&world, &scene_commands, read, write](const Json& arguments, Json& result,
                                                      std::unique_ptr<EditorEdit>& edit,
                                                      std::string& error)
               {
                   auto node = target(world, arguments, error);
                   if (!node)
                   {
                       return false;
                   }
                   const auto component = node.get_component<T>();
                   std::optional<schema::ComponentDataT> before;
                   if (component)
                   {
                       before.emplace();
                       if (!component.build_data(*before))
                       {
                           error = "Cannot snapshot component";
                           return false;
                       }
                   }
                   Data data{};
                   if (component && !component.get_settings(data))
                   {
                       error = "Cannot read component settings";
                       return false;
                   }
                   std::vector<ResourceReference> prepared;
                   const auto valid = [&]()
                   {
                       if constexpr (requires { T::valid_settings(data); })
                       {
                           return T::valid_settings(data);
                       }
                       else
                       {
                           schema::ComponentDataT candidate;
                           candidate.payload.Set(data);
                           const auto* codec =
                               asset_pipeline::find_scene_component_codec(T::component_type);
                           return codec && codec->validate(candidate, error);
                       }
                   };
                   if (!read(node, arguments, data, prepared, error) || !valid())
                   {
                       if (error.empty())
                       {
                           error = "Invalid component settings";
                       }
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
                   auto candidate = write(T{}, data);
                   candidate["enabled"] = after.flags->enabled;
                   candidate["sceneId"] = std::to_string(node.scene()->id());
                   candidate["nodeId"] = std::to_string(node.id());
                   after.payload.Set(std::move(data));
                   if (!prepare_component_edit(world, scene_commands, node, std::move(before),
                                               std::move(after), std::move(prepared), edit, error))
                   {
                       return false;
                   }
                   result = std::move(candidate);
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
                   const auto node = target(world, arguments, error);
                   const auto component = node ? node.get_component<T>() : T{};
                   if (!component)
                   {
                       if (error.empty())
                       {
                           error = "Component not found";
                       }
                       return false;
                   }
                   Data data{};
                   if (!component.get_settings(data))
                   {
                       error = "Cannot read component settings";
                       return false;
                   }
                   result = write(component, data);
                   result["enabled"] = component.enabled();
                   result["sceneId"] = std::to_string(node.scene()->id());
                   result["nodeId"] = std::to_string(node.id());
                   return true;
               });
}
} // namespace engine::editor::components
