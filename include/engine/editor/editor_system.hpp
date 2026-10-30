#pragma once

#include "runtime/system/system.hpp"

#include <memory>

struct ImGuiContext;

namespace engine
{
class GameplaySession;
class WorldSystem;
} // namespace engine

namespace engine::editor
{

class EditorEnvironment;

class EditorCommands;
class SceneCommands;

class EditorSystem final : public System
{
public:
    // 环境由宿主拥有，须先初始化并保持就绪直到编辑器关闭。
    // gameplay 可空；非空时借用就绪会话，须与 App 一起存活到编辑器关闭。
    explicit EditorSystem(EditorEnvironment& environment, GameplaySession* gameplay = nullptr,
                          WorldSystem* world = nullptr) noexcept;
    ~EditorSystem() noexcept override;
    // 返回借用；初始化前、关闭后或未配置世界时返回空。
    [[nodiscard]] EditorCommands* commands() noexcept;
    [[nodiscard]] SceneCommands* scene_commands() noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    EditorEnvironment& environment_;
    GameplaySession* gameplay_ = nullptr;
    WorldSystem* world_ = nullptr;
    struct State;
    std::unique_ptr<State> state_;
    ImGuiContext* context_ = nullptr;
};

} // namespace engine::editor
