#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/font/font_system.hpp"
#include "runtime/gameplay/deployment_internal.hpp"
#include "runtime/gameplay/scene_load_operation.hpp"
#include "runtime/ui/ui_system.hpp"

#include "runtime/animation/animation_system.hpp"
#include "runtime/audio/audio_system.hpp"
#include "runtime/physics/physics_system.hpp"

#include "runtime/render/render_system.hpp"
#include "runtime/resource/builtin_resource_candidate.hpp"
#include "runtime/resource/builtin_resources.hpp"
#include "runtime/resource/resource_reference.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/resource/types/script_resource.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"
#include <cstdio>
#include <map>

#include "runtime/app/app.hpp"
#include "runtime/gameplay/script_runtime.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <set>
#include <system_error>
#include <utility>
#include <vector>

namespace engine
{
using namespace gameplay_detail;

struct GameplaySession::State
{
    struct BackgroundIo
    {
        std::atomic<bool> cancelled{false};
        std::uint64_t generation = 0;
        ControlRead control;
        DeploymentRead deployment;
        std::string status_text;
        std::string write_error;
        bool status_written = false;
        bool ready = false; // 仅完成委托在所属线程写入。
    };

    GameplaySession& owner;
    fs::path persistent_root;
    std::string target_platform;
    std::shared_ptr<BackgroundIo> pending;
    std::uint64_t generation = 1;
    std::string desired_status;
    Snapshot builtin;
    std::unique_ptr<Snapshot> available;
    std::string requested_entry;
    std::string last_manifest;
    std::string last_control;
    fs::file_time_type last_control_time{};
    bool control_seen = false;
    std::string last_attempt;
    std::string last_status;
    bool gameplay_requested = false;
    bool hot_reload = false;
    enum class PreparePhase
    {
        idle,
        load,
        precheck,
        restore,
        initialize,
        gpu,
        ready,
        failed,
        active
    };
    PreparePhase prepare_phase = PreparePhase::idle;
    std::string prepare_entry;
    std::string prepare_scene;
    std::uint64_t prepare_generation = 0;
    SceneLoadId startup_load;
    std::uint32_t prepare_script_completed = 0;
    std::unique_ptr<ResourceLoad> prepare_script_load;
    ResourceReference prepare_script;
    std::unique_ptr<Snapshot> prepare_snapshot;

    std::vector<ResourceReference> builtins;

    explicit State(GameplaySession& session)
        : owner(session),
          persistent_root(App::is_initialized() ? App::persistent_data_path() : fs::path{})
    {
        if (const auto* resources = &owner.resources_)
        {
            target_platform = resources->catalog().data().target_platform;
        }
    }

    bool fail(std::string message)
    {
        if (owner.last_error_ != message)
        {
            LogSystem::write(LogLevel::error, message);
            owner.last_error_ = std::move(message);
        }
        return false;
    }

    void publish()
    {
        if (!App::is_initialized())
        {
            return;
        }
        const auto* status = owner.script_status();
        if (status == nullptr)
        {
            return;
        }
        const Json document = {
            {"running", status->running},
            {"runId", std::to_string(status->run_id)},
            {"hotReload", hot_reload},
            {"activeRevision", status->active_revision},
            {"availableRevision", available ? available->revision : ""},
            {"lastError", owner.last_error_.empty() ? status->last_error : owner.last_error_}};
        desired_status = document.dump(-1, ' ', false, Json::error_handler_t::replace);
        if (owner.background_ != nullptr || desired_status == last_status)
        {
            return;
        }
        std::string error;
        if (!write_status(persistent_root, desired_status, error))
        {
            (void)fail(std::move(error));
            return;
        }
        last_status = desired_status;
    }

    void accept_control(const ControlRead& control)
    {
        if (!control.error.empty())
        {
            (void)fail(control.error);
            return;
        }
        if (!control.exists)
        {
            last_control.clear();
            control_seen = false;
            return;
        }
        if (!control.valid ||
            (control_seen && control.text == last_control && control.written == last_control_time))
        {
            return;
        }
        last_control = control.text;
        last_control_time = control.written;
        control_seen = true;
        if (control.hot_reload && !hot_reload)
        {
            last_attempt.clear();
        }
        hot_reload = control.hot_reload;
    }

