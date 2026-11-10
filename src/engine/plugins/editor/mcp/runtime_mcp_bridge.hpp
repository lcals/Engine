#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace engine
{
class RuntimeHost;
}

namespace engine::plugins::mcp
{

class RuntimeMcpBridge final
{
public:
    RuntimeMcpBridge() noexcept;
    ~RuntimeMcpBridge() noexcept;
    RuntimeMcpBridge(const RuntimeMcpBridge&) = delete;
    RuntimeMcpBridge& operator=(const RuntimeMcpBridge&) = delete;

    [[nodiscard]] bool initialize(RuntimeHost& host, const std::filesystem::path& project,
                                  const std::filesystem::path& directory);
    [[nodiscard]] bool poll();
    void shutdown() noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
    std::string error_;
};
} // namespace engine::plugins::mcp
