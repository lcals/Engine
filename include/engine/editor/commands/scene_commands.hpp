#pragma once

namespace engine
{

class EditorCommands;
class WorldSystem;

class SceneCommands final
{
public:
    explicit SceneCommands(WorldSystem& world) noexcept;
    SceneCommands(const SceneCommands&) = delete;
    SceneCommands& operator=(const SceneCommands&) = delete;
    // 本模块和 world 须存活到已注册的处理函数释放。
    [[nodiscard]] bool register_commands(EditorCommands& commands);

private:
    WorldSystem& world_;
};

} // namespace engine
