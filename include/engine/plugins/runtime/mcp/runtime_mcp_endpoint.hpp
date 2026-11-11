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
struct McpEndpointConfig
{
    std::filesystem::path cache_root;
    std::filesystem::path channel_directory;
    std::string session;
};

#if defined(ENGINE_ENABLE_TEST_SUPPORT)
class RuntimeMcpEndpoint final
{
public:
    RuntimeMcpEndpoint() noexcept;
    ~RuntimeMcpEndpoint() noexcept;
    RuntimeMcpEndpoint(const RuntimeMcpEndpoint&) = delete;
    RuntimeMcpEndpoint& operator=(const RuntimeMcpEndpoint&) = delete;
    [[nodiscard]] bool initialize(RuntimeHost& host, const McpEndpointConfig& config);
    [[nodiscard]] bool poll();
    [[nodiscard]] bool publish_status();
    void shutdown() noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
    std::string error_;
};
#endif
} // namespace engine::plugins::mcp
