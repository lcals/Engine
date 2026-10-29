#include "runtime/engine.hpp"

#include "runtime/animation/animation_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/navigation/navigation_system.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/system/system.hpp"
#include "runtime/ui/ui_system.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#if ENGINE_HAS_V8
#include "runtime/script/script_system.hpp"
#endif

#include <cmath>
#include <cstdio>
#include <limits>

namespace engine
{

Engine::Engine() noexcept = default;

Engine::~Engine() noexcept
{
    shutdown();
}

Engine& Engine::instance() noexcept
{
    static Engine engine;
    return engine;
}

bool Engine::initialize(const EngineConfig& config)
{
    if (initialized_ || config.window_title.empty() || config.width == 0 || config.height == 0 ||
        config.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        config.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }

    shutdown();
    auto window = std::make_unique<WindowSystem>();
    window_ = window.get();
    systems_.push_back(std::move(window));
    systems_.push_back(std::make_unique<ResourceSystem>());

    auto world = std::make_unique<WorldSystem>();
    world_ = world.get();
    std::unique_ptr<System> physics = std::make_unique<PhysicsSystem>();
    std::unique_ptr<System> navigation = std::make_unique<NavigationSystem>();
    std::unique_ptr<System> animation = std::make_unique<AnimationSystem>();
    std::unique_ptr<System> ui = std::make_unique<UiSystem>();
    if (!world->add_subsystem(physics) || !world->add_subsystem(navigation) ||
        !world->add_subsystem(animation) || !world->add_subsystem(ui))
    {
        shutdown();
        return false;
    }
#if ENGINE_HAS_V8
    auto script = std::make_unique<ScriptSystem>(world_);
    script_ = script.get();
    std::unique_ptr<System> script_base = std::move(script);
    if (!world->add_subsystem(script_base))
    {
        shutdown();
        return false;
    }
#endif
    systems_.push_back(std::move(world));

    auto renderer = std::make_unique<RenderSystem>(*window_, world_);
    renderer_ = renderer.get();
    systems_.push_back(std::move(renderer));
    for (const auto& system : systems_)
    {
        if (!system->initialize(config))
        {
            std::fputs("Engine system initialization failed\n", stderr);
            shutdown();
            return false;
        }
    }
    initialized_ = true;
    return true;
}

FrameStatus Engine::tick(double delta_time)
{
    if (!initialized_ || failed_ || !std::isfinite(delta_time) || delta_time < 0.0)
    {
        return FrameStatus::failed;
    }
    if (exit_requested_)
    {
        return FrameStatus::exit_requested;
    }
    if (!window_->tick(delta_time))
    {
        failed_ = true;
        return FrameStatus::failed;
    }
    if (window_->exit_requested())
    {
        request_exit();
        return FrameStatus::exit_requested;
    }
    if (!window_->can_render())
    {
        return FrameStatus::paused;
    }
    for (const auto& system : systems_)
    {
        if (system.get() != window_ && !system->tick(delta_time))
        {
            failed_ = true;
            return FrameStatus::failed;
        }
    }
    if (!renderer_->render())
    {
        // 平台暂停请求可以在本帧开始之后到达；此时等待恢复，不锁定为渲染失败。
        if (!window_->can_render())
        {
            return FrameStatus::paused;
        }
        if (world_->scene() != nullptr)
        {
            last_error_ = "Project rendering failed; the project was stopped";
            stop_gameplay();
            if (renderer_->render())
            {
                return FrameStatus::running;
            }
        }
        failed_ = true;
        return FrameStatus::failed;
    }
    return FrameStatus::running;
}

void Engine::request_exit() noexcept
{
    exit_requested_ = true;
}

bool Engine::start_gameplay(std::string_view entry)
{
#if ENGINE_HAS_SCRIPT_RUNTIME
    last_error_.clear();
    if (!initialized_ || failed_ || script_ == nullptr)
    {
        last_error_ = "The runtime is not ready for gameplay";
        return false;
    }
    if (script_->status().running)
    {
        return true;
    }
    if (!script_->start_gameplay(entry))
    {
        last_error_ = script_->status().last_error;
        stop_gameplay();
        return false;
    }
    if (!renderer_->prepare_scene())
    {
        last_error_ = "Cannot prepare scene rendering resources";
        stop_gameplay();
        return false;
    }
    return true;
#else
    static_cast<void>(entry);
    last_error_ = "Script runtime is not enabled";
    return false;
#endif
}

void Engine::stop_gameplay() noexcept
{
#if ENGINE_HAS_SCRIPT_RUNTIME
    if (script_ != nullptr)
    {
        script_->stop_gameplay();
    }
#endif
    if (world_ != nullptr)
    {
        world_->destroy_scene();
    }
    if (renderer_ != nullptr)
    {
        renderer_->release_scene();
    }
}

void Engine::set_hot_reload(bool enabled)
{
#if ENGINE_HAS_SCRIPT_RUNTIME
    if (script_ != nullptr)
    {
        script_->set_hot_reload(enabled);
    }
#else
    static_cast<void>(enabled);
#endif
}

const ScriptStatus* Engine::script_status() const noexcept
{
#if ENGINE_HAS_SCRIPT_RUNTIME
    return script_ != nullptr ? &script_->status() : nullptr;
#else
    return nullptr;
#endif
}

std::size_t Engine::scene_node_count() const noexcept
{
    return world_ != nullptr && world_->scene() != nullptr ? world_->scene()->nodes().size() : 0;
}

std::uint32_t Engine::render_resource_count() const noexcept
{
    return renderer_ != nullptr ? renderer_->gpu_resource_count() : 0;
}

std::string_view Engine::last_error() const noexcept
{
    return last_error_;
}

void Engine::shutdown() noexcept
{
    // 项目退出回调执行期间，渲染和资源系统仍然可用。
    stop_gameplay();
    // 逐个反序销毁，保证渲染器析构期间借用的窗口仍然存在。
    while (!systems_.empty())
    {
        systems_.back()->shutdown();
        systems_.pop_back();
    }
    renderer_ = nullptr;
    script_ = nullptr;
    world_ = nullptr;
    last_error_.clear();
    window_ = nullptr;
    initialized_ = false;
    exit_requested_ = false;
    failed_ = false;
}

} // namespace engine
