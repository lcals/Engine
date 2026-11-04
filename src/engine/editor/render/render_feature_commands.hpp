#pragma once

namespace engine
{
class RenderSystem;
class WorldSystem;
} // namespace engine
namespace engine::editor
{
class EditorCommands;
class SceneCommands;
[[nodiscard]] bool register_render_feature_commands(RenderSystem& renderer, WorldSystem& world,
                                                    EditorCommands& commands,
                                                    SceneCommands& scenes);
} // namespace engine::editor
