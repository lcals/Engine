#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace engine::editor
{
class EditorCommands;
}

namespace engine::plugins::destruction
{

[[nodiscard]] bool destruction_file_path(const std::filesystem::path& project,
                                         std::string_view directory, std::string_view relative,
                                         std::string_view extension, std::filesystem::path& output,
                                         std::string& error);
[[nodiscard]] bool register_destruction_asset_commands(editor::EditorCommands& commands,
                                                       const std::filesystem::path& project);

} // namespace engine::plugins::destruction
