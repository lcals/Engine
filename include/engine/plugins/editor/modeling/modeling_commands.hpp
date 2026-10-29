#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace engine
{
class Node;
class WorldSystem;
} // namespace engine

namespace engine::editor
{
class EditorCommands;
} // namespace engine::editor

namespace engine::plugins::modeling
{

class ModelingCommands final
{
public:
    ModelingCommands(engine::WorldSystem& world, std::filesystem::path project_root);
    ~ModelingCommands() noexcept;
    ModelingCommands(const ModelingCommands&) = delete;
    ModelingCommands& operator=(const ModelingCommands&) = delete;
    // 本模块与 world 须存活到命令处理函数及撤销历史释放。
    [[nodiscard]] bool register_commands(engine::editor::EditorCommands& commands);
    // 宿主在场景生命周期更新后调用；场景身份改变即释放编辑源快照。
    void synchronize_scene() noexcept;
    // 写入由编辑器宿主提供的安全目标；保留多边形可编辑源，镜像派生结果烘焙后保存。
    [[nodiscard]] bool export_mesh(const engine::Node& node, const std::filesystem::path& path,
                                   std::string& error);

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace engine::plugins::modeling
