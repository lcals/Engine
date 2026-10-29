#pragma once

#include "system.hpp"

struct ImGuiContext;

namespace engine
{

class ProjectSystem;

class EditorSystem final : public System
{
public:
    // 项目由宿主拥有，须先初始化并保持就绪直到编辑器关闭。
    explicit EditorSystem(ProjectSystem& project) noexcept;
    ~EditorSystem() noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    ProjectSystem& project_;
    ImGuiContext* context_ = nullptr;
};

} // namespace engine
