#pragma once

#include <filesystem>

namespace engine
{
class WorldSystem;
}

namespace engine::editor
{
class SceneCommands;
class EditorCommands;
} // namespace engine::editor

namespace engine::plugins::destruction
{

// 借用的 world、scene_commands 须存活到命令及撤销历史释放；project_root 按值捕获。
[[nodiscard]] bool register_destruction_commands(WorldSystem& world,
                                                 editor::SceneCommands& scene_commands,
                                                 editor::EditorCommands& commands,
                                                 const std::filesystem::path& project_root);

} // namespace engine::plugins::destruction
