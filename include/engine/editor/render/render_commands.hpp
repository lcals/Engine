#pragma once

#include <filesystem>

namespace engine
{
class RenderSystem;
}

namespace engine::editor
{
class EditorCommands;

class RenderCommands final
{
public:
    RenderCommands(RenderSystem& renderer, std::filesystem::path capture_root);
    RenderCommands(const RenderCommands&) = delete;
    RenderCommands& operator=(const RenderCommands&) = delete;
    [[nodiscard]] bool register_commands(EditorCommands& commands);

private:
    RenderSystem& renderer_;
    std::filesystem::path capture_root_;
    std::filesystem::path capture_path_;
};
} // namespace engine::editor
