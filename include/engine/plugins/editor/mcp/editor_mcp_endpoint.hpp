#pragma once
#include "plugins/runtime/mcp/runtime_mcp_endpoint.hpp"

namespace engine::editor
{
class EditorSystem;
}
namespace engine::plugins::mcp
{
class EditorMcpEndpoint final
{
public:
    EditorMcpEndpoint() noexcept;
    ~EditorMcpEndpoint() noexcept;
    [[nodiscard]] bool initialize(editor::EditorSystem& editor, RuntimeHost& host,
                                  const McpEndpointConfig& config);
    [[nodiscard]] bool poll();
    [[nodiscard]] bool publish_status();
    void shutdown() noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
    std::string error_;
};
} // namespace engine::plugins::mcp
