#pragma once

#include "runtime/animation/animation_retargeter.hpp"
#include "runtime/app/runtime_parallel_config.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/frame_status.hpp"
#include "runtime/resource/virtual_file_system.hpp"
#include "runtime/window/window_system.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

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
class MoviePlayer;
class ResourceCatalog;

enum class RuntimeStartupStatus
{
    preparing,
    ready,
    failed
};

// 工厂由宿主提供，创建尚未初始化的实例；返回空表示创建失败。
// 所有系统借用只用于玩法回调，宿主关闭它们前先结束玩法。
using ScriptRuntimeFactory = std::unique_ptr<ScriptRuntime> (*)(WorldSystem&, RenderSystem&,
                                                                UiSystem&, PhysicsSystem&,
                                                                NavigationSystem&, InputSystem&,
                                                                WindowSystem&, AnimationSystem&,
                                                                MoviePlayer*);

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
    // 完整运行预算由调用方读取配置后显式提交；可空依赖也须明确传入。
    [[nodiscard]] bool initialize(const EngineConfig& config, ResourceSystem* resources,
                                  ScriptRuntimeFactory script_factory, RuntimeSetup setup,
                                  ThreadSystem* threads, const RuntimeParallelConfig& parallel,
                                  AnimationRetargeterFactory retargeter_factory = nullptr);
    [[nodiscard]] bool initialize_presentation(const EngineConfig& config,
                                               ResourceSystem* resources, ThreadSystem* threads,
                                               const RuntimeParallelConfig& parallel);
    [[nodiscard]] bool begin_gameplay(ScriptRuntimeFactory script_factory, RuntimeSetup setup,
                                      AnimationRetargeterFactory retargeter_factory,
                                      std::shared_ptr<const ResourceCatalog> catalog,
                                      std::shared_ptr<const MountList> mounts,
                                      std::string_view entry = {}, std::string_view scene = {});
    [[nodiscard]] RuntimeStartupStatus step_gameplay(double max_seconds);
    [[nodiscard]] bool activate_gameplay();
    [[nodiscard]] FrameStatus tick(double delta_time);
    // 单独处理事件并发布输入，供不推进世界的宿主流程使用；每帧与 tick 二选一。
    [[nodiscard]] FrameStatus poll_events(double delta_time);
    // Borrowed, main-thread notification before normal native event routing. Cannot consume input.
    void set_event_observer(WindowEventHandler observer, void* context) noexcept;
    // 返回借用；仅在本宿主初始化成功至关闭前有效，其他状态返回空。
    [[nodiscard]] GameplaySession* gameplay() noexcept;
    [[nodiscard]] WorldSystem* world() noexcept;
    [[nodiscard]] WindowSystem* window() noexcept;
    [[nodiscard]] InputSystem* input() noexcept;
    [[nodiscard]] RenderSystem* renderer() noexcept;
    [[nodiscard]] UiSystem* ui() noexcept;
    // 返回本宿主独占任务域的借用，仅初始化成功至关闭前有效。
    [[nodiscard]] TaskSystem* tasks() noexcept;
    // 播放器由宿主独占；借用有效期至宿主关闭。
    [[nodiscard]] MoviePlayer* movie_player() noexcept;
    void request_exit() noexcept;
    void shutdown() noexcept;

private:
    [[nodiscard]] bool initialize_gameplay(const EngineConfig& config,
                                           ScriptRuntimeFactory script_factory, RuntimeSetup setup,
                                           AnimationRetargeterFactory retargeter_factory);
    void reset_gameplay() noexcept;
    Engine& engine_;
    std::unique_ptr<ThreadSystem> owned_threads_;
    std::unique_ptr<TaskSystem> tasks_;
    std::unique_ptr<TaskSystem> loading_tasks_;
    std::unique_ptr<TaskSystem> background_tasks_;
    std::unique_ptr<TaskSystem> physics_tasks_;
    std::unique_ptr<TaskSystem> animation_tasks_;
    std::unique_ptr<TaskSystem> navigation_tasks_;
    RuntimeParallelConfig parallel_;
    EngineConfig presentation_config_;
    ScriptRuntimeFactory startup_script_factory_ = nullptr;
    RuntimeSetup startup_setup_ = nullptr;
    AnimationRetargeterFactory startup_retargeter_factory_ = nullptr;
    std::shared_ptr<const ResourceCatalog> startup_catalog_;
    std::shared_ptr<const MountList> startup_mounts_;
    std::string startup_entry_;
    std::string startup_scene_;
    std::uint64_t presented_movie_id_ = 0;
    std::uint64_t presented_movie_generation_ = 0;
    RuntimeStartupStatus startup_status_ = RuntimeStartupStatus::failed;
    bool startup_requested_ = false;
    bool gameplay_active_ = false;
    ResourceSystem* resources_ = nullptr;
    std::unique_ptr<ResourceSystem> owned_resources_;
    std::unique_ptr<WindowSystem> window_;
    std::unique_ptr<InputSystem> input_;
    std::unique_ptr<RenderSystem> owned_renderer_;
    std::unique_ptr<AudioSystem> owned_audio_;
    std::unique_ptr<MoviePlayer> movie_player_;
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
    std::size_t completion_domain_ = 0;
    WindowEventHandler event_observer_ = nullptr;
    void* event_observer_context_ = nullptr;
};

} // namespace engine
