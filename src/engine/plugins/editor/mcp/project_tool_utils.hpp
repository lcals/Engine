#pragma once

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <string>

namespace engine::plugins::mcp::tools
{

[[nodiscard]] bool no_symlinks(const std::filesystem::path& path);
[[nodiscard]] bool read_project_status(const std::filesystem::path& runtime,
                                       nlohmann::json& status);
[[nodiscard]] bool editor_alive(const nlohmann::json& status);
[[nodiscard]] bool android_receipt(bool success, const std::string& text, nlohmann::json& output);

} // namespace engine::plugins::mcp::tools
