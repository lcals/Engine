#pragma once

namespace engine
{
class WorldSystem;
}

namespace engine::editor
{
class SceneCommands;
class EditorCommands;
} // namespace engine::editor

namespace engine::plugins::character_controller
{

// 借用对象覆盖命令及撤销历史的生命周期。
[[nodiscard]] bool register_character_camera_commands(WorldSystem& world,
                                                      editor::SceneCommands& scene_commands,
                                                      editor::EditorCommands& commands);

} // namespace engine::plugins::character_controller
