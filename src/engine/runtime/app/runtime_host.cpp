#include "runtime/app/runtime_host.hpp"

#include "runtime/animation/animation_system.hpp"
#include "runtime/audio/audio_system.hpp"
#include "runtime/engine.hpp"
#include "runtime/font/font_system.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/gameplay/script_runtime.hpp"
#include "runtime/navigation/navigation_system.hpp"
#include "runtime/performance/performance_system.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/system/system.hpp"
#include "runtime/ui/ui_system.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

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

bool RuntimeHost::initialize(const EngineConfig& config, ResourceSystem* resources,
                             ScriptRuntimeFactory script_factory, RuntimeSetup setup)
{
    if (initialized_ || owns_engine_ || engine_.is_initialized() || engine_.system_count() != 0 ||
        (resources != nullptr &&
         (!resources->is_initialized() || ResourceSystem::instance() != resources)))
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
    if (resources == nullptr)
    {
        std::unique_ptr<System> owned_resources = std::make_unique<ResourceSystem>();
        if (!engine_.add_system(owned_resources))
        {
            shutdown();
            return false;
        }
    }

    std::unique_ptr<System> performance = std::make_unique<PerformanceSystem>();
    if (!engine_.add_system(performance))
    {
        shutdown();
        return false;
    }

    auto world = std::make_unique<WorldSystem>();
    auto physics_system = std::make_unique<PhysicsSystem>(world.get());
    physics_ = physics_system.get();
    std::unique_ptr<System> physics = std::move(physics_system);
    auto audio_system = std::make_unique<AudioSystem>(world.get());
    audio_ = audio_system.get();
    std::unique_ptr<System> audio = std::move(audio_system);
    std::unique_ptr<System> navigation = std::make_unique<NavigationSystem>();
    auto animation_system = std::make_unique<AnimationSystem>(world.get());
    animation_ = animation_system.get();
    std::unique_ptr<System> animation = std::move(animation_system);
    auto font_system = std::make_unique<FontSystem>();
    fonts_ = font_system.get();
    std::unique_ptr<System> fonts = std::move(font_system);
    auto ui_system = std::make_unique<UiSystem>(*window_, *world, *fonts_);
    ui_ = ui_system.get();
    std::unique_ptr<System> ui = std::move(ui_system);
    if (!world->add_subsystem(physics) || !world->add_subsystem(navigation) ||
        !world->add_subsystem(animation) || !world->add_subsystem(fonts) ||
        !world->add_subsystem(ui) || !world->add_subsystem(audio))
    {
        shutdown();
        return false;
    }
    auto renderer = std::make_unique<RenderSystem>(*window_, world.get(), fonts_, ui_);
    ScriptRuntime* script = nullptr;
    if (script_factory != nullptr)
    {
        auto script_system = script_factory(*world, *renderer, *ui_);
        script = script_system.get();
        std::unique_ptr<System> script_base = std::move(script_system);
        if (!world->add_subsystem(script_base))
        {
            shutdown();
            return false;
        }
    }
    if (setup != nullptr && !setup(*world, *window_, *physics_))
    {
        shutdown();
        return false;
    }
    world_ = world.get();
    std::unique_ptr<System> world_base = std::move(world);
    if (!engine_.add_system(world_base))
    {
        shutdown();
        return false;
    }
    renderer_ = renderer.get();
    std::unique_ptr<System> renderer_base = std::move(renderer);
    if (!engine_.add_system(renderer_base) || !engine_.initialize(config))
    {
        shutdown();
        return false;
    }
    gameplay_ = std::make_unique<GameplaySession>(*world_, *renderer_, script, physics_, audio_,
                                                  animation_, fonts_, ui_);
    window_->set_event_handler([](void* context, const SDL_Event& event)
                               { return static_cast<UiSystem*>(context)->process_event(event); },
                               ui_);
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
    if (!audio_->set_paused(!window_->can_render()))
    {
        failed_ = true;
        return FrameStatus::failed;
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
    if (!gameplay_->tick())
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

WorldSystem* RuntimeHost::world() noexcept
{
    return initialized_ ? world_ : nullptr;
}

WindowSystem* RuntimeHost::window() noexcept
{
    return initialized_ ? window_.get() : nullptr;
}

RenderSystem* RuntimeHost::renderer() noexcept
{
    return initialized_ ? renderer_ : nullptr;
}

void RuntimeHost::request_exit() noexcept
{
    exit_requested_ = true;
}

void RuntimeHost::shutdown() noexcept
{
    // 先结束项目回调，再关闭其借用的系统，窗口最后销毁。
    if (window_)
    {
        window_->set_event_handler(nullptr, nullptr);
    }
    gameplay_.reset();
    fonts_ = nullptr;
    ui_ = nullptr;
    animation_ = nullptr;
    audio_ = nullptr;
    physics_ = nullptr;
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
