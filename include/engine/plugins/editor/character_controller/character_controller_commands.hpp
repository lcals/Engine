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

// 三个对象均须覆盖已登记命令及其撤销历史的生命周期。
[[nodiscard]] bool register_controller_commands(WorldSystem& world,
                                                editor::SceneCommands& scene_commands,
                                                editor::EditorCommands& commands);

} // namespace engine::plugins::character_controller
