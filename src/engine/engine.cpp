#include "engine.hpp"

#include "animation_system.hpp"
#include "engine_config.hpp"
#include "navigation_system.hpp"
#include "physics_system.hpp"
#include "project_system.hpp"
#include "render_system.hpp"
#include "resource_system.hpp"
#include "system.hpp"
#include "ui_system.hpp"
#include "window_system.hpp"
#include "world_system.hpp"

#if ENGINE_HAS_V8
#include "script_system.hpp"
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
    if (initialized_ || config.project.empty() || config.window_title.empty() ||
        config.width == 0 || config.height == 0 ||
        config.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        config.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
    {
        return false;
    }

    shutdown();
    systems_.push_back(std::make_unique<ProjectSystem>());
    auto window = std::make_unique<WindowSystem>();
    window_ = window.get();
    systems_.push_back(std::move(window));
    systems_.push_back(std::make_unique<ResourceSystem>());

    auto world = std::make_unique<WorldSystem>();
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
    std::unique_ptr<System> script = std::make_unique<ScriptSystem>();
    if (!world->add_subsystem(script))
    {
        shutdown();
        return false;
    }
#endif
    systems_.push_back(std::move(world));

    auto renderer = std::make_unique<RenderSystem>(*window_);
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
        failed_ = true;
        return FrameStatus::failed;
    }
    return FrameStatus::running;
}

void Engine::request_exit() noexcept
{
    exit_requested_ = true;
}

void Engine::shutdown() noexcept
{
    // 逐个反序销毁，保证渲染器析构期间借用的窗口仍然存在。
    while (!systems_.empty())
    {
        systems_.back()->shutdown();
        systems_.pop_back();
    }
    renderer_ = nullptr;
    window_ = nullptr;
    initialized_ = false;
    exit_requested_ = false;
    failed_ = false;
}

} // namespace engine
