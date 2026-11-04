#pragma once

#include <filesystem>

namespace engine::editor
{
class EditorCommands;
}

namespace engine::plugins::animation_tools
{
// Source metadata edits share EditorCommands apply/revert, identity and batch semantics.
[[nodiscard]] bool register_animation_authoring_commands(editor::EditorCommands& commands,
                                                         const std::filesystem::path& project);
} // namespace engine::plugins::animation_tools
