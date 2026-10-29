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

class SceneCommands final
{
public:
    explicit SceneCommands(WorldSystem& world) noexcept;
    SceneCommands(const SceneCommands&) = delete;
    SceneCommands& operator=(const SceneCommands&) = delete;
    // 本模块和 world 须存活到已注册的处理函数释放。
    [[nodiscard]] bool register_commands(EditorCommands& commands);

private:
    void synchronize_aliases() noexcept;
    WorldSystem& world_;
    std::uint64_t scene_id_ = 0;
    std::map<std::uint64_t, std::shared_ptr<std::uint64_t>> node_aliases_;
};

} // namespace engine::editor
