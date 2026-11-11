#include "plugins/editor/mcp/project_mcp_tools.hpp"
#include "editor/file_text.hpp"

#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "plugins/editor/asset_build/asset_build_system.hpp"
#include "plugins/editor/asset_sync/asset_sync_system.hpp"
#include "plugins/editor/mcp/project_tool_utils.hpp"
#include "plugins/runtime/mcp/mcp_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <system_error>
#include <utility>

namespace engine::plugins::mcp
{
namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

bool valid_android_target(const McpTarget& target)
{
    const auto ascii_letter = [](unsigned char value)
    { return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z'); };
    if (target.android_serial.empty() || target.android_package.empty() ||
        target.adb_executable.empty() || !target.runtime_dir.empty() ||
        target.android_package.find('.') == std::string::npos)
    {
        return false;
    }
    for (const unsigned char value : target.android_serial)
    {
        if (!ascii_letter(value) && !(value >= '0' && value <= '9') && value != '-' &&
            value != '_' && value != '.' && value != ':')
        {
            return false;
        }
    }
    bool component_start = true;
    for (const unsigned char value : target.android_package)
    {
        if (value == '.')
        {
            if (component_start)
            {
                return false;
            }
            component_start = true;
        }
        else
        {
            if ((!ascii_letter(value) && value != '_') &&
                (component_start || value < '0' || value > '9'))
            {
                return false;
            }
            component_start = false;
        }
    }
    return !component_start;
}

bool valid_ios_target(const McpTarget& target)
{
    if (target.ios_udid.size() < 20 || target.ios_udid.size() > 64 ||
        target.ios_bundle_identifier.empty() || target.ios_application_name.empty() ||
        !target.runtime_dir.empty() || (target.ios_simulator && target.xcrun_executable.empty()))
    {
        return false;
    }
    for (const unsigned char value : target.ios_udid)
    {
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
              (value >= 'A' && value <= 'F') || value == '-'))
        {
            return false;
        }
    }
    if (target.ios_bundle_identifier.find('.') == std::string::npos ||
        target.ios_bundle_identifier.front() == '.' || target.ios_bundle_identifier.back() == '.' ||
        target.ios_bundle_identifier.find("..") != std::string::npos)
    {
        return false;
    }
    for (const unsigned char value : target.ios_bundle_identifier)
    {
        if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '-' || value == '.'))
        {
            return false;
        }
    }
    if (target.ios_application_name.front() < 'a' || target.ios_application_name.front() > 'z')
    {
        return false;
    }
    for (const unsigned char value : target.ios_application_name)
    {
        if (!((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '-' ||
              value == '_'))
        {
            return false;
        }
    }
    return true;
}

} // namespace

namespace tools
{
bool no_symlinks(const fs::path& path)
{
    std::error_code error;
    fs::path current;
    for (const auto& component : path)
    {
        current /= component;
        const auto status = fs::symlink_status(current, error);
        if (error == std::errc::no_such_file_or_directory)
        {
            error.clear();
            continue;
        }
        if (error || fs::is_symlink(status))
        {
            return false;
        }
    }
    return true;
}

bool read_project_status(const fs::path& runtime, Json& status)
{
    const auto path = runtime / "project-status.json";
    std::error_code error;
    const auto size = fs::file_size(path, error);
    std::string text;
    if (error || size > 64 * 1024 || !no_symlinks(path) ||
        !engine::editor::file_detail::read_text(path, text))
    {
        return false;
    }
    status = Json::parse(text, nullptr, false);
    return status.is_object() && status.contains("session") && status["session"].is_string() &&
           status.contains("projectRoot") && status["projectRoot"].is_string() &&
           status.contains("projectState") && status["projectState"].is_string() &&
           status.contains("updatedAtMs") && status["updatedAtMs"].is_number_integer() &&
           status.contains("lastRequestId") && status["lastRequestId"].is_string() &&
           status.contains("lastRequestSucceeded") && status["lastRequestSucceeded"].is_boolean();
}

bool editor_alive(const Json& status)
{
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    const auto time = status["updatedAtMs"].get<std::int64_t>();
    return time <= now && time >= now - 5000 && status["projectState"] != "closed";
}

bool device_receipt(bool success, const std::string& text, Json& output)
{
    output = Json::parse(text, nullptr, false);
    if (!output.is_object())
    {
        output = {{"error", "Invalid device operation receipt"}};
        return false;
    }
    return success;
}
} // namespace tools

ProjectMcpTools::ProjectMcpTools(engine::editor::AssetPipelineSystem& pipeline,
                                 engine::plugins::asset_build::AssetBuildSystem& builder,
                                 engine::plugins::asset_sync::AssetSyncSystem& sync,
                                 fs::path project_root, McpTarget target)
    : pipeline_(pipeline), builder_(builder), sync_(sync), project_root_(std::move(project_root)),
      target_(std::move(target))
{
}

bool ProjectMcpTools::initialize()
{
    if (initialized_)
    {
        return false;
    }
    if (!pipeline_.is_initialized() || !builder_.is_initialized() || !sync_.is_initialized())
    {
        std::fputs("MCP requires initialized asset pipeline, build and sync systems\n", stderr);
        return false;
    }
    std::error_code error;
    project_root_ = fs::absolute(project_root_, error).lexically_normal();
    if (error || !tools::no_symlinks(project_root_))
    {
        return false;
    }
    for (const char* name : {"settings", "cache", "assets"})
    {
        if (!tools::no_symlinks(project_root_ / name) ||
            !fs::is_directory(project_root_ / name, error) || error)
        {
            std::fputs("MCP project must contain settings, cache and assets directories\n", stderr);
            return false;
        }
    }
    const bool android = !target_.android_serial.empty() || !target_.android_package.empty();
    const bool ios = !target_.ios_udid.empty() || !target_.ios_bundle_identifier.empty() ||
                     !target_.ios_application_name.empty() || target_.ios_simulator;
    if (android && ios)
    {
        std::fputs("MCP requires exactly one explicit runtime target\n", stderr);
        return false;
    }
    if (ios)
    {
        if (!valid_ios_target(target_))
        {
            std::fputs("iOS MCP requires an explicit UDID, bundle identifier and application name; "
                       "local and Android targets are mutually exclusive\n",
                       stderr);
            return false;
        }
        initialized_ = true;
        return true;
    }
    if (android)
    {
        if (!valid_android_target(target_))
        {
            std::fputs("Android MCP requires a valid explicit serial, package and adb path; "
                       "--runtime-dir is mutually exclusive\n",
                       stderr);
            return false;
        }
        initialized_ = true;
        return true;
    }
    if (target_.runtime_dir.empty())
    {
        std::fputs("MCP requires an explicit local, Android or iOS target\n", stderr);
        return false;
    }
    target_.runtime_dir = fs::absolute(target_.runtime_dir, error).lexically_normal();
    if (error || !tools::no_symlinks(target_.runtime_dir))
    {
        return false;
    }
    fs::create_directories(target_.runtime_dir, error);
    initialized_ = !error;
    return initialized_;
}

bool ProjectMcpTools::register_tools(McpSystem& mcp)
{
    return initialized_ && register_resource_tools(mcp) && register_runtime_tools(mcp);
}

} // namespace engine::plugins::mcp
