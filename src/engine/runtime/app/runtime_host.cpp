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

#include <SDL3/SDL_init.h>

#include <algorithm>
#include <array>
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
                             ResourceLoader* loader, ScriptRuntimeFactory script_factory,
                             RuntimeSetup setup, ThreadSystem* threads,
                             const RuntimeParallelConfig& parallel,
                             AnimationRetargeterFactory retargeter_factory)
{
    if (!initialize_presentation(config, resources, loader, threads, parallel))
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
                                          ResourceLoader* loader, ThreadSystem* threads,
                                          const RuntimeParallelConfig& parallel)
{
    if (initialized_ || owns_engine_ || engine_.is_initialized() || engine_.system_count() != 0 ||
        (resources != nullptr && !resources->is_initialized()) ||
        (threads != nullptr && !threads->is_initialized()) ||
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
        !create_domain(render_tasks_, parallel.render) ||
        !create_domain(movie_tasks_, parallel.movie) ||
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
                                                     render_tasks_.get(), parallel.rendering);
    renderer_ = owned_renderer_.get();
    owned_audio_ = std::make_unique<AudioSystem>(loader);
    audio_ = owned_audio_.get();
    if (!owned_renderer_->initialize(config) || !owned_audio_->initialize(config))
    {
        shutdown();
        return false;
    }
    resources_ = resources;
    loader_ = loader;
    if (loader_)
    {
        movie_player_ = std::make_unique<MoviePlayer>(*loader_, *movie_tasks_, *audio_);
    }

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
                if (!host.failed_)
                {
                    host.error_code_.clear();
                    host.error_ = host.input_->last_error();
                }
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

    auto world = std::make_unique<WorldSystem>(loader_, parallel_.scene_restore);
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
    auto font_system = std::make_unique<FontSystem>(loader_);
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
    if (!engine_.initialize(config) ||
        (setup && !setup(RuntimeStage::initialized, *world_, *window_, *physics_, *navigation_ptr,
                         *renderer_, *background_tasks_)) ||
        !renderer_->bind_world(world_, fonts_, ui_))
    {
        reset_gameplay();
        return false;
    }
    gameplay_ =
        std::make_unique<GameplaySession>(*resources_, *world_, *renderer_, script, physics_,
                                          audio_, animation_, fonts_, ui_, background_tasks_.get());
    return true;
}

