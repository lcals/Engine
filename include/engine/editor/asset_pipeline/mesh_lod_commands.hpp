#pragma once

#include <filesystem>

namespace engine::editor
{
class AssetPipelineSystem;
class EditorCommands;

// Commands retain the project path; pipeline must outlive the registered callbacks.
[[nodiscard]] bool register_mesh_lod_commands(EditorCommands& commands,
                                              AssetPipelineSystem& pipeline,
                                              std::filesystem::path project_root);
} // namespace engine::editor
