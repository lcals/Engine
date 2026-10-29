#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <set>
#include <string>

namespace engine
{

class EditorCommands;

class EditorCommandChannel final
{
public:
    EditorCommandChannel(EditorCommands& commands, std::filesystem::path directory,
                         std::string project_root, std::string session);
    [[nodiscard]] bool poll();
    [[nodiscard]] const std::string& last_error() const noexcept;

private:
    [[nodiscard]] bool publish_pending();

    EditorCommands& commands_;
    std::filesystem::path directory_;
    std::string project_root_;
    std::string session_;
    std::string last_text_;
    std::string last_error_;
    std::set<std::string> executed_;
    nlohmann::json pending_;
};

} // namespace engine
