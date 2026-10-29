#include "plugins/mcp/editor_mcp_tools.hpp"

#include "editor/asset_pipeline/project_lock.hpp"
#include "plugins/mcp/mcp_system.hpp"
#include "plugins/mcp/project_tool_utils.hpp"
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

namespace engine
{
namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
constexpr std::uintmax_t max_file_size = 1024 * 1024;

bool identifier(const Json& value)
{
    if (!value.is_string())
    {
        return false;
    }
    const auto& text = value.get_ref<const std::string&>();
    if (text.empty() || text.size() > 128)
    {
        return false;
    }
    for (const unsigned char character : text)
    {
        if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') || character == '_' || character == '-'))
        {
            return false;
        }
    }
    return true;
}

bool fail(Json& reply, std::string message)
{
    reply = {{"error", std::move(message)}};
    return false;
}

bool read_json(const fs::path& path, Json& value, bool& exists)
{
    exists = false;
    value = Json::object();
    std::error_code error;
    if (!mcp_tools::no_symlinks(path))
    {
        return false;
    }
    exists = fs::exists(path, error);
    if (error || !exists)
    {
        return !error;
    }
    const auto size = fs::file_size(path, error);
    std::string text;
    if (error || size > max_file_size || !ResourceSystem::read_text(path, text) ||
        text.size() > max_file_size)
    {
        return false;
    }
    value = Json::parse(text, nullptr, false);
    return value.is_object();
}

bool valid_request(const Json& request)
{
    return request.is_object() && request.contains("session") && identifier(request["session"]) &&
           request.contains("requestId") && identifier(request["requestId"]) &&
           request.contains("projectRoot") && request["projectRoot"].is_string() &&
           request.contains("command") && request["command"].is_string() &&
           request.contains("arguments") && request["arguments"].is_object();
}

bool fetch_receipt(const fs::path& runtime, const fs::path& project, const Json& identity,
                   Json& receipt, bool& exists)
{
    const auto path = runtime / "editor-receipts" / identity["session"].get<std::string>() /
                      (identity["requestId"].get<std::string>() + ".json");
    if (!read_json(path, receipt, exists))
    {
        return false;
    }
    if (!exists)
    {
        return true;
    }
    return receipt.contains("session") && receipt["session"] == identity["session"] &&
           receipt.contains("requestId") && receipt["requestId"] == identity["requestId"] &&
           receipt.contains("projectRoot") &&
           receipt["projectRoot"] == PathUtils::to_utf8(project) && receipt.contains("command") &&
           receipt["command"].is_string() &&
           (!identity.contains("command") || receipt["command"] == identity["command"]) &&
           receipt.contains("success") && receipt["success"].is_boolean() &&
           receipt.contains("result") &&
           (receipt["result"].is_object() ||
            (!receipt["success"].get<bool>() && receipt["result"].is_null())) &&
           receipt.contains("error") && receipt["error"].is_string() &&
           receipt.contains("completedAtMs") && receipt["completedAtMs"].is_number_integer();
}

bool publish_request(const fs::path& runtime, const Json& request)
{
    const auto destination = runtime / "editor-command.json";
    const auto temporary = runtime / (".editor-command-" + request["requestId"].get<std::string>());
    if (!mcp_tools::no_symlinks(destination) || !mcp_tools::no_symlinks(temporary))
    {
        return false;
    }
    const auto text = request.dump(-1, ' ', false, Json::error_handler_t::replace);
    if (text.size() > max_file_size)
    {
        return false;
    }
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    if (!output || !SDL_RenamePath(PathUtils::to_utf8(temporary).c_str(),
                                   PathUtils::to_utf8(destination).c_str()))
    {
        std::error_code error;
        fs::remove(temporary, error);
        return false;
    }
    return true;
}

Json object_schema(Json properties, Json required = Json::array())
{
    return {{"type", "object"},
            {"properties", std::move(properties)},
            {"required", std::move(required)},
            {"additionalProperties", false}};
}

Json vector_schema(std::size_t count)
{
    return {{"type", "array"},
            {"items", {{"type", "number"}}},
            {"minItems", count},
            {"maxItems", count}};
}

Json id_schema()
{
    return {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}};
}

} // namespace

EditorMcpTools::EditorMcpTools(fs::path project_root, McpTarget target)
    : project_root_(std::move(project_root)), target_(std::move(target))
{
}

