#pragma once

#include "runtime/system/system.hpp"

#include <memory>

struct ImGuiContext;

namespace engine
{

class EditorEnvironment;
class GameplaySession;

class EditorSystem final : public System
{
public:
    // 环境由宿主拥有，须先初始化并保持就绪直到编辑器关闭。
    // gameplay 可空；非空时借用就绪会话，须与 App 一起存活到编辑器关闭。
    explicit EditorSystem(EditorEnvironment& environment,
                          GameplaySession* gameplay = nullptr) noexcept;
    ~EditorSystem() noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    EditorEnvironment& environment_;
    GameplaySession* gameplay_ = nullptr;
    struct State;
    std::unique_ptr<State> state_;
    ImGuiContext* context_ = nullptr;
};

} // namespace engine
