#pragma once

#include "runtime/system/system.hpp"

#include <memory>

struct ImGuiContext;

namespace engine
{

class EditorEnvironment;
class Engine;

class EditorSystem final : public System
{
public:
    // 环境由宿主拥有，须先初始化并保持就绪直到编辑器关闭。
    // runtime 可空；非空时必须先初始化，并与 App 一起存活到编辑器关闭。
    explicit EditorSystem(EditorEnvironment& environment, Engine* runtime = nullptr) noexcept;
    ~EditorSystem() noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    EditorEnvironment& environment_;
    Engine* runtime_ = nullptr;
    struct State;
    std::unique_ptr<State> state_;
    ImGuiContext* context_ = nullptr;
};

} // namespace engine
