#include "plugins/runtime/script/script_system.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "plugins/runtime/script/bindings/script_binding_registry.hpp"
#include "plugins/runtime/script/vm/script_vm.hpp"

#include "runtime/app/app.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <filesystem>
#include <map>
#include <utility>
#include <vector>

namespace engine::plugins::script
{
namespace
{
namespace fs = std::filesystem;
constexpr std::size_t maximum_text_size = vm::maximum_text_size;

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
    bool active_catalog = false;
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
              bindings(state.world, state.renderer, state.ui), machine(vm::create_script_vm())
        {
        }

        ~Run()
        {
            // VM 与队列先释放，再撤销场景；不存在仍能调用旧 BindingContext 的函数。
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
            if (success && snapshot.active_catalog)
            {
                ResourcePtr resource;
                auto* system = ResourceSystem::instance();
                success = system != nullptr && system->loader().load(mapped->second, resource);
                if (success)
                {
                    const auto content = resource->bytes();
                    bytes.assign(content.begin(), content.end());
                    resources.push_back(std::move(resource));
                }
            }
            else if (success)
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
    double delta_time = 0.0;
    std::unique_ptr<Run> active;
    std::unique_ptr<Run> prepared;

    State(ScriptStatus& state, WorldSystem* scene_world, RenderSystem* scene_renderer,
          UiSystem* scene_ui)
        : status(state), world(scene_world), renderer(scene_renderer), ui(scene_ui)
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
        run.bindings.begin_call(delta_time);
        const bool called = run.machine->call_gameplay(name, initialize);
        run.bindings.end_call();
        return (called && run.machine->drain_jobs()) || fail(run.machine->last_error());
    }

    [[nodiscard]] bool call_binding(Run& run, const char* name)
    {
        run.bindings.begin_call(delta_time);
        const bool called = run.machine->call_binding(name);
        run.bindings.end_call();
        return called || fail(run.machine->last_error());
    }

    void stop()
    {
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
        if (candidate->snapshot.catalog != nullptr && !candidate->snapshot.active_catalog)
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
        status.running = true;
        return true;
    }
};

ScriptSystem::ScriptSystem(WorldSystem* world, RenderSystem* renderer, UiSystem* ui) noexcept
    : world_(world), renderer_(renderer), ui_(ui)
{
}

ScriptSystem::~ScriptSystem() noexcept
{
    shutdown();
}

bool ScriptSystem::on_initialize(const EngineConfig&)
{
    status_.last_error.clear();
    status_.available_revision.clear();
    state_ = std::make_unique<State>(status_, world_, renderer_, ui_);
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
    status_.available_revision = snapshot.revision;
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

bool ScriptSystem::start_gameplay(std::string_view entry)
{
    if (!is_initialized() || !state_)
    {
        status_.last_error = "Gameplay requires an initialized script system";
        return false;
    }
    Snapshot snapshot;
    const auto selected = entry.empty() ? std::string_view{"scripts/game.js"} : entry;
    if (!relative_path(selected, snapshot.entry) || snapshot.entry != selected ||
        !script_path(snapshot.entry))
    {
        return state_->fail("Unsafe gameplay entry path");
    }
    auto* resources = ResourceSystem::instance();
    if (resources == nullptr || resources->catalog().data().entries.empty() ||
        !catalog_snapshot(resources->catalog(), {}, snapshot))
    {
        return state_->fail("Gameplay requires an active runtime resource catalog");
    }
    snapshot.active_catalog = true;
    snapshot.revision = std::string(resources->catalog().revision());
    auto candidate = state_->precheck(std::move(snapshot));
    return candidate != nullptr && state_->activate(std::move(candidate));
}

void ScriptSystem::stop_gameplay() noexcept
{
    if (state_)
    {
        state_->stop();
    }
}

void ScriptSystem::set_hot_reload(bool enabled)
{
    status_.hot_reload = enabled;
}

const ScriptStatus& ScriptSystem::status() const noexcept
{
    return status_;
}

bool ScriptSystem::on_tick(double delta_time)
{
    if (!state_ || !App::is_initialized())
    {
        return true;
    }
    if (status_.running && state_->active)
    {
        state_->delta_time = delta_time;
        if (!state_->call_binding(*state_->active, "__dispatchBindingEvents") ||
            !state_->call(*state_->active, "Update", false))
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
    stop_gameplay();
    state_.reset();
}

} // namespace engine::plugins::script