bool RuntimeHost::begin_gameplay(ScriptRuntimeFactory script_factory, RuntimeSetup setup,
                                 AnimationRetargeterFactory retargeter_factory,
                                 std::shared_ptr<const ResourceCatalog> catalog,
                                 std::shared_ptr<const MountList> mounts, std::string_view revision,
                                 std::string_view entry, std::string_view scene)
{
    if (!initialized_ || startup_requested_ || gameplay_ || !catalog || !mounts || !resources_ ||
        revision.empty() || catalog->revision().empty() ||
        catalog->revision() != resources_->catalog().revision() ||
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
    startup_revision_ = revision;
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
        if (movie_player_ && movie_player_->current_handle(current) &&
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
        if (!ready)
        {
            reset_gameplay();
            error_code_ = "world_initialize_failed";
            error_ = "Cannot initialize the gameplay world and plugin registrations";
            startup_status_ = RuntimeStartupStatus::failed;
        }
        else if (!gameplay_->begin_prepare(*startup_catalog_, *startup_mounts_, startup_revision_,
                                           startup_entry_, startup_scene_, startup_prepare_id_))
        {
            error_code_ = "gameplay_prepare_rejected";
            error_ = gameplay_->last_error();
            gameplay_->stop(GameplayStopCause::failure, error_);
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
        error_code_ = "gameplay_prepare_failed";
        error_ = gameplay_->last_error();
        gameplay_->stop(GameplayStopCause::failure, error_);
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

bool RuntimeHost::status(RuntimeHostStatus& output) const noexcept
{
    output = {};
    output.tick_id = tick_sequence_;
    output.sampled_at_ns =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::steady_clock::now().time_since_epoch())
                                       .count());
    output.initialized = initialized_;
    output.window_drawable = window_ && window_->can_render();
    output.exit_requested = exit_requested_;
    output.frame_status = failed_ ? FrameStatus::failed : last_tick_status_;
    output.prepare_id = startup_prepare_id_;
    output.error_code = error_code_;
    output.error = error_;
    return true;
}

FrameStatus RuntimeHost::fail_runtime(std::string_view code, std::string_view message)
{
    if (!failed_ || error_code_.empty())
    {
        error_code_ = code;
        error_ = message;
        failed_ = true;
        failure_cleanup_pending_ = true;
    }
    gameplay_active_ = false;
    if (std::exchange(failure_cleanup_pending_, false))
    {
        if (gameplay_)
        {
            gameplay_->stop(GameplayStopCause::failure, error_);
        }
    }
    return FrameStatus::failed;
}

FrameStatus RuntimeHost::tick(double delta_time)
{
    last_tick_status_ = tick_runtime(delta_time);
    ++tick_sequence_;
    return last_tick_status_;
}

FrameStatus RuntimeHost::tick_runtime(double delta_time)
{
    ENGINE_PROFILE_ZONE("Host.runtime_tick");
    const FrameStatus events = poll_events(delta_time);
    if (events != FrameStatus::running)
    {
        return events;
    }
    const bool presentation_was_paused = std::exchange(movie_clock_paused_, false);
    const double frame_delta = std::exchange(resume_clock_, false) ? 0.0 : delta_time;
    const double movie_delta = presentation_was_paused ? 0.0 : frame_delta;
    if (gameplay_active_ && !engine_.tick(std::min(frame_delta, 0.1)))
    {
        return fail_runtime("system_update_failed", "Cannot update the gameplay systems");
    }
    if (!audio_->tick(frame_delta))
    {
        return fail_runtime("audio_update_failed", "Cannot update audio output");
    }
    if (gameplay_ && !gameplay_->tick())
    {
        return fail_runtime("gameplay_update_failed", "Cannot update the gameplay session");
    }
    // 不可呈现期间暂停整个设备，恢复首帧提交后才恢复音轨时钟。
    if (!audio_->set_paused(presentation_was_paused || !window_->can_render()))
    {
        return fail_runtime("audio_pause_failed", "Cannot update the audio pause state");
    }
    if (movie_player_ && !movie_player_->update(movie_delta))
    {
        return fail_runtime("movie_update_failed", "Cannot update movie playback");
    }
    MoviePlaybackHandle movie_handle;
    MoviePlaybackStatus movie_status;
    MovieFrame movie_frame;
    const bool showing_movie =
        movie_player_ && movie_player_->current_handle(movie_handle) &&
        movie_player_->status(movie_handle, movie_status) &&
        (movie_status.state == MovieState::playing || movie_status.state == MovieState::paused ||
         movie_status.state == MovieState::completed) &&
        movie_player_->frame(movie_handle, movie_frame);
    // Preparing a candidate never gates presentation of the published scene.
    if (!showing_movie && window_->can_render())
    {
        (void)renderer_->prepare_scope({}, {});
    }
    const auto rendered =
        showing_movie ? renderer_->present_movie_frame(movie_frame) : renderer_->render();
    if (rendered == RenderSubmitStatus::deferred)
    {
        movie_clock_paused_ = true;
        if (!audio_->set_paused(true))
        {
            return fail_runtime("audio_pause_failed", "Cannot pause deferred frame audio");
        }
        return FrameStatus::paused;
    }
    if (rendered == RenderSubmitStatus::failed)
    {
        return fail_runtime("render_failed", "Cannot submit the runtime frame");
    }
    if (presentation_was_paused && !audio_->set_paused(false))
    {
        return fail_runtime("audio_pause_failed", "Cannot resume presented frame audio");
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
    if (!initialized_ || !std::isfinite(delta_time) || delta_time < 0.0)
    {
        return FrameStatus::failed;
    }
    const auto fail_input = [&]()
    {
        const std::string_view message = failed_ && error_code_.empty() && !error_.empty()
                                             ? std::string_view(error_)
                                             : std::string_view(input_->last_error());
        return fail_runtime("input_update_failed",
                            message.empty() ? "Cannot update runtime window and input" : message);
    };
    if (failed_)
    {
        return fail_input();
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
                             render_tasks_.get(),
                             movie_tasks_.get()};
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
    // 上一回调可能改变文档 / 可见性；没有 SDL 事件也必须刷新持续占用事实。
    if (window_->can_render() && gameplay_active_ && ui_ && !ui_->synchronize())
    {
        return fail_runtime("ui_update_failed", "Cannot synchronize runtime UI documents");
    }
    schema::InputRoutingDataT routing;
    routing.keyboard_claimed = gameplay_active_ && ui_ && ui_->keyboard_claimed();
    routing.pointer_claimed = gameplay_active_ && ui_ && ui_->pointer_claimed();
    if (!input_->begin_frame() || !input_->set_routing_state(routing) ||
        !window_->tick(delta_time) || failed_ || !input_->tick(delta_time))
    {
        return fail_input();
    }
    if (window_->exit_requested())
    {
        request_exit();
        return FrameStatus::exit_requested;
    }
    if (!audio_->set_paused(suspended_ || movie_clock_paused_ || !window_->can_render()))
    {
        return fail_runtime("audio_pause_failed", "Cannot update the audio pause state");
    }
    if (suspended_ || !window_->can_render())
    {
        movie_clock_paused_ = true;
        resume_clock_ = true;
        return FrameStatus::paused;
    }
    return FrameStatus::running;
}

bool RuntimeHost::set_suspended(bool suspended)
{
    if (!initialized_ || failed_ || !SDL_IsMainThread())
    {
        return false;
    }
    if (suspended_ == suspended)
    {
        return true;
    }
    suspended_ = suspended;
    resume_clock_ = true;
    movie_clock_paused_ = true;
    last_tick_status_ = FrameStatus::paused;
    if (suspended && !audio_->set_paused(true))
    {
        // 生命周期通知内只锁存故障；玩法停止留给宿主安全点。
        error_code_ = "audio_pause_failed";
        error_ = "Cannot suspend runtime audio";
        failed_ = true;
        failure_cleanup_pending_ = true;
        gameplay_active_ = false;
        return false;
    }
    return true;
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
    if (failure_cleanup_pending_)
    {
        (void)fail_runtime(error_code_, error_);
    }
    reset_gameplay();
    owns_engine_ = false;
    movie_player_.reset();
    resources_ = nullptr;
    loader_ = nullptr;
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
    movie_tasks_.reset();
    render_tasks_.reset();
    tasks_.reset();
    owned_threads_.reset();
    initialized_ = false;
    exit_requested_ = false;
    failed_ = false;
    failure_cleanup_pending_ = false;
    completion_domain_ = 0;
    startup_requested_ = false;
    startup_status_ = RuntimeStartupStatus::failed;
    startup_catalog_.reset();
    startup_mounts_.reset();
    startup_entry_.clear();
    startup_revision_.clear();
    startup_scene_.clear();
    startup_prepare_id_ = 0;
    error_code_.clear();
    error_.clear();
    presented_movie_id_ = 0;
    presented_movie_generation_ = 0;
    movie_clock_paused_ = false;
    suspended_ = false;
    resume_clock_ = false;
}

} // namespace engine
