#pragma once

#include "runtime/system/system.hpp"

#include <memory>

struct ImGuiContext;

namespace engine
{

class ProjectSystem;
class Engine;

class EditorSystem final : public System
{
public:
    // 项目由宿主拥有，须先初始化并保持就绪直到编辑器关闭。
    // runtime 可空；非空时必须先初始化，并与 App 一起存活到编辑器关闭。
    explicit EditorSystem(ProjectSystem& project, Engine* runtime = nullptr) noexcept;
    ~EditorSystem() noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    ProjectSystem& project_;
    Engine* runtime_ = nullptr;
    struct State;
    std::unique_ptr<State> state_;
    ImGuiContext* context_ = nullptr;
};

} // namespace engine
