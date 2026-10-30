#pragma once

#include "runtime/frame_status.hpp"

#include <memory>

namespace engine
{

class Engine;
struct EngineConfig;
class WindowSystem;
class WorldSystem;
class RenderSystem;
class PhysicsSystem;
class AudioSystem;
class AnimationSystem;
class FontSystem;
class UiSystem;
class GameplaySession;
class ResourceSystem;
class ScriptRuntime;

// 工厂由宿主提供，创建尚未初始化的实例；返回空表示创建失败。
// renderer 和 ui 借用只用于玩法回调，宿主关闭它们前先结束玩法。
using ScriptRuntimeFactory = std::unique_ptr<ScriptRuntime> (*)(WorldSystem&, RenderSystem&,
                                                                UiSystem&);

// 宿主在世界初始化前追加可选子系统；失败按已有生命周期回滚。
using RuntimeSetup = bool (*)(WorldSystem&, WindowSystem&, PhysicsSystem&);

class RuntimeHost final
{
public:
    RuntimeHost() noexcept;
    ~RuntimeHost() noexcept;

    RuntimeHost(const RuntimeHost&) = delete;
    RuntimeHost& operator=(const RuntimeHost&) = delete;
    RuntimeHost(RuntimeHost&&) = delete;
    RuntimeHost& operator=(RuntimeHost&&) = delete;

    // resources 可空；非空时借用已初始化的资源核心，调用方须在宿主关闭后再释放它。
    // script_factory 可空；实例交给世界独占，核心不选择具体脚本插件。
    [[nodiscard]] bool initialize(const EngineConfig& config, ResourceSystem* resources = nullptr,
                                  ScriptRuntimeFactory script_factory = nullptr,
                                  RuntimeSetup setup = nullptr);
    [[nodiscard]] FrameStatus tick(double delta_time);
    // 返回借用；仅在本宿主初始化成功至关闭前有效，其他状态返回空。
    [[nodiscard]] GameplaySession* gameplay() noexcept;
    [[nodiscard]] WorldSystem* world() noexcept;
    [[nodiscard]] WindowSystem* window() noexcept;
    [[nodiscard]] RenderSystem* renderer() noexcept;
    void request_exit() noexcept;
    void shutdown() noexcept;

private:
    Engine& engine_;
    std::unique_ptr<WindowSystem> window_;
    std::unique_ptr<GameplaySession> gameplay_;
    WorldSystem* world_ = nullptr;
    RenderSystem* renderer_ = nullptr;
    PhysicsSystem* physics_ = nullptr;
    AudioSystem* audio_ = nullptr;
    AnimationSystem* animation_ = nullptr;
    FontSystem* fonts_ = nullptr;
    UiSystem* ui_ = nullptr;
    bool owns_engine_ = false;
    bool initialized_ = false;
    bool exit_requested_ = false;
    bool failed_ = false;
};

} // namespace engine
