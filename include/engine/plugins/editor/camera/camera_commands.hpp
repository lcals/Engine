#pragma once
namespace engine
{
class WorldSystem;
namespace editor
{
class SceneCommands;
class EditorCommands;
} // namespace editor
} // namespace engine
namespace engine::plugins::camera_editor
{
[[nodiscard]] bool register_camera_commands(WorldSystem&, editor::SceneCommands&,
                                            editor::EditorCommands&);
}
