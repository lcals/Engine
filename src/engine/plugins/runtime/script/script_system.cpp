#include "plugins/runtime/script/script_system.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "plugins/runtime/script/bindings/script_binding_registry.hpp"
#include "plugins/runtime/script/vm/script_vm.hpp"

#include "runtime/animation/animation_events.hpp"
#include "runtime/animation/animation_system.hpp"
#include "runtime/app/app.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <filesystem>
#include <limits>
#include <map>
#include <thread>
#include <utility>
#include <vector>

namespace engine::plugins::script
{
namespace
{
namespace fs = std::filesystem;
constexpr std::size_t maximum_text_size = vm::maximum_text_size;
std::atomic<std::uint64_t> last_run_id{0};

[[nodiscard]] std::uint64_t allocate_run_id()
{
    auto previous = last_run_id.load(std::memory_order_relaxed);
    while (previous != std::numeric_limits<std::uint64_t>::max())
    {
        if (last_run_id.compare_exchange_weak(previous, previous + 1, std::memory_order_relaxed))
        {
            return previous + 1;
        }
    }
    return 0;
}

[[nodiscard]] bool relative_path(std::string_view input, std::string& normalized)
{
    normalized.clear();
    if (input.empty() || input.find_first_of("\\:") != std::string_view::npos ||
        input.find('\0') != std::string_view::npos)
    {
        return false;
    }
    const fs::path path = PathUtils::from_utf8(input);
    if (path.is_absolute() || path.has_root_path())
    {
        return false;
    }
    const auto clean = path.lexically_normal();
    for (const auto& part : clean)
    {
        if (part == "..")
        {
            return false;
        }
    }
    normalized = PathUtils::to_utf8(clean);
    return !normalized.empty() && normalized != "." && normalized.back() != '/';
}

[[nodiscard]] bool script_path(std::string_view path)
{
    return path.ends_with(".js") || path.ends_with(".mjs");
}

struct Snapshot
{
    std::string revision = "builtin";
    std::string entry = "scripts/game.js";
    std::shared_ptr<const ResourceCatalog> catalog;
    MountList mounts;
    std::map<std::string, Guid, std::less<>> module_guids;
};

[[nodiscard]] bool catalog_snapshot(const ResourceCatalog& catalog, const MountList& mounts,
                                    Snapshot& snapshot)
{
    snapshot.catalog = std::make_shared<ResourceCatalog>(catalog);
    snapshot.mounts = mounts;
    for (const auto& asset : catalog.data().entries)
    {
        if (asset->kind == schema::AssetKind::script &&
            (asset->module_path.empty() ||
             !snapshot.module_guids.emplace(asset->module_path, asset->guid).second))
        {
            return false;
        }
    }
    return true;
}

} // namespace

struct ScriptSystem::State
{
    struct Run
    {
        State& owner;
        Snapshot snapshot;
        std::vector<ResourcePtr> resources;
        bool initialization_entered = false;
        bindings::BindingContext bindings;
        std::unique_ptr<vm::ScriptVm> machine;

        Run(State& state, Snapshot version)
            : owner(state), snapshot(std::move(version)),
              bindings(state.world, state.renderer, state.ui, state.physics, state.navigation,
                       state.input, state.window, state.movies),
              machine(vm::create_script_vm())
        {
        }

        ~Run()
        {
            // VM 与队列先释放，再撤销场景；不存在仍能调用旧 BindingContext 的函数。
            bindings.cancel_movies();
            machine.reset();
            bindings.shutdown();
        }

        [[nodiscard]] bool read_source(std::string_view path, std::string& source)
        {
            source.clear();
            if (path == "engine")
            {
                source = owner.binding_registry.source();
                return true;
            }
            if (!script_path(path) || snapshot.catalog == nullptr)
            {
                return owner.fail("Module is deleted or is not JavaScript: " + std::string(path));
            }
            const auto mapped = snapshot.module_guids.find(path);
            ByteBuffer bytes;
            bool success = mapped != snapshot.module_guids.end();
            if (success)
            {
                success = ResourceSystem::read_resource(*snapshot.catalog, snapshot.mounts,
                                                        mapped->second, bytes);
            }
            if (!success || bytes.size() > maximum_text_size)
            {
                return owner.fail("Cannot read GUID module from supplied catalog: " +
                                  std::string(path));
            }
            if (!bytes.empty())
            {
                source.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            }
            return true;
        }
    };

    ScriptStatus& status;
    bindings::ScriptBindingRegistry binding_registry;
    WorldSystem* world = nullptr;
    RenderSystem* renderer = nullptr;
    UiSystem* ui = nullptr;
    PhysicsSystem* physics = nullptr;
    NavigationSystem* navigation = nullptr;
    InputSystem* input = nullptr;
    WindowSystem* window = nullptr;
    MoviePlayer* movies = nullptr;
    double delta_time = 0.0;
    const std::thread::id owner_thread = std::this_thread::get_id();
    bool calling = false;
    std::unique_ptr<Run> active;
    std::unique_ptr<Run> prepared;