    void poll_control()
    {
        if (App::is_initialized())
        {
            accept_control(read_control(persistent_root));
        }
    }

    void invalidate_background() noexcept
    {
        ++generation;
        if (pending)
        {
            pending->cancelled.store(true, std::memory_order_relaxed);
        }
    }

    bool collect_background()
    {
        if (!pending || !pending->ready)
        {
            return true;
        }
        auto result = std::move(pending);
        if (result->generation != generation)
        {
            return true;
        }
        accept_control(result->control);
        if (result->status_written)
        {
            last_status = result->status_text;
        }
        if (!result->write_error.empty())
        {
            (void)fail(std::move(result->write_error));
        }
        if (!result->deployment.error.empty())
        {
            return fail(std::move(result->deployment.error));
        }
        accept_deployment(result->deployment);
        return true;
    }

    void schedule_background()
    {
        if (owner.background_ == nullptr || pending || persistent_root.empty())
        {
            return;
        }
        auto operation = std::make_shared<BackgroundIo>();
        operation->generation = generation;
        operation->deployment.last_manifest = last_manifest;
        operation->deployment.current_revision = available ? available->revision : "";
        operation->deployment.target_platform = target_platform;
        if (desired_status != last_status)
        {
            operation->status_text = desired_status;
        }
        const bool submitted = owner.background_->submit(
            "Gameplay.deployment",
            [operation, root = persistent_root]()
            {
                ENGINE_PROFILE_ZONE("Gameplay.deployment");
                if (operation->cancelled.load(std::memory_order_relaxed))
                {
                    return false;
                }
                operation->control = read_control(root);
                const bool valid = operation->deployment.read(root, &operation->cancelled);
                if (!operation->status_text.empty() &&
                    !operation->cancelled.load(std::memory_order_relaxed))
                {
                    operation->status_written =
                        write_status(root, operation->status_text, operation->write_error);
                }
                return valid;
            },
            [operation](bool) { operation->ready = true; }, TaskPriority::low);
        if (submitted)
        {
            pending = std::move(operation);
        }
    }

    [[nodiscard]] bool poll_available()
    {
        DeploymentRead result;
        result.last_manifest = last_manifest;
        result.current_revision = available ? available->revision : "";
        result.target_platform = target_platform;
        if (!result.read(persistent_root))
        {
            return fail(std::move(result.error));
        }
        accept_deployment(result);
        return true;
    }

    void accept_deployment(DeploymentRead& result)
    {
        last_manifest = std::move(result.last_manifest);
        if (result.available)
        {
            available = std::move(result.available);
        }
    }

    bool prepare_builtins(std::vector<ResourceReference>& output)
    {
        auto* loader = owner.world_.resource_loader();
        if (!loader)
        {
            return fail("Resource loader is not available");
        }
        std::map<Guid, ResourceHandle> loaded;
        std::vector<ResourceReference> candidate;
        const auto& entries = builtin_resource_catalog().entries;
        while (loaded.size() != entries.size())
        {
            const auto previous = loaded.size();
            for (const auto& entry : entries)
            {
                if (loaded.contains(entry->guid))
                {
                    continue;
                }
                std::vector<ResourceHandle> dependencies;
                for (const auto& guid : entry->dependencies)
                {
                    const auto found = loaded.find(guid);
                    if (found == loaded.end())
                    {
                        break;
                    }
                    dependencies.push_back(found->second);
                }
                if (dependencies.size() != entry->dependencies.size())
                {
                    continue;
                }
                ResourceHandle handle;
                if (!loader->acquire_loaded(entry->guid, handle))
                {
                    const auto* physical = owner.resources_.catalog().find(entry->guid);
                    if (physical && physical->location)
                    {
                        if (!loader->load(entry->guid, handle))
                        {
                            return fail(std::string(loader->last_error()));
                        }
                    }
                    else
                    {
                        std::unique_ptr<Resource> value;
                        if (!resource_detail::builtin_candidate(entry->guid, value) ||
                            !loader->load_memory(value, dependencies, handle))
                        {
                            return fail("Cannot prepare built-in resource: " + entry->path + ": " +
                                        std::string(loader->last_error()));
                        }
                    }
                }
                ResourceReference reference;
                if (!reference.adopt(loader, handle))
                {
                    (void)loader->release(handle);
                    return false;
                }
                loaded.emplace(entry->guid, reference.handle());
                candidate.push_back(std::move(reference));
            }
            if (loaded.size() == previous)
            {
                return fail("Built-in dependency graph is incomplete");
            }
        }
        output = std::move(candidate);
        return true;
    }