bool EditorMcpTools::register_tools(McpSystem& system)
{
    if (!target_.android_serial.empty())
    {
        return true;
    }
    std::error_code error;
    if (!mcp_tools::no_symlinks(project_root_) || !mcp_tools::no_symlinks(target_.runtime_dir))
    {
        return false;
    }
    project_root_ = fs::canonical(project_root_, error);
    if (error || target_.runtime_dir.empty())
    {
        return false;
    }
    target_.runtime_dir = fs::canonical(target_.runtime_dir, error);
    if (error)
    {
        return false;
    }
    const auto register_command =
        [&](std::string name, std::string command, std::string description, Json schema)
    {
        return system.register_tool(
            {std::move(name), std::move(description), std::move(schema),
             [this, command = std::move(command)](const Json& arguments, Json& reply)
             { return call_command(command, arguments, reply); }});
    };
    const auto required_node = Json::array({"sceneId", "nodeId"});
    const auto node_properties = Json{{"sceneId", id_schema()}, {"nodeId", id_schema()}};
    auto transform = node_properties;
    for (const char* field : {"position", "rotation", "scale"})
    {
        transform[field] = vector_schema(3);
    }
    auto light = node_properties;
    light["color"] = vector_schema(4);
    light["intensity"] = {{"type", "number"}};
    light["kind"] = {{"type", "string"}, {"enum", {"directional", "point", "spot"}}};
    for (const char* name : {"range", "innerAngle", "outerAngle"})
    {
        light[name] = {{"type", "number"}};
    }
    auto camera = node_properties;
    camera["projection"] = {{"type", "string"}, {"enum", {"perspective", "orthographic"}}};
    for (const char* name : {"fovDegrees", "nearClip", "farClip", "orthographicSize"})
    {
        camera[name] = {{"type", "number"}};
    }
    auto texture = node_properties;
    texture["name"] = id_schema();
    texture["path"] = {{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}};
    texture["slot"] = {{"type", "integer"}, {"minimum", 0}, {"maximum", 15}};
    auto material = node_properties;
    material["slot"] = texture["slot"];
    material["name"] = {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}};
    material["type"] = {{"type", "string"}, {"enum", {"float", "v2", "v3", "v4"}}};
    material["value"] = Json::object();
    const auto batch_entry =
        object_schema({{"command", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                       {"arguments", {{"type", "object"}, {"additionalProperties", true}}}},
                      {"command", "arguments"});
    return register_command("get_scene", "get_scene", "Read the active scene and its components.",
                            object_schema(Json::object())) &&
           register_command(
               "create_node", "create_node",
               "Create a named node with Transform and an optional component.",
               object_schema(
                   {{"sceneId", id_schema()},
                    {"name", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                    {"component", {{"type", "string"}, {"enum", {"empty", "camera", "light"}}}}},
                   {"sceneId", "name", "component"})) &&
           register_command("set_active_camera", "set_active_camera",
                            "Select the active scene camera.",
                            object_schema(node_properties, required_node)) &&
           register_command(
               "save_scene", "save_scene",
               "Save the active scene, mesh sources and material values into project assets. "
               "Import after stopping the project.",
               object_schema(
                   {{"sceneId", id_schema()},
                    {"path", {{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}}}},
                   {"sceneId", "path"})) &&
           register_command("set_transform", "set_transform", "Edit an existing node transform.",
                            object_schema(std::move(transform), required_node)) &&
           register_command("set_light", "set_light", "Edit an existing light component.",
                            object_schema(std::move(light), required_node)) &&
           register_command("set_camera", "set_camera", "Edit camera projection and clipping.",
                            object_schema(std::move(camera), required_node)) &&
           register_command(
               "set_material_texture", "set_material_texture",
               "Assign a texture by sampler name and resource path.",
               object_schema(std::move(texture), {"sceneId", "nodeId", "name", "path"})) &&
           register_command(
               "set_material_type", "set_material_type",
               "Select a material resource for a renderer slot.",
               object_schema(
                   {{"sceneId", id_schema()},
                    {"nodeId", id_schema()},
                    {"slot", {{"type", "integer"}, {"minimum", 0}, {"maximum", 15}}},
                    {"path", {{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}}}},
                   {"sceneId", "nodeId", "path"})) &&
           register_command("set_material_parameter", "set_material_parameter",
                            "Set an existing material parameter by name and declared type.",
                            object_schema(std::move(material),
                                          {"sceneId", "nodeId", "name", "type", "value"})) &&
           register_command("execute_batch", "batch", "Apply edits as one undoable transaction.",
                            object_schema({{"commands",
                                            {{"type", "array"},
                                             {"items", batch_entry},
                                             {"minItems", 1},
                                             {"maxItems", 64}}}},
                                          {"commands"})) &&
           register_command("undo", "undo", "Undo the last editor edit in this scene.",
                            object_schema({{"sceneId", id_schema()}}, {"sceneId"})) &&
           register_command("redo", "redo", "Redo the last undone editor edit in this scene.",
                            object_schema({{"sceneId", id_schema()}}, {"sceneId"})) &&
           system.register_tool(
               {"get_editor_receipt", "Look up a durable editor receipt by session and request id.",
                object_schema({{"session", id_schema()}, {"requestId", id_schema()}},
                              {"session", "requestId"}),
                [this](const Json& arguments, Json& reply)
                { return read_receipt(arguments, reply); }});
}

bool EditorMcpTools::call_command(std::string_view command, const Json& arguments, Json& reply)
{
    if (!target_.android_serial.empty() || target_.runtime_dir.empty() || command.empty() ||
        command.size() > 128 || !arguments.is_object())
    {
        return fail(reply, "Editor command requires a desktop target, name and arguments object");
    }
    asset_pipeline::ProjectLock lock;
    bool acquired = false;
    std::string error;
    const auto lock_path = target_.runtime_dir / "editor-command.lock";
    if (!mcp_tools::no_symlinks(lock_path) || !lock.try_acquire(lock_path, acquired, error) ||
        !acquired)
    {
        return fail(reply, "Another editor command owns the submission lock: " + error);
    }
    Json status;
    if (!mcp_tools::read_project_status(target_.runtime_dir, status) ||
        !mcp_tools::editor_alive(status) || !identifier(status["session"]))
    {
        return fail(reply, "No live editor session for this runtime directory");
    }
    if (status["projectRoot"] != PathUtils::to_utf8(project_root_))
    {
        return fail(reply, "Editor session belongs to a different project");
    }
    Json previous;
    bool exists = false;
    if (!read_json(target_.runtime_dir / "editor-command.json", previous, exists) ||
        (exists && !valid_request(previous)))
    {
        return fail(reply, "Cannot inspect previous editor command");
    }
    if (exists && previous["session"] == status["session"])
    {
        Json receipt;
        if (!fetch_receipt(target_.runtime_dir, project_root_, previous, receipt, exists))
        {
            return fail(reply, "Previous editor receipt is invalid");
        }
        if (!exists)
        {
            reply = {{"error", "Previous editor command is still pending"},
                     {"activation", "pending"},
                     {"session", previous["session"]},
                     {"requestId", previous["requestId"]},
                     {"projectRoot", PathUtils::to_utf8(project_root_)},
                     {"runtimeDir", PathUtils::to_utf8(target_.runtime_dir)}};
            return false;
        }
    }
    // 提交锁覆盖整个等待过程，同一运行目录的请求 ID 不会同时生成。
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string request_id = std::to_string(nonce);
    const Json request{{"session", status["session"]},
                       {"projectRoot", PathUtils::to_utf8(project_root_)},
                       {"requestId", request_id},
                       {"command", command},
                       {"arguments", arguments}};
    if (!publish_request(target_.runtime_dir, request))
    {
        return fail(reply, "Cannot publish editor command (maximum 1 MiB)");
    }
    reply = {{"activation", "pending"},
             {"session", request["session"]},
             {"requestId", request_id},
             {"projectRoot", PathUtils::to_utf8(project_root_)},
             {"runtimeDir", PathUtils::to_utf8(target_.runtime_dir)}};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline)
    {
        Json receipt;
        if (!fetch_receipt(target_.runtime_dir, project_root_, request, receipt, exists))
        {
            reply["error"] = "Editor receipt identity or format is invalid";
            return false;
        }
        if (exists)
        {
            receipt["activation"] = "completed";
            receipt["runtimeDir"] = PathUtils::to_utf8(target_.runtime_dir);
            const bool success = receipt["success"].get<bool>();
            reply = std::move(receipt);
            return success;
        }
        Json current;
        if (mcp_tools::read_project_status(target_.runtime_dir, current) &&
            (current["session"] != request["session"] ||
             current["projectRoot"] != request["projectRoot"] ||
             current["projectState"] == "closed"))
        {
            reply["error"] = "Editor session changed before completion; query the original receipt";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    reply["error"] = "Editor command submitted; completion is not confirmed";
    return false;
}

bool EditorMcpTools::read_receipt(const Json& arguments, Json& reply)
{
    if (!identifier(arguments["session"]) || !identifier(arguments["requestId"]))
    {
        return fail(reply, "Invalid receipt identity");
    }
    bool exists = false;
    if (!fetch_receipt(target_.runtime_dir, project_root_, arguments, reply, exists))
    {
        return fail(reply, "Editor receipt identity or format is invalid");
    }
    if (exists)
    {
        reply["activation"] = "completed";
        reply["runtimeDir"] = PathUtils::to_utf8(target_.runtime_dir);
        return true;
    }
    Json request;
    if (!read_json(target_.runtime_dir / "editor-command.json", request, exists))
    {
        return fail(reply, "Cannot inspect pending editor command");
    }
    if (exists && valid_request(request) && request["session"] == arguments["session"] &&
        request["requestId"] == arguments["requestId"] &&
        request["projectRoot"] == PathUtils::to_utf8(project_root_))
    {
        reply = {{"activation", "pending"},
                 {"session", request["session"]},
                 {"requestId", request["requestId"]},
                 {"projectRoot", request["projectRoot"]},
                 {"runtimeDir", PathUtils::to_utf8(target_.runtime_dir)}};
        return true;
    }
    return fail(reply, "No editor request or receipt matches this identity");
}

} // namespace engine