    State(ScriptStatus& state, WorldSystem* scene_world, RenderSystem* scene_renderer,
          UiSystem* scene_ui, PhysicsSystem* scene_physics, NavigationSystem* scene_navigation,
          InputSystem* scene_input, WindowSystem* scene_window, MoviePlayer* movie_player)
        : status(state), world(scene_world), renderer(scene_renderer), ui(scene_ui),
          physics(scene_physics), navigation(scene_navigation), input(scene_input),
          window(scene_window), movies(movie_player)
    {
    }

    ~State()
    {
        prepared.reset();
        stop();
    }

    bool fail(std::string message)
    {
        if (status.last_error != message)
        {
            LogSystem::write(LogLevel::error, message);
            status.last_error = std::move(message);
        }
        return false;
    }

    [[nodiscard]] std::unique_ptr<Run> precheck(Snapshot snapshot)
    {
        auto candidate = std::make_unique<Run>(*this, std::move(snapshot));
        auto& machine = candidate->machine;
        if (!machine || !machine->initialize(&candidate->bindings) ||
            !binding_registry.register_functions(*machine) ||
            !machine->prepare(candidate->snapshot.entry,
                              [&run = *candidate](std::string_view path, std::string& source)
                              { return run.read_source(path, source); }))
        {
            (void)fail(machine ? machine->last_error() : "Cannot create script virtual machine");
            return {};
        }
        return candidate;
    }

    [[nodiscard]] bool call(Run& run, const char* name, bool initialize)
    {
        ENGINE_PROFILE_ZONE("Script.callGameplay");
        calling = true;
        run.bindings.begin_call(delta_time);
        const bool called = run.machine->call_gameplay(name, initialize);
        run.bindings.end_call();
        const bool success = called && run.machine->drain_jobs();
        calling = false;
        return success || fail(run.machine->last_error());
    }

    [[nodiscard]] bool call_binding(Run& run, const char* name)
    {
        ENGINE_PROFILE_ZONE("Script.callBinding");
        calling = true;
        run.bindings.begin_call(delta_time);
        const bool called = run.machine->call_binding(name);
        run.bindings.end_call();
        calling = false;
        return called || fail(run.machine->last_error());
    }

    void stop()
    {
        status.run_id = 0;
        status.running = false;
        if (active)
        {
            if (active->initialization_entered)
            {
                active->initialization_entered = false;
                delta_time = 0.0;
                (void)call(*active, "OnShutdown", false);
            }
            (void)call_binding(*active, "__clearBindings");
            active.reset();
        }
        delta_time = 0.0;
        status.running = false;
        status.active_revision.clear();
    }