    bool apply(const Snapshot& snapshot)
    {
        auto* resources = &owner.resources_;
        if (resources == nullptr ||
            snapshot.catalog.data().target_platform != resources->catalog().data().target_platform)
        {
            return fail("Runtime resource catalog targets a different platform");
        }
        const auto& entry = requested_entry.empty() ? snapshot.entry : requested_entry;
        if (!resources->update_catalog(snapshot.catalog.data(), snapshot.mounts))
        {
            return fail(std::string(resources->last_error()));
        }
        std::vector<ResourceReference> candidate_builtins;
        if (!prepare_builtins(candidate_builtins))
        {
            return false;
        }
        auto* loader = owner.world_.resource_loader();
        const schema::AssetEntryT* source = nullptr;
        for (const auto& item : snapshot.catalog.data().entries)
        {
            if (item->kind == schema::AssetKind::script && item->module_path == entry)
            {
                source = item.get();
                break;
            }
        }
        ResourceHandle handle;
        ResourceReference script;
        if (!source || !loader->load(source->guid, handle) || !script.adopt(loader, handle))
        {
            (void)loader->release(handle);
            return fail("Cannot load gameplay entry");
        }
        if (!owner.script_->prepare_gameplay(script.handle(), snapshot.revision))
        {
            return fail(owner.script_->status().last_error);
        }
        owner.script_->stop_gameplay();
        owner.release_scene();
        builtins = std::move(candidate_builtins);
        if (!owner.script_->start_prepared_gameplay())
        {
            const auto reason = owner.script_->status().last_error;
            owner.stop(GameplayStopCause::failure, reason);
            return false;
        }
        prepare_generation = resources->generation();
        prepare_phase = PreparePhase::gpu;
        owner.last_error_.clear();
        gameplay_requested = true;
        return true;
    }
};

GameplaySession::GameplaySession(ResourceSystem& resources, WorldSystem& world,
                                 RenderSystem& renderer, ScriptRuntime* script,
                                 PhysicsSystem* physics, AudioSystem* audio,
                                 AnimationSystem* animation, FontSystem* fonts, UiSystem* ui,
                                 TaskSystem* background) noexcept
    : resources_(resources), world_(world), renderer_(renderer), script_(script), physics_(physics),
      audio_(audio), animation_(animation), fonts_(fonts), ui_(ui), background_(background)
{
    if (script_ && !script_->bind_gameplay_session(this))
    {
        last_error_ = "Cannot bind gameplay scene loading service";
    }
}

GameplaySession::~GameplaySession() noexcept
{
    stop(GameplayStopCause::requested);
    if (script_)
    {
        (void)script_->bind_gameplay_session(nullptr);
    }
    if (background_ && state_)
    {
        // 关闭边界可以等待；先收尾旧写入，再确保磁盘上的最终状态为 stopped。
        (void)background_->wait();
        if (!state_->desired_status.empty())
        {
            std::string error;
            if (!write_status(state_->persistent_root, state_->desired_status, error))
            {
                (void)state_->fail(std::move(error));
            }
        }
    }
}

bool GameplaySession::set_resources(const schema::AssetCatalogDataT& data, const MountList& mounts,
                                    std::string_view entry)
{
    auto* resources = &resources_;
    if (resources == nullptr)
    {
        last_error_ = "Gameplay resource services are not initialized";
        return false;
    }
    if ((script_ != nullptr && script_->status().running) ||
        (state_ && state_->prepare_phase != State::PreparePhase::idle))
    {
        last_error_ = "Stop gameplay before replacing its resource directory";
        return false;
    }
    auto candidate = std::make_unique<State>(*this);
    if (!candidate->builtin.catalog.assign(data))
    {
        last_error_ = "Invalid prepared resource directory";
        return false;
    }
    candidate->builtin.mounts = mounts;
    candidate->builtin.revision = data.revision;
    candidate->builtin.entry = entry.empty() ? "scripts/game.js" : std::string(entry);
    if (state_)
    {
        candidate->last_manifest = state_->last_manifest;
        candidate->hot_reload = state_->hot_reload;
    }
    else if (App::is_initialized())
    {
        // 第一次直接提交时忽略上次编辑器会话已发布的旧内容。
        std::error_code error;
        const auto manifest = App::persistent_data_path() / "assets" / "available.json";
        if (fs::is_regular_file(manifest, error) && !error)
        {
            if (!read_text(manifest, candidate->last_manifest))
            {
                last_error_ = "Cannot inspect existing deployment manifest";
                return false;
            }
        }
    }
    if (!resources->update_catalog(data, mounts))
    {
        last_error_ =
            "Cannot submit prepared resource directory: " + std::string(resources->last_error());
        return false;
    }
    if (!candidate->prepare_builtins(candidate->builtins))
    {
        return false;
    }
    if (state_ && state_->available && state_->available->catalog.revision() == data.revision)
    {
        candidate->available = std::move(state_->available);
    }
    if (state_)
    {
        state_->invalidate_background();
        candidate->generation = state_->generation;
        candidate->pending = std::move(state_->pending);
        candidate->last_status = state_->last_status;
    }
    state_ = std::move(candidate);
    last_error_.clear();
    return true;
}

bool GameplaySession::begin_prepare(const ResourceCatalog& catalog, const MountList& mounts,
                                    std::string_view revision, std::string_view entry,
                                    std::string_view scene, std::uint64_t& output_id)
{
    output_id = 0;
    auto* resources = &resources_;
    if (!resources || !world_.is_initialized() || !renderer_.is_initialized() ||
        (script_ && (!script_->is_initialized() || script_->status().running)) ||
        (state_ && state_->prepare_phase != State::PreparePhase::idle) ||
        preparations_.size() >= 64)
    {
        last_error_ = "Gameplay preparation is unavailable or already active";
        return false;
    }
    if (revision.empty() || catalog.revision().empty() ||
        catalog.revision() != resources->catalog().revision() ||
        catalog.data().target_platform != resources->catalog().data().target_platform)
    {
        last_error_ = "Gameplay snapshot does not match the current directory";
        return false;
    }
    if (!state_)
    {
        state_ = std::make_unique<State>(*this);
    }
    state_->prepare_snapshot = std::make_unique<Snapshot>();
    state_->prepare_snapshot->catalog = catalog;
    state_->prepare_snapshot->mounts = mounts;
    state_->prepare_snapshot->revision = revision;
    state_->prepare_snapshot->entry = entry.empty() ? "scripts/game.js" : std::string(entry);
    state_->builtin = *state_->prepare_snapshot;
    state_->prepare_entry = state_->prepare_snapshot->entry;
    state_->prepare_scene = scene;
    state_->prepare_generation = resources->generation();
    state_->prepare_script_completed = 0;
    if (!state_->prepare_builtins(state_->builtins))
    {
        state_->prepare_snapshot.reset();
        return false;
    }
    static std::atomic<std::uint64_t> sequence{0};
    auto previous = sequence.load(std::memory_order_relaxed);
    do
    {
        if (previous == UINT64_MAX)
        {
            state_->prepare_snapshot.reset();
            last_error_ = "Gameplay preparation identity space is exhausted";
            return false;
        }
    } while (!sequence.compare_exchange_weak(previous, previous + 1, std::memory_order_relaxed));
    GameplayPrepareSnapshot snapshot;
    snapshot.id = previous + 1;
    current_prepare_id_ = snapshot.id;
    preparations_.emplace(snapshot.id, std::move(snapshot));
    state_->prepare_phase = State::PreparePhase::load;
    output_id = current_prepare_id_;
    last_error_.clear();
    return true;
}

GameplayPrepareStatus GameplaySession::step_prepare(std::uint32_t resource_budget,
                                                    double max_seconds)
{
    using Phase = State::PreparePhase;
    auto* resources = &resources_;
    if (!state_ || !resources || resource_budget == 0 || !std::isfinite(max_seconds) ||
        max_seconds <= 0 || state_->prepare_phase == Phase::idle ||
        state_->prepare_phase == Phase::failed || state_->prepare_phase == Phase::active)
    {
        return state_ && state_->prepare_phase == Phase::active && resource_budget > 0 &&
                       std::isfinite(max_seconds) && max_seconds > 0
                   ? GameplayPrepareStatus::active
                   : GameplayPrepareStatus::failed;
    }
    if (state_->prepare_phase == Phase::ready)
    {
        return GameplayPrepareStatus::ready;
    }
    auto fail = [&](std::string_view message)
    {
        const std::string error(message);
        if (const auto found = preparations_.find(current_prepare_id_);
            found != preparations_.end())
        {
            found->second.state = GameplayPrepareStatus::failed;
            found->second.error_code = "preparation_failed";
            found->second.error = error;
        }
        stop(GameplayStopCause::failure, error);
        state_->prepare_phase = Phase::failed;
        return GameplayPrepareStatus::failed;
    };
    if (resources->generation() != state_->prepare_generation)
    {
        return fail("Resource directory changed during gameplay preparation");
    }
    if (state_->prepare_phase == Phase::load)
    {
        const auto started = std::chrono::steady_clock::now();
        std::uint32_t remaining = resource_budget;
        auto begin = [&](std::string_view guid, std::unique_ptr<ResourceLoad>& load)
        {
            return world_.resource_loader() && world_.resource_loader()->begin_load(guid, load) &&
                   load != nullptr;
        };
        if (script_ && !state_->prepare_script && !state_->prepare_script_load)
        {
            const schema::AssetEntryT* entry = nullptr;
            for (const auto& item : resources->catalog().data().entries)
            {
                if (item->kind == schema::AssetKind::script &&
                    item->module_path == state_->prepare_entry)
                {
                    entry = item.get();
                    break;
                }
            }
            if (!entry || !begin(entry->guid, state_->prepare_script_load))
            {
                return fail("Cannot begin asynchronous entry script load");
            }
        }
        if (!state_->prepare_scene.empty() && state_->startup_load.request == 0)
        {
            SceneLoadSpec spec;
            spec.scene_path = state_->prepare_scene;
            if (!begin_scene_load(spec, state_->startup_load))
            {
                return fail("Cannot begin startup scene load");
            }
            if (const auto found = preparations_.find(current_prepare_id_);
                found != preparations_.end())
            {
                found->second.scene_load_id = state_->startup_load;
            }
        }
        auto advance = [&](std::unique_ptr<ResourceLoad>& load, ResourceReference& output,
                           std::uint32_t& completed)
        {
            if (!load || remaining == 0 ||
                std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() >=
                    max_seconds)
            {
                return true;
            }
            ResourceLoadProgress progress;
            const auto status = load->status(progress);
            const auto newly_completed =
                progress.completed > completed ? progress.completed - completed : 0;
            remaining -= std::min(remaining, newly_completed);
            completed = progress.completed;
            if (status == ResourceLoadStatus::failed || status == ResourceLoadStatus::cancelled)
            {
                last_error_ = load->last_error();
                return false;
            }
            if (status == ResourceLoadStatus::ready)
            {
                ResourceHandle handle;
                if (!load->take(handle) || !output.adopt(world_.resource_loader(), handle))
                {
                    if (world_.resource_loader())
                    {
                        (void)world_.resource_loader()->release(handle);
                    }
                    return false;
                }
                load.reset();
                return output && output->generation() == state_->prepare_generation;
            }
            return true;
        };
        if (!advance(state_->prepare_script_load, state_->prepare_script,
                     state_->prepare_script_completed))
        {
            const std::string error =
                last_error_.empty() ? "Gameplay resource load failed" : last_error_;
            return fail(error);
        }
        if (!script_ || state_->prepare_script)
        {
            state_->prepare_phase = Phase::precheck;
        }
        return GameplayPrepareStatus::preparing;
    }
    if (state_->prepare_phase == Phase::precheck)
    {
        const auto started = std::chrono::steady_clock::now();
        const bool ready =
            !script_ || script_->prepare_gameplay(state_->prepare_script.handle(),
                                                  state_->prepare_snapshot->revision);
        LogSystem::write(LogLevel::info,
                         "Startup script precheck seconds: " +
                             std::to_string(std::chrono::duration<double>(
                                                std::chrono::steady_clock::now() - started)
                                                .count()));
        if (!ready)
        {
            const std::string error = script_->status().last_error;
            return fail(error);
        }
        state_->prepare_phase = Phase::restore;
        return GameplayPrepareStatus::preparing;
    }
    if (state_->prepare_phase == Phase::restore)
    {
        if (state_->startup_load.request)
        {
            step_scene_loads({resource_budget, max_seconds});
            SceneLoadSnapshot snapshot;
            if (!scene_load_status(state_->startup_load, snapshot) ||
                snapshot.state == SceneLoadState::failed ||
                snapshot.state == SceneLoadState::cancelled)
            {
                return fail(snapshot.error.empty() ? "Startup scene load failed" : snapshot.error);
            }
            if (snapshot.state == SceneLoadState::staged &&
                !prepare_scene_load(state_->startup_load))
            {
                return fail("Cannot prepare startup scene");
            }
            if (snapshot.state == SceneLoadState::ready &&
                !activate_scene_load(state_->startup_load))
            {
                return fail("Cannot activate startup scene");
            }
            if (snapshot.state != SceneLoadState::active)
            {
                return GameplayPrepareStatus::preparing;
            }
        }
        state_->prepare_phase = Phase::initialize;
        return GameplayPrepareStatus::preparing;
    }
    if (state_->prepare_phase == Phase::initialize)
    {
        const auto started = std::chrono::steady_clock::now();
        const bool ready = !script_ || script_->start_prepared_gameplay();
        LogSystem::write(LogLevel::info,
                         "Startup project initialization seconds: " +
                             std::to_string(std::chrono::duration<double>(
                                                std::chrono::steady_clock::now() - started)
                                                .count()));
        if (!ready)
        {
            const std::string error = script_->status().last_error;
            return fail(error);
        }
        if (const auto found = preparations_.find(current_prepare_id_);
            found != preparations_.end())
        {
            found->second.run_id = script_ ? script_->status().run_id : 0;
        }
        state_->prepare_phase = Phase::gpu;
        return GameplayPrepareStatus::preparing;
    }
    if (state_->prepare_phase == Phase::gpu)
    {
        const auto scope =
            world_.scene() ? SceneScopeId{world_.epoch(), world_.scene()->id()} : SceneScopeId{};
        const SceneWorkBudget budget{resource_budget, max_seconds};
        const auto prepare = [&](auto* system)
        {
            return !system || !world_.scene() ? SceneWorkState::not_required
                                              : system->prepare_scope(scope, budget);
        };
        for (const auto result : {prepare(physics_), prepare(animation_), prepare(ui_),
                                  renderer_.prepare_scope({}, budget)})
        {
            if (result == SceneWorkState::failed)
            {
                return fail("Startup scene preparation failed");
            }
            if (result != SceneWorkState::ready && result != SceneWorkState::not_required)
            {
                return GameplayPrepareStatus::preparing;
            }
        }
        state_->prepare_phase = Phase::ready;
        if (const auto found = preparations_.find(current_prepare_id_);
            found != preparations_.end())
        {
            found->second.state = GameplayPrepareStatus::ready;
        }
        return GameplayPrepareStatus::ready;
    }
    return GameplayPrepareStatus::failed;
}

bool GameplaySession::activate_prepared()
{
    if (!state_ || state_->prepare_phase != State::PreparePhase::ready)
    {
        return false;
    }
    state_->prepare_phase = State::PreparePhase::active;
    if (const auto found = preparations_.find(current_prepare_id_); found != preparations_.end())
    {
        found->second.state = GameplayPrepareStatus::active;
    }
    state_->gameplay_requested = script_ != nullptr;
    state_->prepare_script.reset();
    if (state_->startup_load.request)
    {
        state_->startup_load = {};
    }
    state_->prepare_snapshot.reset();
    return true;
}

bool GameplaySession::prepare_status(std::uint64_t id, GameplayPrepareSnapshot& output) const
{
    output = {};
    const auto found = preparations_.find(id);
    if (found == preparations_.end())
    {
        return false;
    }
    output = found->second;
    return true;
}

bool GameplaySession::release_prepare(std::uint64_t id)
{
    const auto found = preparations_.find(id);
    if (found == preparations_.end() || (found->second.state != GameplayPrepareStatus::active &&
                                         found->second.state != GameplayPrepareStatus::failed &&
                                         found->second.state != GameplayPrepareStatus::cancelled))
    {
        return false;
    }
    preparations_.erase(found);
    return true;
}

bool GameplaySession::start(std::string_view entry)
{
    last_error_.clear();
    if (script_ == nullptr)
    {
        last_error_ = "No script runtime was provided by the host";
        return false;
    }
    if (!world_.is_initialized() || !renderer_.is_initialized() || !script_->is_initialized() ||
        !App::is_initialized())
    {
        last_error_ = "The runtime is not ready for gameplay";
        return false;
    }
    if (script_->status().running)
    {
        return true;
    }
    if (!state_)
    {
        state_ = std::make_unique<State>(*this);
    }
    state_->invalidate_background();
    state_->requested_entry = entry;
    state_->poll_control();
    const bool deployment_valid = state_->poll_available();
    const std::string deployment_error = deployment_valid ? std::string{} : last_error_;
    if (!state_->available && state_->builtin.catalog.revision().empty())
    {
        if (!ResourceSystem::read_catalog(App::assets_path(), state_->builtin.catalog))
        {
            last_error_ = "Cannot read the built-in runtime catalog";
            if (!deployment_error.empty())
            {
                last_error_ += "; " + deployment_error;
            }
            state_->publish();
            return false;
        }
        state_->builtin.mounts = {{"content", App::assets_path()}};
        state_->builtin.revision = state_->builtin.catalog.revision();
    }
    const auto& selected = state_->available ? *state_->available : state_->builtin;
    state_->last_attempt = selected.revision;
    const bool result = state_->apply(selected);
    if (result && !deployment_error.empty())
    {
        last_error_ = deployment_error;
    }
    state_->publish();
    state_->schedule_background();
    return result;
}

bool GameplaySession::tick()
{
    step_scene_loads({});
    if (!state_ || script_ == nullptr || !App::is_initialized())
    {
        return true;
    }
    if (!background_)
    {
        state_->poll_control();
    }
    const bool complete = background_ ? state_->collect_background() : state_->poll_available();
    if (state_->gameplay_requested && !script_->status().running)
    {
        state_->gameplay_requested = false;
        release_scene();
    }
    if (state_->gameplay_requested && state_->prepare_phase == State::PreparePhase::gpu)
    {
        const auto status = step_prepare(1, 0.002);
        if (status == GameplayPrepareStatus::ready && !activate_prepared())
        {
            stop(GameplayStopCause::failure, "Cannot activate reloaded gameplay");
        }
    }
    if (complete && state_->gameplay_requested &&
        state_->prepare_phase != State::PreparePhase::gpu && state_->hot_reload &&
        state_->available && state_->available->revision != script_->status().active_revision &&
        state_->available->revision != state_->last_attempt)
    {
        state_->last_attempt = state_->available->revision;
        (void)state_->apply(*state_->available);
    }
    state_->publish();
    state_->schedule_background();
    return true;
}

void GameplaySession::release_scene() noexcept
{
    finish_scene_loads(GameplayStopCause::requested, "Scene content was released");
    if (ui_)
    {
        ui_->release_scene();
    }
    if (fonts_)
    {
        fonts_->release_scene();
    }
    if (animation_ != nullptr)
    {
        animation_->clear();
    }
    if (audio_ != nullptr)
    {
        audio_->clear_scene();
    }
    if (physics_ != nullptr)
    {
        physics_->clear();
    }
    world_.destroy_scene();
    renderer_.release_scene();
}

void GameplaySession::stop(GameplayStopCause cause, std::string_view reason) noexcept
{
    if (!reason.empty())
    {
        last_error_ = reason;
    }
    if (const auto found = preparations_.find(current_prepare_id_);
        found != preparations_.end() && (found->second.state == GameplayPrepareStatus::preparing ||
                                         found->second.state == GameplayPrepareStatus::ready))
    {
        auto& result = found->second;
        result.state = cause == GameplayStopCause::failure ? GameplayPrepareStatus::failed
                                                           : GameplayPrepareStatus::cancelled;
        result.error_code =
            cause == GameplayStopCause::failure ? "session_failed" : "session_stopped";
        result.error = reason;
    }
    current_prepare_id_ = 0;
    finish_scene_loads(cause, reason);
    if (state_)
    {
        state_->gameplay_requested = false;
        state_->invalidate_background();
        if (state_->startup_load.request)
        {
            state_->startup_load = {};
        }
        state_->prepare_script_load.reset();
        state_->prepare_script.reset();
        state_->prepare_snapshot.reset();
        state_->prepare_phase = State::PreparePhase::idle;
    }
    if (script_ != nullptr)
    {
        script_->stop_gameplay();
    }
    release_scene();
    if (state_)
    {
        state_->publish();
        state_->schedule_background();
    }
}

#if defined(ENGINE_ENABLE_TEST_SUPPORT)
bool GameplaySession::execute_project_script(const ProjectScriptRequest& request,
                                             std::string& result_json, std::string& error)
{
    result_json.clear();
    error.clear();
    if (const auto found = preparations_.find(current_prepare_id_);
        found != preparations_.end() && found->second.state != GameplayPrepareStatus::active)
    {
        error = "Project script execution requires activated gameplay";
        return false;
    }
    if (script_ == nullptr)
    {
        error = "Project script execution requires the script plugin";
        return false;
    }
    auto* loader = world_.resource_loader();
    if (!loader)
    {
        error = "Project script requires a resource loader";
        return false;
    }
    static std::atomic<std::uint64_t> sequence{0};
    auto identity = sequence.load(std::memory_order_relaxed);
    do
    {
        if (identity == UINT64_MAX)
        {
            error = "Script identities exhausted";
            return false;
        }
    } while (!sequence.compare_exchange_weak(identity, identity + 1, std::memory_order_relaxed));
    ++identity;
    char guid[37]{};
    std::snprintf(guid, sizeof(guid), "653266a1-3c1e-40a5-%04x-%012llx",
                  static_cast<unsigned int>(identity >> 48),
                  static_cast<unsigned long long>(identity & 0x0000ffffffffffffULL));
    schema::AssetEntryT entry;
    entry.guid = guid;
    entry.path = std::string("runtime/project-scripts/") + guid + ".js";
    entry.module_path = request.module_path;
    entry.kind = schema::AssetKind::script;
    ByteBuffer bytes(request.javascript.size());
    std::transform(request.javascript.begin(), request.javascript.end(), bytes.begin(),
                   [](char value) { return static_cast<std::byte>(value); });
    std::unique_ptr<Resource> value =
        std::make_unique<ScriptResource>(std::move(entry), std::move(bytes));
    ResourceHandle handle;
    if (!loader->load_memory(value, {}, handle))
    {
        error = loader->last_error();
        return false;
    }
    ResourceReference script;
    if (!script.adopt(loader, handle))
    {
        (void)loader->release(handle);
        return false;
    }
    return script_->execute_project_script(script.handle(), request, result_json, error);
}

#endif

const ScriptStatus* GameplaySession::script_status() const noexcept
{
    if (script_ == nullptr)
    {
        return nullptr;
    }
    return &script_->status();
}

std::string_view GameplaySession::last_error() const noexcept
{
    return last_error_;
}

} // namespace engine
