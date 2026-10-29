#include "plugins/editor/mcp/project_mcp_tools.hpp"

#include "plugins/editor/mcp/mcp_system.hpp"
#include "plugins/editor/mcp/project_tool_utils.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL_filesystem.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

namespace engine::plugins::mcp
{
namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
using tools::editor_alive;
using tools::no_symlinks;
using tools::read_project_status;

bool control_project(const fs::path& runtime, const fs::path& project_root,
                     const std::string& command, Json& response)
{
    const auto lock = runtime / "project-control.lock";
    std::error_code error;
    if (!no_symlinks(lock) || !fs::create_directory(lock, error))
    {
        response = {{"error", "Another project control operation owns the lock"}};
        return false;
    }
    const bool result = [&]() -> bool
    {
        Json before;
        if (!read_project_status(runtime, before) || !editor_alive(before))
        {
            response = {{"error", "No live editor session for this runtime directory"}};
            return false;
        }
        const auto canonical_project = fs::canonical(project_root, error);
        if (error || before["projectRoot"] != PathUtils::to_utf8(canonical_project))
        {
            response = {{"error", "Editor session belongs to a different project"}};
            return false;
        }
        const auto target = runtime / "project-control.json";
        if (!no_symlinks(target))
        {
            response = {{"error", "Project control path contains a symlink"}};
            return false;
        }
        if (fs::exists(target, error))
        {
            std::string previous_text;
            const auto previous_size = fs::file_size(target, error);
            if (error || previous_size > 16 * 1024 ||
                !ResourceSystem::read_text(target, previous_text))
            {
                response = {{"error", "Cannot inspect previous project request"}};
                return false;
            }
            const auto previous = Json::parse(previous_text, nullptr, false);
            if (previous.is_object() && previous.contains("session") &&
                previous["session"] == before["session"] && previous.contains("requestId") &&
                (previous["requestId"] != before["lastRequestId"] ||
                 before["projectState"] == "starting" || before["projectState"] == "stopping"))
            {
                response = {{"error", "Previous project request is still pending"},
                            {"requestId", previous["requestId"]}};
                return false;
            }
        }
        else if (error)
        {
            response = {{"error", "Cannot inspect project control path"}};
            return false;
        }
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto request_id = std::to_string(nonce);
        const auto temporary = runtime / (".project-control-" + request_id + ".json");
        if (!no_symlinks(temporary))
        {
            response = {{"error", "Invalid temporary control path"}};
            return false;
        }
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << Json({{"session", before["session"]},
                        {"requestId", request_id},
                        {"command", command}})
                      .dump()
               << '\n';
        output.close();
        if (!output || !SDL_RenamePath(PathUtils::to_utf8(temporary).c_str(),
                                       PathUtils::to_utf8(target).c_str()))
        {
            fs::remove(temporary, error);
            response = {{"error", "Cannot publish project request"}};
            return false;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline)
        {
            Json receipt;
            if (read_project_status(runtime, receipt))
            {
                if (receipt["session"] != before["session"])
                {
                    response = {{"error", "Editor session changed before completion"},
                                {"requestId", request_id}};
                    return false;
                }
                if (receipt["lastRequestId"] == request_id &&
                    receipt["projectState"] != "starting" && receipt["projectState"] != "stopping")
                {
                    receipt["requestId"] = request_id;
                    receipt["activation"] = "completed";
                    receipt["runtimeDir"] = PathUtils::to_utf8(runtime);
                    const bool succeeded = receipt["lastRequestSucceeded"].get<bool>();
                    response = std::move(receipt);
                    return succeeded;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        response = {{"error", "Project request submitted; completion is not confirmed"},
                    {"requestId", request_id},
                    {"activation", "pending"}};
        return false;
    }();
    fs::remove(lock, error);
    return result;
}

} // namespace

bool ProjectMcpTools::register_lifecycle_tools(McpSystem& mcp)
{
    if (!target_.android_serial.empty())
    {
        return true;
    }
    for (const auto& [name, description] :
         {std::pair{"start_project",
                    "Start the loaded project in the existing editor window; wait for completion."},
          {"stop_project",
           "Stop the project, release its resources, and restore the blank editor window."}})
    {
        const std::string command = name;
        if (!mcp.register_tool(
                {command,
                 description,
                 {{"type", "object"},
                  {"properties", Json::object()},
                  {"additionalProperties", false}},
                 [this, command](const Json&, Json& output)
                 { return control_project(target_.runtime_dir, project_root_, command, output); }}))
        {
            return false;
        }
    }
    return true;
}

} // namespace engine::plugins::mcp
