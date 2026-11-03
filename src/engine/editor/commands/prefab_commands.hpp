#pragma once

#include <cstdint>
#include <map>
#include <memory>

namespace engine
{
class WorldSystem;
}
namespace engine::editor
{
class EditorCommands;
class SceneCommands;
[[nodiscard]] bool
register_prefab_commands(WorldSystem& world, SceneCommands& scenes,
                         std::map<std::uint64_t, std::shared_ptr<std::uint64_t>>& aliases,
                         EditorCommands& commands);
} // namespace engine::editor
