#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace engine
{
class Node;
class WorldSystem;
} // namespace engine

namespace engine::editor
{

class EditorCommands;

class ScenePersistence final
{
public:
    using MeshSourceWriter =
        std::function<bool(const Node&, const std::filesystem::path&, std::string&)>;
    using SavedCallback = std::function<void(std::string_view)>;

    ScenePersistence(WorldSystem& world, std::filesystem::path project_root,
                     MeshSourceWriter writer, SavedCallback saved = {});
    ScenePersistence(const ScenePersistence&) = delete;
    ScenePersistence& operator=(const ScenePersistence&) = delete;
    // world、回调借用的模块及本对象须存活到命令处理函数释放。
    [[nodiscard]] bool register_commands(EditorCommands& commands);

private:
    WorldSystem& world_;
    std::filesystem::path project_root_;
    MeshSourceWriter writer_;
    SavedCallback saved_;
};

} // namespace engine::editor
