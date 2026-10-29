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
class GameplaySession;
class ResourceSystem;
class ScriptRuntime;

// 工厂由宿主提供，创建尚未初始化的实例；返回空表示创建失败。
using ScriptRuntimeFactory = std::unique_ptr<ScriptRuntime> (*)(WorldSystem&);

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
                                  ScriptRuntimeFactory script_factory = nullptr);
    [[nodiscard]] FrameStatus tick(double delta_time);
    // 返回借用；仅在本宿主初始化成功至关闭前有效，其他状态返回空。
    [[nodiscard]] GameplaySession* gameplay() noexcept;
    [[nodiscard]] WorldSystem* world() noexcept;
    [[nodiscard]] RenderSystem* renderer() noexcept;
    void request_exit() noexcept;
    void shutdown() noexcept;

private:
    Engine& engine_;
    std::unique_ptr<WindowSystem> window_;
    std::unique_ptr<GameplaySession> gameplay_;
    WorldSystem* world_ = nullptr;
    RenderSystem* renderer_ = nullptr;
    bool owns_engine_ = false;
    bool initialized_ = false;
    bool exit_requested_ = false;
    bool failed_ = false;
};

} // namespace engine
