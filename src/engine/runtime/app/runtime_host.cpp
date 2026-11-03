#include "runtime/app/runtime_host.hpp"

#include "runtime/animation/animation_system.hpp"
#include "runtime/audio/audio_system.hpp"
#include "runtime/engine.hpp"
#include "runtime/font/font_system.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/gameplay/script_runtime.hpp"
#include "runtime/input/input_system.hpp"
#include "runtime/navigation/navigation_system.hpp"
#include "runtime/performance/performance_system.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/system/system.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"
#include "runtime/ui/ui_system.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#include <chrono>
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
                             ScriptRuntimeFactory script_factory, RuntimeSetup setup,
                             ThreadSystem* threads, const RuntimeParallelConfig& parallel)
{
    if (initialized_ || owns_engine_ || engine_.is_initialized() || engine_.system_count() != 0 ||
        (resources != nullptr &&
         (!resources->is_initialized() || ResourceSystem::instance() != resources)) ||
        (threads != nullptr && !threads->is_initialized()) ||
        (resources != nullptr && resources->async_enabled()) ||
        !std::isfinite(parallel.completion_budget_seconds) ||
        parallel.completion_budget_seconds < 0.0)
    {
        return false;
    }
    owns_engine_ = true;
    parallel_ = parallel;
    if (threads == nullptr)
    {
        owned_threads_ = std::make_unique<ThreadSystem>(parallel.threads);
        if (!owned_threads_->initialize(config))
        {
            shutdown();
            return false;
        }
        threads = owned_threads_.get();
    }
    const auto create_domain = [&](std::unique_ptr<TaskSystem>& domain, TaskConfig settings)
    {
        if (settings.max_concurrency == 0)
        {
            settings.max_concurrency = threads->worker_count();
        }
        domain = std::make_unique<TaskSystem>(*threads, settings);
        return domain->initialize(config);
    };
    if (!create_domain(tasks_, parallel.runtime) ||
        !create_domain(loading_tasks_, parallel.loading) ||
        !create_domain(background_tasks_, parallel.background) ||
        !create_domain(physics_tasks_, parallel.physics) ||
        !create_domain(animation_tasks_, parallel.animation) ||
        !create_domain(navigation_tasks_, parallel.navigation))
    {
        shutdown();
        return false;
    }
    window_ = std::make_unique<WindowSystem>();
    if (!window_->initialize(config))
    {
        shutdown();
        return false;
    }
    input_ = std::make_unique<InputSystem>(*window_);
    if (!input_->initialize(config))
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

    auto world = std::make_unique<WorldSystem>(parallel.scene_restore);
    auto physics_system = std::make_unique<PhysicsSystem>(world.get(), physics_tasks_.get());
    physics_ = physics_system.get();
    std::unique_ptr<System> physics = std::move(physics_system);
    auto audio_system = std::make_unique<AudioSystem>(world.get());
    audio_ = audio_system.get();
    std::unique_ptr<System> audio = std::move(audio_system);
    auto navigation_system = std::make_unique<NavigationSystem>(
        navigation_tasks_.get(), parallel.navigation_tile_threshold);
    auto* navigation_ptr = navigation_system.get();
    std::unique_ptr<System> navigation = std::move(navigation_system);
    auto animation_system = std::make_unique<AnimationSystem>(
        world.get(), animation_tasks_.get(), parallel.animation_sampling);
    animation_ = animation_system.get();
    std::unique_ptr<System> animation = std::move(animation_system);
    auto font_system = std::make_unique<FontSystem>();
    fonts_ = font_system.get();
    std::unique_ptr<System> fonts = std::move(font_system);
    auto ui_system = std::make_unique<UiSystem>(*window_, *world, *fonts_);
    ui_ = ui_system.get();
    std::unique_ptr<System> ui = std::move(ui_system);
    // 依赖先构造；登记顺序同时决定统一初始化、每帧更新及反序关闭。
    if (!world->add_subsystem(fonts) || !world->add_subsystem(ui) ||
        !world->add_subsystem(navigation))
    {
        shutdown();
        return false;
    }
    auto renderer = std::make_unique<RenderSystem>(*window_, world.get(), fonts_, ui_,
                                                   loading_tasks_.get(), parallel.render);
    ScriptRuntime* script = nullptr;
    if (script_factory != nullptr)
    {
        auto script_system =
            script_factory(*world, *renderer, *ui_, *physics_, *navigation_ptr, *input_, *window_);
        script = script_system.get();
        std::unique_ptr<System> script_base = std::move(script_system);
        if (!world->add_subsystem(script_base))
        {
            shutdown();
            return false;
        }
    }
    if ((setup != nullptr && !setup(RuntimeStage::before_physics, *world, *window_, *physics_,
                                    *navigation_ptr, *renderer, *background_tasks_)) ||
        !world->add_subsystem(physics) || !world->add_subsystem(animation) ||
        (setup != nullptr && !setup(RuntimeStage::after_physics, *world, *window_, *physics_,
                                    *navigation_ptr, *renderer, *background_tasks_)) ||
        !world->add_subsystem(audio))
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
    resources_ = resources != nullptr ? resources : ResourceSystem::instance();
    if (resources_ == nullptr || !resources_->configure_async(*loading_tasks_, parallel.resources))
    {
        resources_ = nullptr;
        shutdown();
        return false;
    }
    gameplay_ = std::make_unique<GameplaySession>(*world_, *renderer_, script, physics_, audio_,
                                                  animation_, fonts_, ui_, background_tasks_.get());
    window_->set_event_handler(
        [](void* context, const SDL_Event& event)
        {
            auto& host = *static_cast<RuntimeHost*>(context);
            if (host.event_observer_ != nullptr)
            {
                host.event_observer_(host.event_observer_context_, event);
            }
            const bool owned = host.input_->owns_pointer(event);
            const bool claimed = !owned && host.ui_->process_event(event);
            schema::InputRoutingDataT routing;
            routing.keyboard_claimed = host.ui_->keyboard_claimed();
            routing.pointer_claimed = host.ui_->pointer_claimed();
            if (!host.input_->set_routing_state(routing) ||
                !host.input_->process_event(event, claimed))
            {
                host.failed_ = true;
            }
        },
        this);
    initialized_ = true;
    return true;
}

