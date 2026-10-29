#include "runtime/app/runtime_host.hpp"

#include "runtime/animation/animation_system.hpp"
#include "runtime/engine.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/navigation/navigation_system.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/system/system.hpp"
#include "runtime/ui/ui_system.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#if ENGINE_HAS_V8
#include "runtime/script/script_system.hpp"
#endif

#include <cmath>
#include <utility>

namespace engine
{

RuntimeHost::RuntimeHost() noexcept : engine_(Engine::instance())
{
}

RuntimeHost::~RuntimeHost() noexcept
{
    shutdown();
}

bool RuntimeHost::initialize(const EngineConfig& config)
{
    if (initialized_ || owns_engine_ || engine_.is_initialized() || engine_.system_count() != 0)
    {
        return false;
    }
    owns_engine_ = true;
    window_ = std::make_unique<WindowSystem>();
    if (!window_->initialize(config))
    {
        shutdown();
        return false;
    }
    std::unique_ptr<System> resources = std::make_unique<ResourceSystem>();
    if (!engine_.add_system(resources))
    {
        shutdown();
        return false;
    }

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
    ScriptSystem* script = nullptr;
#if ENGINE_HAS_V8
    auto script_system = std::make_unique<ScriptSystem>(world.get());
    script = script_system.get();
    std::unique_ptr<System> script_base = std::move(script_system);
    if (!world->add_subsystem(script_base))
    {
        shutdown();
        return false;
    }
#endif
    world_ = world.get();
    std::unique_ptr<System> world_base = std::move(world);
    if (!engine_.add_system(world_base))
    {
        shutdown();
        return false;
    }
    auto renderer = std::make_unique<RenderSystem>(*window_, world_);
    renderer_ = renderer.get();
    std::unique_ptr<System> renderer_base = std::move(renderer);
    if (!engine_.add_system(renderer_base) || !engine_.initialize(config))
    {
        shutdown();
        return false;
    }
    gameplay_ = std::make_unique<GameplaySession>(*world_, *renderer_, script);
    initialized_ = true;
    return true;
}

FrameStatus RuntimeHost::tick(double delta_time)
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
    if (!engine_.tick(delta_time))
    {
        failed_ = true;
        return FrameStatus::failed;
    }
    if (!renderer_->render())
    {
        // 平台暂停可以在本帧检查之后到达，等待恢复而不锁定为失败。
        if (!window_->can_render())
        {
            return FrameStatus::paused;
        }
        if (world_->scene() != nullptr)
        {
            gameplay_->stop("Project rendering failed; the project was stopped");
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

GameplaySession* RuntimeHost::gameplay() noexcept
{
    return gameplay_.get();
}

void RuntimeHost::request_exit() noexcept
{
    exit_requested_ = true;
}

void RuntimeHost::shutdown() noexcept
{
    // 先结束项目回调，再关闭其借用的系统，窗口最后销毁。
    gameplay_.reset();
    renderer_ = nullptr;
    world_ = nullptr;
    if (owns_engine_)
    {
        engine_.shutdown();
        owns_engine_ = false;
    }
    window_.reset();
    initialized_ = false;
    exit_requested_ = false;
    failed_ = false;
}

} // namespace engine
