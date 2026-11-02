#pragma once

#include "runtime/frame_status.hpp"
#include "runtime/app/runtime_parallel_config.hpp"

#include <memory>

namespace engine
{

class Engine;
struct EngineConfig;
class WindowSystem;
class InputSystem;
class WorldSystem;
class RenderSystem;
class PhysicsSystem;
class NavigationSystem;
class AudioSystem;
class AnimationSystem;
class FontSystem;
class UiSystem;
class GameplaySession;
class ResourceSystem;
class ScriptRuntime;
class ThreadSystem;
class TaskSystem;

// 工厂由宿主提供，创建尚未初始化的实例；返回空表示创建失败。
// 所有系统借用只用于玩法回调，宿主关闭它们前先结束玩法。
using ScriptRuntimeFactory = std::unique_ptr<ScriptRuntime> (*)(WorldSystem&, RenderSystem&,
                                                                UiSystem&, PhysicsSystem&,
                                                                NavigationSystem&, InputSystem&,
                                                                WindowSystem&);

// 只指定装配时的位置，逐帧调度继续使用 System 的子系统登记顺序。
enum class RuntimeStage
{
    before_physics,
    after_physics
};

// 世界初始化前按两个位置各调用一次；物理和导航已构造，失败按已有生命周期回滚。
using RuntimeSetup = bool (*)(RuntimeStage, WorldSystem&, WindowSystem&, PhysicsSystem&,
                              NavigationSystem&, RenderSystem&, TaskSystem&);

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
    // threads 可空；非空借用已初始化预算，调用方在宿主关闭后再关闭它。
    [[nodiscard]] bool initialize(const EngineConfig& config, ResourceSystem* resources = nullptr,
                                  ScriptRuntimeFactory script_factory = nullptr,
                                  RuntimeSetup setup = nullptr, ThreadSystem* threads = nullptr,
                                  const RuntimeParallelConfig& parallel = {});
    [[nodiscard]] FrameStatus tick(double delta_time);
    // 单独处理事件并发布输入，供不推进世界的宿主流程使用；每帧与 tick 二选一。
    [[nodiscard]] FrameStatus poll_events(double delta_time);
    // 返回借用；仅在本宿主初始化成功至关闭前有效，其他状态返回空。
    [[nodiscard]] GameplaySession* gameplay() noexcept;
    [[nodiscard]] WorldSystem* world() noexcept;
    [[nodiscard]] WindowSystem* window() noexcept;
    [[nodiscard]] InputSystem* input() noexcept;
    [[nodiscard]] RenderSystem* renderer() noexcept;
    // 返回本宿主独占任务域的借用，仅初始化成功至关闭前有效。
    [[nodiscard]] TaskSystem* tasks() noexcept;
    void request_exit() noexcept;
    void shutdown() noexcept;

private:
    Engine& engine_;
    std::unique_ptr<ThreadSystem> owned_threads_;
    std::unique_ptr<TaskSystem> tasks_;
    std::unique_ptr<TaskSystem> loading_tasks_;
    std::unique_ptr<TaskSystem> background_tasks_;
    std::unique_ptr<TaskSystem> physics_tasks_;
    std::unique_ptr<TaskSystem> animation_tasks_;
    std::unique_ptr<TaskSystem> navigation_tasks_;
    RuntimeParallelConfig parallel_;
    ResourceSystem* resources_ = nullptr;
    std::unique_ptr<WindowSystem> window_;
    std::unique_ptr<InputSystem> input_;
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
