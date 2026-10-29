#include "editor/commands/editor_command_channel.hpp"

#include "editor/commands/editor_commands.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace engine::editor
{
namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
constexpr std::size_t message_limit = 1024 * 1024;

bool token(std::string_view value)
{
    return !value.empty() && value.size() <= 128 &&
           std::all_of(value.begin(), value.end(),
                       [](unsigned char ch)
                       {
                           return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                                  (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
                       });
}

bool plain_path(const fs::path& path)
{
    fs::path current;
    for (const auto& part : path)
    {
        current /= part;
        std::error_code code;
        const auto status = fs::symlink_status(current, code);
        if (code == std::errc::no_such_file_or_directory)
        {
            continue;
        }
        if (code || fs::is_symlink(status))
        {
            return false;
        }
    }
    return true;
}

bool write_json(const fs::path& path, const Json& value)
{
    const auto text = value.dump(-1, ' ', false, Json::error_handler_t::replace) + '\n';
    if (text.size() > message_limit)
    {
        return false;
    }
    auto temporary = path;
    temporary += ".tmp";
    if (!plain_path(path) || !plain_path(temporary))
    {
        return false;
    }
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << text;
    output.close();
    if (!output ||
        !SDL_RenamePath(PathUtils::to_utf8(temporary).c_str(), PathUtils::to_utf8(path).c_str()))
    {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }
    return true;
}
} // namespace

EditorCommandChannel::EditorCommandChannel(EditorCommands& commands, fs::path directory,
                                           std::string project_root, std::string session)
    : commands_(commands), directory_(std::move(directory)), project_root_(std::move(project_root)),
      session_(std::move(session))
{
}

bool EditorCommandChannel::publish_pending()
{
    if (pending_.is_null())
    {
        return true;
    }
    if (pending_.dump(-1, ' ', false, Json::error_handler_t::replace).size() >= message_limit)
    {
        pending_["success"] = false;
        pending_["result"] = nullptr;
        pending_["error"] = "Editor command result exceeds the 1 MiB receipt limit";
    }
    const auto id = pending_["requestId"].get<std::string>();
    const auto original = directory_ / "editor-receipts" / session_ / (id + ".json");
    if (!write_json(original, pending_) ||
        !write_json(directory_ / "editor-receipt.json", pending_))
    {
        last_error_ = "Cannot publish editor command receipt; execution will not be repeated";
        return false;
    }
    pending_ = nullptr;
    return true;
}

bool EditorCommandChannel::poll()
{
    last_error_.clear();
    if (!publish_pending())
    {
        return false;
    }
    const auto input = directory_ / "editor-command.json";
    std::error_code code;
    if (!plain_path(input))
    {
        last_error_ = "Editor command path contains a symlink or is inaccessible";
        return false;
    }
    const bool exists = fs::exists(input, code);
    if (code)
    {
        last_error_ = "Cannot inspect editor command input";
        return false;
    }
    if (!exists)
    {
        return true;
    }
    const auto size = fs::file_size(input, code);
    std::string text;
    if (code || size > message_limit || !ResourceSystem::read_text(input, text) ||
        text.size() > message_limit)
    {
        last_error_ = "Cannot read editor command input within the 1 MiB limit";
        return false;
    }
    if (text == last_text_)
    {
        return true;
    }
    const auto request = Json::parse(text, nullptr, false);
    if (!request.is_object() || !request.contains("session") || !request["session"].is_string() ||
        !request.contains("projectRoot") || !request["projectRoot"].is_string() ||
        !request.contains("requestId") || !request["requestId"].is_string() ||
        !request.contains("command") || !request["command"].is_string() ||
        !request.contains("arguments"))
    {
        last_error_ = "Invalid editor command envelope";
        return false;
    }
    const auto id = request["requestId"].get<std::string>();
    if (!token(id) || !token(session_))
    {
        last_error_ = "Invalid editor session or request identifier";
        return false;
    }
    if (request["session"] != session_ || request["projectRoot"] != project_root_)
    {
        last_text_ = std::move(text);
        return true;
    }
    const auto receipts = directory_ / "editor-receipts" / session_;
    const auto original = receipts / (id + ".json");
    if (!plain_path(original))
    {
        last_error_ = "Editor receipt path contains a symlink or is inaccessible";
        return false;
    }
    fs::create_directories(receipts, code);
    if (code)
    {
        last_error_ = "Cannot create editor receipt directory";
        return false;
    }
    const bool completed = fs::exists(original, code);
    if (code)
    {
        last_error_ = "Cannot inspect previous editor command receipt";
        return false;
    }
    if (completed)
    {
        std::string content;
        const auto receipt_size = fs::file_size(original, code);
        if (code || receipt_size > message_limit || !ResourceSystem::read_text(original, content) ||
            content.size() > message_limit)
        {
            last_error_ = "Cannot read original editor command receipt";
            return false;
        }
        auto receipt = Json::parse(content, nullptr, false);
        if (!receipt.is_object() || receipt.value("session", Json()) != session_ ||
            receipt.value("projectRoot", Json()) != project_root_ ||
            receipt.value("requestId", Json()) != id || !receipt.contains("success") ||
            !receipt["success"].is_boolean() || !receipt.contains("command") ||
            !receipt["command"].is_string() ||
            receipt["command"].get_ref<const std::string&>().size() > 128 ||
            !receipt.contains("result") ||
            (receipt["success"].get<bool>() ? !receipt["result"].is_object()
                                            : !receipt["result"].is_null()) ||
            !receipt.contains("error") || !receipt["error"].is_string() ||
            !receipt.contains("completedAtMs") || !receipt["completedAtMs"].is_number_integer())
        {
            last_error_ = "Original editor command receipt is invalid; replay rejected";
            return false;
        }
        pending_ = std::move(receipt);
        executed_.insert(id);
    }
    else if (executed_.contains(id))
    {
        last_error_ = "Original editor command receipt is missing; replay rejected";
        return false;
    }
    else
    {
        const auto command = request["command"].get<std::string>();
        Json result;
        std::string error;
        bool success = false;
        if (command.size() <= 128)
        {
            success = commands_.execute(command, request["arguments"], result, error);
        }
        else
        {
            error = "Editor command name exceeds 128 characters";
        }
        const auto completed_at = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count();
        pending_ = {{"session", session_},       {"projectRoot", project_root_},
                    {"requestId", id},           {"command", command.substr(0, 128)},
                    {"success", success},        {"result", std::move(result)},
                    {"error", std::move(error)}, {"completedAtMs", completed_at}};
        executed_.insert(id);
    }
    last_text_ = std::move(text);
    return publish_pending();
}

const std::string& EditorCommandChannel::last_error() const noexcept
{
    return last_error_;
}

} // namespace engine::editor