    [[nodiscard]] bool activate(std::unique_ptr<Run> candidate)
    {
        stop();
        status.last_error.clear();
        const auto run_id = allocate_run_id();
        if (run_id == 0)
        {
            return fail("Gameplay run identifiers are exhausted");
        }
        if (candidate->snapshot.catalog != nullptr)
        {
            auto* system = ResourceSystem::instance();
            if (system == nullptr)
            {
                return fail("Gameplay requires an active resource system");
            }
            for (const auto& name : candidate->machine->loaded_modules())
            {
                if (name == "engine")
                {
                    continue;
                }
                const auto mapped = candidate->snapshot.module_guids.find(name);
                const auto* expected = mapped == candidate->snapshot.module_guids.end()
                                           ? nullptr
                                           : candidate->snapshot.catalog->find(mapped->second);
                ResourcePtr resource;
                if (expected == nullptr || !system->loader().load(expected->guid, resource) ||
                    resource->info().content_hash != expected->content_hash ||
                    resource->info().module_path != expected->module_path)
                {
                    return fail("Active script resources differ from the prepared catalog");
                }
                candidate->resources.push_back(std::move(resource));
            }
        }
        active = std::move(candidate);
        active->bindings.activate();
        if (!active->machine->start())
        {
            const auto failure = active->machine->last_error();
            stop();
            return fail(failure);
        }
        active->initialization_entered = true;
        if (!call(*active, "OnInitialize", true))
        {
            const auto failure = status.last_error;
            stop();
            status.last_error = failure;
            return false;
        }
        status.active_revision = active->snapshot.revision;
        status.run_id = run_id;
        status.running = true;
        return true;
    }
};

ScriptSystem::ScriptSystem(WorldSystem* world, RenderSystem* renderer, UiSystem* ui,
                           PhysicsSystem* physics, NavigationSystem* navigation, InputSystem* input,
                           WindowSystem* window, AnimationSystem* animation,
                           MoviePlayer* movies) noexcept
    : world_(world), renderer_(renderer), ui_(ui), physics_(physics), navigation_(navigation),
      input_(input), window_(window), animation_(animation), movies_(movies)
{
}

ScriptSystem::~ScriptSystem() noexcept
{
    shutdown();
}

bool ScriptSystem::on_initialize(const EngineConfig&)
{
    status_.last_error.clear();
    state_ = std::make_unique<State>(status_, world_, renderer_, ui_, physics_, navigation_, input_,
                                     window_, movies_);
    if (!state_->binding_registry.initialize())
    {
        return state_->fail("Cannot register script binding modules");
    }
    auto machine = vm::create_script_vm();
    if (!machine || !machine->initialize(nullptr))
    {
        return state_->fail(machine ? machine->last_error()
                                    : "Cannot create script virtual machine");
    }
    if (animation_ != nullptr)
    {
        animation_->set_event_sink(
            [this](std::span<const AnimationEvent> events)
            {
                if (!state_ || !status_.running || !state_->active)
                {
                    return true;
                }
                if (state_->owner_thread != std::this_thread::get_id() || state_->calling)
                {
                    return state_->fail(
                        "Animation events require a non-reentrant main-thread safe point");
                }
                if (!state_->active->bindings.push_animation_events(events))
                {
                    return state_->fail("Cannot queue the complete animation event batch");
                }
                if (!state_->call_binding(*state_->active, "__dispatchAnimationEvents"))
                {
                    const auto failure = status_.last_error;
                    state_->stop();
                    status_.last_error = failure;
                    return false;
                }
                return true;
            });
    }
    return true;
}

bool ScriptSystem::prepare_gameplay(const ResourceCatalog& catalog, const MountList& mounts,
                                    std::string_view entry, std::string_view revision)
{
    if (!is_initialized() || !state_)
    {
        status_.last_error = "Gameplay requires an initialized script system";
        return false;
    }
    state_->prepared.reset();
    Snapshot snapshot;
    const std::string_view selected = entry.empty() ? std::string_view{"scripts/game.js"} : entry;
    if (!relative_path(selected, snapshot.entry) || snapshot.entry != selected ||
        !script_path(snapshot.entry) || !catalog_snapshot(catalog, mounts, snapshot))
    {
        return state_->fail("Invalid gameplay entry or resource catalog");
    }
    snapshot.revision = revision.empty() ? std::string(catalog.revision()) : std::string(revision);
    state_->prepared = state_->precheck(std::move(snapshot));
    return state_->prepared != nullptr;
}

bool ScriptSystem::start_prepared_gameplay()
{
    if (!is_initialized() || !state_ || !state_->prepared)
    {
        status_.last_error = "No prepared gameplay is available";
        return false;
    }
    return state_->activate(std::move(state_->prepared));
}

void ScriptSystem::stop_gameplay() noexcept
{
    if (state_)
    {
        state_->stop();
    }
}

bool ScriptSystem::execute_project_script(const ProjectScriptRequest& request,
                                          std::string& result_json, std::string& error)
{
    result_json.clear();
    error.clear();
    if (!is_initialized() || !state_ || state_->owner_thread != std::this_thread::get_id())
    {
        error = "Project scripts require the initialized script thread";
        return false;
    }
    if (!status_.running || !state_->active || request.expected_run_id == 0 ||
        request.expected_run_id != status_.run_id || state_->calling)
    {
        error = "Project script requires the matching active Run at a non-reentrant safe point";
        return false;
    }
    std::string module_path;
    if (request.module_path.size() > 1024 || !relative_path(request.module_path, module_path) ||
        module_path != request.module_path || !module_path.ends_with(".js") ||
        request.javascript.empty() || request.javascript.size() > vm::maximum_project_script_size ||
        request.javascript.find('\0') != std::string_view::npos || request.arguments_json.empty() ||
        request.arguments_json.size() > vm::maximum_project_script_size ||
        nlohmann::json::parse(request.arguments_json, nullptr, false).is_discarded())
    {
        error = "Invalid project script path, source or JSON arguments (maximum 256 KiB each)";
        return false;
    }
    auto& run = *state_->active;
    state_->calling = true;
    run.bindings.begin_call(0.0);
    const bool success = run.machine->execute_script(request, result_json);
    run.bindings.end_call();
    state_->calling = false;
    if (!success)
    {
        error = run.machine->last_error();
        result_json.clear();
    }
    return success;
}

const ScriptStatus& ScriptSystem::status() const noexcept
{
    return status_;
}

bool ScriptSystem::on_tick(double delta_time)
{
    ENGINE_PROFILE_ZONE("Script.tick");
    if (!state_ || !App::is_initialized())
    {
        return true;
    }
    if (status_.running && state_->active)
    {
        state_->delta_time = delta_time;
        if (!state_->call_binding(*state_->active, "__dispatchBindingEvents") ||
            !state_->call(*state_->active, "Update", false) ||
            !state_->call_binding(*state_->active, "__updateAnimationStateMachines"))
        {
            const auto failure = status_.last_error;
            state_->stop();
            status_.last_error = failure;
        }
        state_->delta_time = 0.0;
    }
    return true;
}

void ScriptSystem::on_shutdown() noexcept
{
    if (animation_ != nullptr)
    {
        animation_->set_event_sink({});
    }
    stop_gameplay();
    state_.reset();
}

} // namespace engine::plugins::script