FrameStatus RuntimeHost::tick(double delta_time)
{
    ENGINE_PROFILE_ZONE("Host.runtime_tick");
    const FrameStatus events = poll_events(delta_time);
    if (events != FrameStatus::running)
    {
        return events;
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
    // Gameplay 可在本帧切换世界暂停；窗口不可绘制仍是独立的暂停原因。
    if (!audio_->set_paused(!window_->can_render() || world_->paused()))
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
        if (!world_->scenes().empty())
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

FrameStatus RuntimeHost::poll_events(double delta_time)
{
    if (!initialized_ || failed_ || !std::isfinite(delta_time) || delta_time < 0.0)
    {
        return FrameStatus::failed;
    }
    if (exit_requested_)
    {
        return FrameStatus::exit_requested;
    }
    const auto completions_started = std::chrono::steady_clock::now();
    std::size_t remaining = parallel_.completions_per_frame;
    for (auto* domain : {tasks_.get(), physics_tasks_.get(), animation_tasks_.get(),
                        navigation_tasks_.get(), background_tasks_.get(), loading_tasks_.get()})
    {
        const double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - completions_started).count();
        if (remaining == 0 || elapsed >= parallel_.completion_budget_seconds)
        {
            break;
        }
        remaining -= domain->dispatch_completions(
            remaining, parallel_.completion_budget_seconds - elapsed);
    }
    resources_->poll_async();
    // 上一回调可能改变文档 / 可见性；没有 SDL 事件也必须刷新持续占用事实。
    if (window_->can_render() && !ui_->synchronize())
    {
        failed_ = true;
        return FrameStatus::failed;
    }
    schema::InputRoutingDataT routing;
    routing.keyboard_claimed = ui_->keyboard_claimed();
    routing.pointer_claimed = ui_->pointer_claimed();
    if (!input_->begin_frame() || !input_->set_routing_state(routing) ||
        !window_->tick(delta_time) || failed_ || !input_->tick(delta_time))
    {
        failed_ = true;
        return FrameStatus::failed;
    }
    if (window_->exit_requested())
    {
        request_exit();
        return FrameStatus::exit_requested;
    }
    if (!audio_->set_paused(!window_->can_render() || world_->paused()))
    {
        failed_ = true;
        return FrameStatus::failed;
    }
    return window_->can_render() ? FrameStatus::running : FrameStatus::paused;
}

void RuntimeHost::set_event_observer(WindowEventHandler observer, void* context) noexcept
{
    event_observer_ = observer;
    event_observer_context_ = observer == nullptr ? nullptr : context;
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

InputSystem* RuntimeHost::input() noexcept
{
    return initialized_ ? input_.get() : nullptr;
}

RenderSystem* RuntimeHost::renderer() noexcept
{
    return initialized_ ? renderer_ : nullptr;
}

UiSystem* RuntimeHost::ui() noexcept
{
    return initialized_ ? ui_ : nullptr;
}

void RuntimeHost::request_exit() noexcept
{
    exit_requested_ = true;
}

TaskSystem* RuntimeHost::tasks() noexcept
{
    return initialized_ ? tasks_.get() : nullptr;
}

void RuntimeHost::shutdown() noexcept
{
    set_event_observer(nullptr, nullptr);
    // 先结束项目回调，再关闭其借用的系统，窗口最后销毁。
    if (window_)
    {
        window_->set_event_handler(nullptr, nullptr);
    }
    gameplay_.reset();
    if (resources_ != nullptr)
    {
        resources_->shutdown_async();
        resources_ = nullptr;
    }
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
    input_.reset();
    window_.reset();
    navigation_tasks_.reset();
    animation_tasks_.reset();
    physics_tasks_.reset();
    background_tasks_.reset();
    loading_tasks_.reset();
    tasks_.reset();
    owned_threads_.reset();
    initialized_ = false;
    exit_requested_ = false;
    failed_ = false;
}

} // namespace engine
