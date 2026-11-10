#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <set>
#include <string>

namespace engine::plugins::mcp
{

class McpCommandChannel final
{
public:
    using Handler =
        std::function<bool(std::string_view, const nlohmann::json&, nlohmann::json&, std::string&)>;
    McpCommandChannel(std::filesystem::path directory, std::string session, Handler execute);
    [[nodiscard]] bool poll();
    [[nodiscard]] const std::string& last_error() const noexcept;
    [[nodiscard]] const nlohmann::json& last_receipt() const noexcept;

private:
    [[nodiscard]] bool publish_pending();

    Handler execute_;
    std::filesystem::path directory_;
    std::string session_;
    std::string last_text_;
    std::string last_error_;
    std::set<std::string> executed_;
    nlohmann::json pending_;
    nlohmann::json last_receipt_;
};

} // namespace engine::plugins::mcp
