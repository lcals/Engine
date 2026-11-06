#include "runtime/app/runtime_host.hpp"

#include "runtime/animation/animation_system.hpp"
#include "runtime/audio/audio_system.hpp"
#include "runtime/engine.hpp"
#include "runtime/font/font_system.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/gameplay/script_runtime.hpp"
#include "runtime/input/input_system.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/movie/movie_player.hpp"
#include "runtime/navigation/navigation_system.hpp"
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

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <utility>

namespace engine
{
namespace
{
// 无可显示影片时仍提交黑帧，直至候选场景被正式激活。
MovieFrame startup_black_frame()
{
    static constexpr std::array<std::uint8_t, 4> luma{16, 16, 16, 16};
    static constexpr std::array<std::uint8_t, 2> chroma{128, 128};
    MovieFrame frame;
    frame.width = 2;
    frame.height = 2;
    frame.format = schema::MoviePixelFormat::nv12;
    frame.color =
        schema::MovieColorInfo(schema::MovieColorPrimaries::bt709, schema::MovieTransfer::srgb,
                               schema::MovieColorMatrix::bt709, schema::MovieColorRange::limited,
                               schema::MovieChromaLocation::center);
    frame.planes[0] = {luma, 2, 2, 2};
    frame.planes[1] = {chroma, 1, 1, 2};
    return frame;
}
} // namespace

RuntimeHost::RuntimeHost() noexcept : engine_(Engine::instance())
{
}

RuntimeHost::~RuntimeHost() noexcept
{
    shutdown();
}

bool RuntimeHost::initialize(const EngineConfig& config, ResourceSystem* resources,
                             ScriptRuntimeFactory script_factory, RuntimeSetup setup,
                             ThreadSystem* threads, const RuntimeParallelConfig& parallel,
                             AnimationRetargeterFactory retargeter_factory)
{
    if (!initialize_presentation(config, resources, threads, parallel))
    {
        return false;
    }
    if (!initialize_gameplay(config, script_factory, setup, retargeter_factory) ||
        !audio_->set_world(world_))
    {
        shutdown();
        return false;
    }
    gameplay_active_ = true;
    return true;
}

bool RuntimeHost::initialize_presentation(const EngineConfig& config, ResourceSystem* resources,
                                          ThreadSystem* threads,
                                          const RuntimeParallelConfig& parallel)
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
    presentation_config_ = config;
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
        owned_resources_ = std::make_unique<ResourceSystem>();
        if (!owned_resources_->initialize(config))
        {
            shutdown();
            return false;
        }
        resources = owned_resources_.get();
    }

    owned_renderer_ = std::make_unique<RenderSystem>(*window_, nullptr, nullptr, nullptr,
                                                     loading_tasks_.get(), parallel.render);
    renderer_ = owned_renderer_.get();
    owned_audio_ = std::make_unique<AudioSystem>();
    audio_ = owned_audio_.get();
    if (!owned_renderer_->initialize(config) || !owned_audio_->initialize(config))
    {
        shutdown();
        return false;
    }
    resources_ = resources;
    if (!resources_->configure_async(*loading_tasks_, parallel.resources))
    {
        resources_ = nullptr;
        shutdown();
        return false;
    }
    movie_player_ = std::make_unique<MoviePlayer>(*resources_, *loading_tasks_, *audio_);

    window_->set_event_handler(
        [](void* context, const SDL_Event& event)
        {
            auto& host = *static_cast<RuntimeHost*>(context);
            if (host.event_observer_ != nullptr)
            {
                host.event_observer_(host.event_observer_context_, event);
            }
            const bool owned = host.input_->owns_pointer(event);
            const bool claimed =
                !owned && host.gameplay_active_ && host.ui_ && host.ui_->process_event(event);
            schema::InputRoutingDataT routing;
            routing.keyboard_claimed =
                host.gameplay_active_ && host.ui_ && host.ui_->keyboard_claimed();
            routing.pointer_claimed =
                host.gameplay_active_ && host.ui_ && host.ui_->pointer_claimed();
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

bool RuntimeHost::initialize_gameplay(const EngineConfig& config,
                                      ScriptRuntimeFactory script_factory, RuntimeSetup setup,
                                      AnimationRetargeterFactory retargeter_factory)
{
#if defined(ENGINE_ENABLE_TRACY)
    tracy::SetThreadName("Engine main");
#endif

    auto world = std::make_unique<WorldSystem>(parallel_.scene_restore);
    auto physics_system = std::make_unique<PhysicsSystem>(world.get(), physics_tasks_.get());
    physics_ = physics_system.get();
    std::unique_ptr<System> physics = std::move(physics_system);
    auto navigation_system = std::make_unique<NavigationSystem>(
        navigation_tasks_.get(), parallel_.navigation_tile_threshold);
    auto* navigation_ptr = navigation_system.get();
    std::unique_ptr<System> navigation = std::move(navigation_system);
    auto retargeter = retargeter_factory ? retargeter_factory() : nullptr;
    if (retargeter_factory && !retargeter)
    {
        reset_gameplay();
        return false;
    }
    auto animation_system = std::make_unique<AnimationSystem>(
        world.get(), animation_tasks_.get(), parallel_.animation_sampling, std::move(retargeter));
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
        reset_gameplay();
        return false;
    }
    ScriptRuntime* script = nullptr;
    if (script_factory != nullptr)
    {
        auto script_system = script_factory(*world, *renderer_, *ui_, *physics_, *navigation_ptr,
                                            *input_, *window_, *animation_, movie_player_.get());
        script = script_system.get();
        std::unique_ptr<System> script_base = std::move(script_system);
        if (!world->add_subsystem(script_base))
        {
            reset_gameplay();
            return false;
        }
    }
    if ((setup != nullptr && !setup(RuntimeStage::before_physics, *world, *window_, *physics_,
                                    *navigation_ptr, *renderer_, *background_tasks_)) ||
        !world->add_subsystem(physics) || !world->add_subsystem(animation) ||
        (setup != nullptr && !setup(RuntimeStage::after_physics, *world, *window_, *physics_,
                                    *navigation_ptr, *renderer_, *background_tasks_)))
    {
        reset_gameplay();
        return false;
    }
    world_ = world.get();
    std::unique_ptr<System> world_base = std::move(world);
    if (!engine_.add_system(world_base))
    {
        reset_gameplay();
        return false;
    }
    if (!engine_.initialize(config) || !renderer_->bind_world(world_, fonts_, ui_))
    {
        reset_gameplay();
        return false;
    }
    gameplay_ = std::make_unique<GameplaySession>(*world_, *renderer_, script, physics_, audio_,
                                                  animation_, fonts_, ui_, background_tasks_.get());
    return true;
}

bool RuntimeHost::begin_gameplay(ScriptRuntimeFactory script_factory, RuntimeSetup setup,
                                 AnimationRetargeterFactory retargeter_factory,
                                 std::shared_ptr<const ResourceCatalog> catalog,
                                 std::shared_ptr<const MountList> mounts, std::string_view entry,
                                 std::string_view scene)
{
    if (!initialized_ || startup_requested_ || gameplay_ || !catalog || !mounts || !resources_ ||
        catalog->revision().empty() || catalog->revision() != resources_->catalog().revision() ||
        catalog->data().target_platform != resources_->catalog().data().target_platform)
    {
        return false;
    }
    startup_script_factory_ = script_factory;
    startup_setup_ = setup;
    startup_retargeter_factory_ = retargeter_factory;
    startup_catalog_ = std::move(catalog);
    startup_mounts_ = std::move(mounts);
    startup_entry_ = entry;
    startup_scene_ = scene;
    startup_status_ = RuntimeStartupStatus::preparing;
    startup_requested_ = true;
    return true;
}

RuntimeStartupStatus RuntimeHost::step_gameplay(double max_seconds)
{
    if (!initialized_ || !startup_requested_ || !std::isfinite(max_seconds) || max_seconds <= 0)
    {
        return RuntimeStartupStatus::failed;
    }
    if (startup_status_ != RuntimeStartupStatus::preparing)
    {
        return startup_status_;
    }
    if (!gameplay_)
    {
        MoviePlaybackHandle current;
        if (movie_player_->current_handle(current) &&
            (current.id != presented_movie_id_ ||
             current.generation != presented_movie_generation_))
        {
            return RuntimeStartupStatus::preparing;
        }
        const auto started = std::chrono::steady_clock::now();
        const bool ready = initialize_gameplay(presentation_config_, startup_script_factory_,
                                               startup_setup_, startup_retargeter_factory_);
        LogSystem::write(LogLevel::info,
                         "Startup world initialization seconds: " +
                             std::to_string(std::chrono::duration<double>(
                                                std::chrono::steady_clock::now() - started)
                                                .count()));
        if (!ready || !gameplay_->begin_prepare(*startup_catalog_, *startup_mounts_, startup_entry_,
                                                startup_scene_))
        {
            reset_gameplay();
            startup_status_ = RuntimeStartupStatus::failed;
        }
        return startup_status_;
    }
    const auto result = gameplay_->step_prepare(parallel_.resources.completion_budget, max_seconds);
    if (result == GameplayPrepareStatus::ready)
    {
        startup_status_ = RuntimeStartupStatus::ready;
    }
    else if (result == GameplayPrepareStatus::failed)
    {
        reset_gameplay();
        startup_status_ = RuntimeStartupStatus::failed;
    }
    return startup_status_;
}

bool RuntimeHost::activate_gameplay()
{
    if (!initialized_ || startup_status_ != RuntimeStartupStatus::ready || !gameplay_ ||
        !gameplay_->activate_prepared() || !audio_->set_world(world_))
    {
        return false;
    }
    gameplay_active_ = true;
    startup_catalog_.reset();
    startup_mounts_.reset();
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
    const double movie_delta = std::exchange(movie_clock_paused_, false) ? 0.0 : delta_time;
    if (gameplay_active_ && !engine_.tick(std::min(delta_time, 0.1)))
    {
        failed_ = true;
        return FrameStatus::failed;
    }
    if (!audio_->tick(delta_time))
    {
        failed_ = true;
        return FrameStatus::failed;
    }
    if (gameplay_active_ && gameplay_ && !gameplay_->tick())
    {
        failed_ = true;
        return FrameStatus::failed;
    }
    // 窗口不可绘制时暂停整个设备；世界暂停由音频系统只作用于场景声部。
    if (!audio_->set_paused(!window_->can_render()))
    {
        failed_ = true;
        return FrameStatus::failed;
    }
    if (!movie_player_->update(movie_delta))
    {
        failed_ = true;
        return FrameStatus::failed;
    }
    MoviePlaybackHandle movie_handle;
    MoviePlaybackStatus movie_status;
    MovieFrame movie_frame;
    const bool showing_movie =
        movie_player_->current_handle(movie_handle) &&
        movie_player_->status(movie_handle, movie_status) &&
        (movie_status.state == MovieState::playing || movie_status.state == MovieState::paused ||
         movie_status.state == MovieState::completed) &&
        movie_player_->frame(movie_handle, movie_frame);
    const bool rendered = showing_movie ? renderer_->present_movie_frame(movie_frame)
                          : !gameplay_active_
                              ? renderer_->present_movie_frame(startup_black_frame())
                              : renderer_->render();
    if (!rendered)
    {
        // 平台暂停可以在本帧检查之后到达，等待恢复而不锁定为失败。
        if (!window_->can_render())
        {
            movie_clock_paused_ = true;
            return FrameStatus::paused;
        }
        if (!showing_movie && gameplay_active_ && world_ && gameplay_ && !world_->scenes().empty())
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
    if (showing_movie)
    {
        presented_movie_id_ = movie_handle.id;
        presented_movie_generation_ = movie_handle.generation;
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
    const std::array domains{tasks_.get(),
                             physics_tasks_.get(),
                             animation_tasks_.get(),
                             navigation_tasks_.get(),
                             background_tasks_.get(),
                             loading_tasks_.get()};
    std::size_t idle_domains = 0;
    while (remaining != 0 && idle_domains < domains.size())
    {
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - completions_started)
                .count();
        if (remaining == 0 || elapsed >= parallel_.completion_budget_seconds)
        {
            break;
        }
        auto* domain = domains[completion_domain_];
        completion_domain_ = (completion_domain_ + 1) % domains.size();
        const auto completed =
            domain->dispatch_completions(1, parallel_.completion_budget_seconds - elapsed);
        remaining -= completed;
        idle_domains = completed == 0 ? idle_domains + 1 : 0;
    }
    resources_->poll_async();
    // 上一回调可能改变文档 / 可见性；没有 SDL 事件也必须刷新持续占用事实。
    if (window_->can_render() && gameplay_active_ && ui_ && !ui_->synchronize())
    {
        failed_ = true;
        return FrameStatus::failed;
    }
    schema::InputRoutingDataT routing;
    routing.keyboard_claimed = gameplay_active_ && ui_ && ui_->keyboard_claimed();
    routing.pointer_claimed = gameplay_active_ && ui_ && ui_->pointer_claimed();
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
    if (!audio_->set_paused(!window_->can_render()))
    {
        failed_ = true;
        return FrameStatus::failed;
    }
    if (!window_->can_render())
    {
        movie_clock_paused_ = true;
        return FrameStatus::paused;
    }
    return FrameStatus::running;
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

MoviePlayer* RuntimeHost::movie_player() noexcept
{
    return initialized_ ? movie_player_.get() : nullptr;
}

void RuntimeHost::reset_gameplay() noexcept
{
    gameplay_active_ = false;
    gameplay_.reset();
    if (renderer_ != nullptr && renderer_->is_initialized())
    {
        (void)renderer_->bind_world(nullptr, nullptr, nullptr);
    }
    if (audio_ != nullptr && audio_->is_initialized())
    {
        (void)audio_->set_world(nullptr);
    }
    fonts_ = nullptr;
    ui_ = nullptr;
    animation_ = nullptr;
    physics_ = nullptr;
    world_ = nullptr;
    if (owns_engine_)
    {
        engine_.shutdown();
    }
}

void RuntimeHost::shutdown() noexcept
{
    set_event_observer(nullptr, nullptr);
    // 先结束项目回调，再关闭其借用的系统，窗口最后销毁。
    if (window_)
    {
        window_->set_event_handler(nullptr, nullptr);
    }
    reset_gameplay();
    owns_engine_ = false;
    movie_player_.reset();
    if (resources_ != nullptr)
    {
        resources_->shutdown_async();
        resources_ = nullptr;
    }
    owned_audio_.reset();
    owned_renderer_.reset();
    audio_ = nullptr;
    renderer_ = nullptr;
    owned_resources_.reset();
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
    completion_domain_ = 0;
    startup_requested_ = false;
    startup_status_ = RuntimeStartupStatus::failed;
    startup_catalog_.reset();
    startup_mounts_.reset();
    startup_entry_.clear();
    startup_scene_.clear();
    presented_movie_id_ = 0;
    presented_movie_generation_ = 0;
    movie_clock_paused_ = false;
}

} // namespace engine
