#include "plugins/editor/mcp/project_mcp_tools.hpp"

#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "plugins/editor/asset_build/asset_build_system.hpp"
#include "plugins/editor/asset_sync/asset_sync_system.hpp"
#include "plugins/editor/mcp/mcp_system.hpp"
#include "plugins/editor/mcp/project_tool_utils.hpp"
#include "runtime/utils/path_utils.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace engine
{

bool ProjectMcpTools::register_resource_tools(McpSystem& mcp)
{
    using Json = nlohmann::json;
    const bool build = mcp.register_tool(
        {"build_resources",
         "Incrementally build selected items from a project settings manifest.",
         {{"type", "object"},
          {"properties",
           {{"manifest", {{"type", "string"}}},
            {"items", {{"type", "array"}, {"items", {{"type", "string"}}}}}}},
          {"required", Json::array({"manifest"})},
          {"additionalProperties", false}},
         [this](const Json& arguments, Json& output)
         {
             std::vector<std::string> items;
             if (arguments.contains("items"))
             {
                 for (const auto& item : arguments["items"])
                 {
                     items.push_back(item.get<std::string>());
                 }
             }
             if (!builder_.build(project_root_,
                                 PathUtils::from_utf8(arguments["manifest"].get<std::string>()),
                                 items))
             {
                 output = {{"error", builder_.last_error()}};
                 return false;
             }
             const auto& stats = builder_.last_build();
             output = {{"revision", stats.revision},
                       {"resources", stats.resource_count},
                       {"packages", stats.package_count},
                       {"reusedPackages", stats.reused_package_count},
                       {"output", PathUtils::to_utf8(stats.output_path)}};
             return true;
         }});
    if (!build)
    {
        return false;
    }
    return mcp.register_tool(
        {"sync_assets",
         "Import project resources or synchronize a prebuilt runtime catalog; "
         "success means available, not activated.",
         {{"type", "object"},
          {"properties",
           {{"source",
             {{"type", "string"},
              {"description", "Optional project-relative prebuilt output; skips import."}}}}},
          {"additionalProperties", false}},
         [this](const Json& arguments, Json& output)
         {
             std::filesystem::path source = project_root_ / "cache";
             if (arguments.contains("source"))
             {
                 const auto relative = PathUtils::from_utf8(arguments["source"].get<std::string>());
                 bool valid =
                     !relative.empty() && !relative.is_absolute() && !relative.has_root_path();
                 for (const auto& component : relative)
                 {
                     valid = valid && component != ".." && component != ".";
                 }
                 source = project_root_ / relative;
                 if (!valid || !mcp_tools::no_symlinks(source))
                 {
                     output = {{"error", "Invalid project-relative build output directory"}};
                     return false;
                 }
             }
             else if (!pipeline_.build_assets(project_root_, target_.android_serial.empty()
                                                                 ? std::string_view{}
                                                                 : "android"))
             {
                 output = {{"error", pipeline_.last_error()}};
                 return false;
             }
             if (!target_.android_serial.empty())
             {
                 std::string receipt;
                 const bool success = sync_android(source, receipt);
                 return mcp_tools::android_receipt(success, receipt, output);
             }
             if (!sync_.sync(source, target_.runtime_dir))
             {
                 output = {{"error", sync_.last_error()}};
                 return false;
             }
             output = {{"availableRevision", sync_.revision()},
                       {"runtimeDir", PathUtils::to_utf8(target_.runtime_dir)},
                       {"activation", "pending engine decision"}};
             return true;
         }});
}

} // namespace engine
