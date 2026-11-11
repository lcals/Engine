#include "movie_fixture.hpp"
#include "resource_fixture.hpp"
#include "resource_services.hpp"
#include "runtime_settings_fixture.hpp"

#include "runtime/app/app.hpp"
#include "runtime/app/runtime_host.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/gameplay/script_runtime.hpp"
#include "runtime/movie/movie_frame.hpp"
#include "runtime/movie/movie_player.hpp"
#include "runtime/render/render_extension.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_video.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>

namespace
{

using namespace engine;
namespace fs = std::filesystem;
using Json = nlohmann::json;

TaskSystem* background_tasks = nullptr;

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "runtime host: %s\n", message);
    }
    return condition;
}

bool observe_domains(RuntimeStage, WorldSystem&, WindowSystem&, PhysicsSystem&, NavigationSystem&,
                     RenderSystem&, TaskSystem& tasks)
{
    background_tasks = &tasks;
    return true;
}

class TestScript final : public ScriptRuntime
{
public:
    bool bind_gameplay_session(GameplaySession*) override
    {
        return true;
    }
    ~TestScript() noexcept override
    {
        shutdown();
    }

    bool prepare_gameplay(const ResourceHandle&, std::string_view revision) override
    {
        prepared_ = revision;
        return true;
    }
    bool start_prepared_gameplay() override
    {
        status_.running = true;
        status_.active_revision = prepared_;
        ++status_.run_id;
        return true;
    }
    void stop_gameplay() noexcept override
    {
        status_.running = false;
    }
#if defined(ENGINE_ENABLE_TEST_SUPPORT)
    bool execute_project_script(const ResourceHandle&, const ProjectScriptRequest&,
                                std::string& result, std::string& error) override
    {
        result.clear();
        error = "Test runtime has no VM";
        return false;
    }
#endif
    const ScriptStatus& status() const noexcept override
    {
        return status_;
    }

protected:
    bool on_initialize(const EngineConfig&) override
    {
        return true;
    }
    void on_shutdown() noexcept override
    {
        stop_gameplay();
    }

private:
    std::string prepared_;
    ScriptStatus status_;
};

std::unique_ptr<ScriptRuntime> create_script(WorldSystem&, RenderSystem&, UiSystem&, PhysicsSystem&,
                                             NavigationSystem&, InputSystem&, WindowSystem&,
                                             AnimationSystem&, MoviePlayer*)
{
    return std::make_unique<TestScript>();
}

bool write_file(const fs::path& path, std::string_view bytes)
{
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    if (error)
    {
        return false;
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    return static_cast<bool>(file);
}

bool write_bytes(const fs::path& path, const void* bytes, std::size_t size)
{
    return write_file(path, {static_cast<const char*>(bytes), size});
}

std::string hash_string(std::uint64_t value)
{
    std::array<char, 17> text{};
    std::snprintf(text.data(), text.size(), "%016llx", static_cast<unsigned long long>(value));
    return text.data();
}

bool write_version(const fs::path& root, std::string_view revision, std::string_view platform,
                   const fs::path& manifest = {})
{
    schema::AssetCatalogDataT catalog;
    catalog.schema_version = 2;
    catalog.revision = revision;
    catalog.target_platform = platform;
    auto entry = std::make_unique<schema::AssetEntryT>();
    entry->guid = "10000000-0000-4000-8000-000000000001";
    entry->kind = schema::AssetKind::script;
    entry->path = "scripts/game.js";
    entry->module_path = entry->path;
    constexpr std::string_view script = "export default class Game {}";
    ByteBuffer entity;
    if (!ResourceSystem::encode_resource(
            entry->guid, entry->kind,
            {reinterpret_cast<const std::byte*>(script.data()), script.size()}, entity,
            entry->header_size))
    {
        return false;
    }
    entry->location = std::make_unique<schema::FileLocationT>();
    entry->location->mount = "content";
    entry->location->path = "gen/" + entry->guid;
    entry->location->length = entity.size();
    if (!write_bytes(root / entry->location->path, entity.data(), entity.size()))
    {
        return false;
    }
    std::uint64_t hash = 0;
    if (!VirtualFileSystem::hash_file(root / entry->location->path, hash, entry->content_size))
    {
        return false;
    }
    entry->content_hash = hash_string(hash);
    catalog.entries.push_back(std::move(entry));
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishAssetCatalogDataBuffer(builder,
                                         schema::AssetCatalogData::Pack(builder, &catalog));
    if (!write_bytes(root / "catalog.bin", builder.GetBufferPointer(), builder.GetSize()))
    {
        return false;
    }
    if (manifest.empty())
    {
        return true;
    }
    Json files = Json::array();
    for (const auto& path : {std::string("catalog.bin"), catalog.entries.front()->location->path})
    {
        std::uint64_t size = 0;
        if (!VirtualFileSystem::hash_file(root / path, hash, size))
        {
            return false;
        }
        files.push_back({{"path", path}, {"size", size}, {"hash", hash_string(hash)}});
    }
    return write_file(manifest, Json{{"revision", revision},
                                     {"entry", "scripts/game.js"},
                                     {"files", std::move(files)},
                                     {"deleted", Json::array()}}
                                    .dump());
}

bool test_completion_fairness(const EngineConfig& config, RuntimeParallelConfig parallel)
{
    parallel.completions_per_frame = 1;
    parallel.completion_budget_seconds = 1.0;
    RuntimeHost host;
    background_tasks = nullptr;
    if (!host.initialize(config, nullptr, nullptr, nullptr, observe_domains, nullptr, parallel) ||
        background_tasks == nullptr)
    {
        return false;
    }
    std::size_t runtime_completed = 0;
    bool background_completed = false;
    for (std::size_t index = 0; index < 16; ++index)
    {
        if (!host.tasks()->submit(
                "runtime backlog", [] { return true; }, [&](bool) { ++runtime_completed; }))
        {
            return false;
        }
    }
    if (!background_tasks->submit(
            "deployment completion", [] { return true; },
            [&](bool) { background_completed = true; }) ||
        !host.tasks()->wait() || !background_tasks->wait())
    {
        return false;
    }
    for (std::size_t frame = 0; frame < 6 && !background_completed; ++frame)
    {
        if (host.poll_events(0.0) == FrameStatus::failed)
        {
            return false;
        }
    }
    return expect(background_completed && runtime_completed < 16,
                  "a one-completion frame budget services background work before runtime drains");
}

bool test_movie_presentation(const EngineConfig& config, const RuntimeParallelConfig& parallel)
{
    RuntimeHost host;
    if (!host.initialize_presentation(config, nullptr, nullptr, nullptr, parallel) ||
        !expect(host.world() == nullptr && host.ui() == nullptr && host.movie_player() == nullptr,
                "presentation without a resource loader starts without movie services") ||
        !expect(host.tick(0.0) == FrameStatus::running, "presentation ticks without a world"))
    {
        return false;
    }
    std::array<std::uint8_t, 16> luma{};
    std::array<std::uint8_t, 4> u{};
    std::array<std::uint8_t, 4> v{};
    luma.fill(128);
    u.fill(128);
    v.fill(128);
    MovieFrame frame;
    frame.width = frame.height = 4;
    frame.planes = {{{luma, 4, 4, 4}, {u, 2, 2, 2}, {v, 2, 2, 2}}};
    auto* renderer = host.renderer();
    const auto before = renderer->frame_number();
    if (!expect(renderer->present_movie_frame(frame) && renderer->frame_number() != before,
                "movie frame submits through the initialized GPU"))
    {
        return false;
    }
    frame.planes[0].bytes = {};
    return expect(!renderer->present_movie_frame(frame), "truncated movie plane is rejected");
}

bool test_movie_clock_and_startup(const EngineConfig& config, const RuntimeParallelConfig& parallel)
{
    test_support::ResourceFixture fixture;
    test_support::ResourceServices services;
    auto& resources = services.core;
    RuntimeHost host;
    if (!services.initialize(config) || !test_support::make_movie(fixture, 90, 30) ||
        !fixture.publish(resources) ||
        !host.initialize_presentation(config, &resources, &services.loader, &services.threads,
                                      parallel))
    {
        return false;
    }
    auto catalog = std::make_shared<ResourceCatalog>(resources.catalog());
    auto mounts = std::make_shared<MountList>(MountList{{"content", fixture.root}});
    const auto generation = resources.generation();
    auto& player = *host.movie_player();
    engine::ResourceReference movie;
    if (!services.load("movies/test.mp4", movie))
    {
        return false;
    }
    MoviePlaybackHandle handle;
    if (!player.play(movie.handle(), handle) ||
        !host.begin_gameplay(nullptr, nullptr, nullptr, catalog, mounts) ||
        !expect(host.step_gameplay(0.002) == RuntimeStartupStatus::preparing && !host.world(),
                "world preparation waits for this movie's submitted first frame"))
    {
        return false;
    }
    MovieFrame frame;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!player.frame(handle, frame) && std::chrono::steady_clock::now() < deadline)
    {
        if (!services.tick() || host.tick(0) != FrameStatus::running)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    MoviePlaybackStatus playback;
    if (!expect(player.frame(handle, frame) && host.tick(1.25) == FrameStatus::running &&
                    player.status(handle, playback) &&
                    std::abs(playback.position_seconds - 1.25) < 0.0001,
                "silent movie receives full wall time beyond the simulation step cap"))
    {
        return false;
    }
    RuntimeStartupStatus startup = RuntimeStartupStatus::preparing;
    while (startup == RuntimeStartupStatus::preparing &&
           std::chrono::steady_clock::now() < deadline)
    {
        startup = host.step_gameplay(0.002);
        if (!services.tick() || host.tick(0) != FrameStatus::running)
        {
            return false;
        }
    }
    return expect(startup == RuntimeStartupStatus::ready && host.world() &&
                      player.status(handle, playback) && playback.state == MovieState::playing &&
                      resources.generation() == generation,
                  "staged preparation preserves the intro and submitted catalog") &&
           player.stop(handle, MovieStopReason::skipped) && player.release(handle) &&
           host.activate_gameplay() && host.tick(0) == FrameStatus::running;
}

bool test_prepare_results(const EngineConfig& config, const RuntimeParallelConfig& parallel)
{
    test_support::ResourceFixture fixture;
    test_support::ResourceServices services;
    RuntimeHost host;
    if (!services.initialize(config) || !fixture.publish(services.core) ||
        !host.initialize(config, &services.core, &services.loader, nullptr, nullptr,
                         &services.threads, parallel))
    {
        return false;
    }
    auto& session = *host.gameplay();
    const auto catalog = services.core.catalog();
    const MountList mounts{{"content", fixture.root}};
    std::uint64_t cancelled = 0;
    GameplayPrepareSnapshot snapshot;
    if (!session.begin_prepare(catalog, mounts, {}, {}, cancelled) ||
        !expect(session.prepare_status(cancelled, snapshot) &&
                    snapshot.state == GameplayPrepareStatus::preparing &&
                    !session.release_prepare(cancelled),
                "startup acceptance has an identity but is not a releasable success"))
    {
        return false;
    }
    session.stop(GameplayStopCause::requested, "A normal stop can include diagnostic text");
    if (!expect(session.prepare_status(cancelled, snapshot) &&
                    snapshot.state == GameplayPrepareStatus::cancelled,
                "an explicit stop cause determines cancellation independently of its text"))
    {
        return false;
    }
    std::uint64_t active = 0;
    if (!session.begin_prepare(catalog, mounts, {}, {}, active))
    {
        return false;
    }
    const auto advance = [&]()
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        auto status = GameplayPrepareStatus::preparing;
        while (status == GameplayPrepareStatus::preparing &&
               std::chrono::steady_clock::now() < deadline)
        {
            if (!services.tick() || host.tick(0) != FrameStatus::running)
            {
                return GameplayPrepareStatus::failed;
            }
            status = session.step_prepare(64, 0.01);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return status;
    };
    if (!expect(active > cancelled && advance() == GameplayPrepareStatus::ready &&
                    session.prepare_status(active, snapshot) &&
                    snapshot.state == GameplayPrepareStatus::ready &&
                    !session.release_prepare(active) && session.activate_prepared() &&
                    session.prepare_status(active, snapshot) &&
                    snapshot.state == GameplayPrepareStatus::active &&
                    session.release_prepare(active),
                "ready waits for explicit activation and releasing success does not stop gameplay"))
    {
        return false;
    }
    std::uint64_t rejected = 99;
    if (!expect(!session.begin_prepare(catalog, mounts, {}, {}, rejected) && rejected == 0,
                "a running native session rejects a second startup and clears its output"))
    {
        return false;
    }
    session.stop(GameplayStopCause::requested);
    std::uint64_t failed = 0;
    if (!session.begin_prepare(catalog, mounts, {}, "scenes/missing.scene", failed) ||
        !expect(advance() == GameplayPrepareStatus::failed &&
                    session.prepare_status(failed, snapshot) &&
                    snapshot.state == GameplayPrepareStatus::failed && !snapshot.error.empty(),
                "an accepted startup keeps its own asynchronous failure"))
    {
        return false;
    }
    const auto error = snapshot.error;
    session.stop(GameplayStopCause::requested);
    std::uint64_t next = 0;
    if (!session.begin_prepare(catalog, mounts, {}, {}, next))
    {
        return false;
    }
    session.stop(GameplayStopCause::failure, "Synthetic runtime failure");
    return expect(session.prepare_status(failed, snapshot) && snapshot.error == error &&
                      session.prepare_status(next, snapshot) &&
                      snapshot.state == GameplayPrepareStatus::failed &&
                      session.prepare_status(cancelled, snapshot) &&
                      snapshot.state == GameplayPrepareStatus::cancelled &&
                      session.release_prepare(cancelled) && session.release_prepare(failed) &&
                      session.release_prepare(next) && !session.prepare_status(active, snapshot) &&
                      snapshot.id == 0,
                  "restart, stop and release preserve independent terminal results");
}

bool test_scene_publication_order(const EngineConfig& config, const RuntimeParallelConfig& parallel)
{
    test_support::ResourceFixture fixture;
    schema::SceneDataT replacement;
    replacement.schema_version = 3;
    replacement.info = std::make_unique<schema::SceneInfoT>();
    replacement.info->name = "Replacement";
    test_support::ResourceServices services;
    RuntimeHost host;
    if (!fixture.add("scenes/replacement.scene", replacement) || !services.initialize(config) ||
        !fixture.publish(services.core) ||
        !host.initialize(config, &services.core, &services.loader, nullptr, nullptr,
                         &services.threads, parallel))
    {
        return false;
    }
    auto& world = *host.world();
    auto& session = *host.gameplay();
    auto* original = world.create_scene("Original");
    if (!original || !original->create_node("Old content") || host.tick(0) != FrameStatus::running)
    {
        return false;
    }
    const auto original_id = original->id();
    std::map<std::uint64_t, Node> prepared_nodes;
    const auto ready = [&](SceneLoadId id)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline)
        {
            SceneLoadSnapshot status;
            if (!services.tick() || host.poll_events(0) == FrameStatus::failed || !session.tick() ||
                !session.scene_load_status(id, status))
            {
                return false;
            }
            if (status.state == SceneLoadState::staged)
            {
                auto* candidate = session.scene_load_scene(id);
                auto node = candidate ? candidate->create_node("Prepared content") : Node{};
                if (!node || !session.prepare_scene_load(id))
                {
                    return false;
                }
                prepared_nodes[id.request] = node;
            }
            if (status.state == SceneLoadState::ready)
            {
                return true;
            }
            if (status.state == SceneLoadState::failed)
            {
                std::fprintf(stderr, "scene publication: %s\n", status.error.c_str());
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    };
    SceneLoadSpec append;
    append.kind = SceneLoadKind::append_batch;
    append.destination_scene_id = original_id;
    SceneLoadId first, earlier, pending, replace;
    SceneLoadSpec replacing;
    replacing.scene_path = "scenes/replacement.scene";
    if (!session.begin_scene_load(append, first) || !ready(first) ||
        !session.begin_scene_load(append, earlier) || !ready(earlier) ||
        !session.begin_scene_load(append, pending) || !ready(pending) ||
        !session.begin_scene_load(replacing, replace) || !ready(replace))
    {
        return false;
    }
    auto late_removed = original->create_node("Late old content");
    auto persistent_root = original->create_node("Late persistent root");
    auto persistent_child = original->create_node("Late persistent child");
    if (!late_removed || !persistent_root || !persistent_child ||
        !persistent_root.set_persistent(true) || !persistent_child.set_parent(persistent_root) ||
        !session.activate_scene_load(earlier) || !session.activate_scene_load(first) ||
        !session.activate_scene_load(replace) || !session.tick())
    {
        return false;
    }
    SceneLoadSnapshot status;
    RenderFrameSnapshot frame;
    if (!expect(
            session.scene_load_status(first, status) &&
                status.state == SceneLoadState::activating &&
                status.stages.front().state == SceneWorkState::queued &&
                session.cancel_scene_load(first) && !session.cancel_scene_load(earlier) &&
                !session.activate_scene_load(pending) && world.scene()->id() == original_id,
            "activation order, queued cancellation and the replacement cutoff share one target"))
    {
        return false;
    }
    if (host.tick(0) != FrameStatus::running || !session.tick() ||
        !expect(session.scene_load_status(earlier, status) &&
                    status.state == SceneLoadState::active &&
                    status.first_scene_submission_id != 0 &&
                    session.scene_load_status(replace, status) &&
                    status.state == SceneLoadState::activating &&
                    !host.renderer()->frame_status(status.scope, frame) &&
                    !session.cancel_scene_load(replace) && world.scenes().size() >= 2 &&
                    world.find_node(persistent_root.id()) && world.find_node(persistent_child.id()),
                "replacement publishes after the earlier batch frame and retains old ownership"))
    {
        return false;
    }
    if (host.tick(0) != FrameStatus::running || !session.tick() ||
        !expect(
            session.scene_load_status(replace, status) && status.state == SceneLoadState::active &&
                session.scene_load_status(pending, status) &&
                status.state == SceneLoadState::cancelled && status.error_code == "target_replaced",
            "only a real replacement frame completes the switch and cancels old candidates"))
    {
        return false;
    }
    for (std::size_t pass = 0; pass < 8; ++pass)
    {
        if (!world.tick(0))
        {
            return false;
        }
    }
    if (!expect(!late_removed && !prepared_nodes.at(earlier.request) && persistent_root &&
                    persistent_child && world.find_node(persistent_child.id()),
                "late old nodes and earlier batches retire while late persistent trees remain "
                "published"))
    {
        return false;
    }
    append.destination_scene_id = world.scene()->id();
    SceneLoadId unloaded;
    if (!session.begin_scene_load(append, unloaded) || !ready(unloaded) ||
        !world.destroy_scene(append.destination_scene_id) || !session.tick())
    {
        return false;
    }
    if (!expect(session.scene_load_status(unloaded, status) &&
                    status.state == SceneLoadState::failed &&
                    status.error_code == "target_unloaded" && session.release_scene_load(first) &&
                    session.release_scene_load(earlier) && session.release_scene_load(pending) &&
                    session.release_scene_load(replace) && session.release_scene_load(unloaded),
                "an unloaded target ends ready requests and every terminal result can be released"))
    {
        return false;
    }
    auto* target = world.create_scene("Replacement target");
    if (!target)
    {
        return false;
    }
    append.destination_scene_id = target->id();
    SceneLoadId cancelled_replace, blocked_append;
    if (!session.begin_scene_load(replacing, cancelled_replace) || !ready(cancelled_replace) ||
        !session.begin_scene_load(append, blocked_append) || !ready(blocked_append) ||
        !expect(session.activate_scene_load(cancelled_replace) &&
                    !session.activate_scene_load(blocked_append) &&
                    session.cancel_scene_load(cancelled_replace) &&
                    session.activate_scene_load(blocked_append) &&
                    session.cancel_scene_load(blocked_append) &&
                    session.release_scene_load(cancelled_replace) &&
                    session.release_scene_load(blocked_append),
                "cancelling an unpublished replacement removes its publication cutoff"))
    {
        return false;
    }
    SceneLoadId orphaned;
    return session.begin_scene_load(replacing, orphaned) && ready(orphaned) &&
           session.activate_scene_load(orphaned) && world.destroy_scene(target->id()) &&
           session.tick() &&
           expect(session.scene_load_status(orphaned, status) &&
                      status.state == SceneLoadState::failed &&
                      status.error_code == "target_unloaded" && !world.scene() &&
                      session.release_scene_load(orphaned),
                  "an accepted replacement cannot publish after its original target is destroyed");
}

class FailingRenderExtension final : public RenderExtension
{
public:
    bool reject = false;
    bool draw(const RenderContext&) override
    {
        return !reject;
    }
    void release_scene() noexcept override
    {
    }
    void release_gpu() noexcept override
    {
    }
};

bool test_render_failure(const EngineConfig& config, const RuntimeParallelConfig& parallel)
{
    test_support::ResourceFixture fixture;
    test_support::ResourceServices services;
    RuntimeHost host;
    FailingRenderExtension extension;
    if (!services.initialize(config) || !fixture.publish(services.core) ||
        !host.initialize(config, &services.core, &services.loader, nullptr, nullptr,
                         &services.threads, parallel))
    {
        return false;
    }
    auto* scene = host.world()->create_scene("Render failure");
    auto camera = scene ? scene->create_node("Camera").add_component<Camera>() : Camera{};
    if (!camera || !scene->set_active_camera(camera) || !host.renderer()->add_extension(extension))
    {
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    RenderFrameSnapshot frame;
    while ((!host.renderer()->frame_status({}, frame) || frame.kind != RenderFrameKind::scene) &&
           std::chrono::steady_clock::now() < deadline)
    {
        if (!services.tick() || host.tick(0) != FrameStatus::running)
        {
            return false;
        }
    }
    SceneLoadSpec spec;
    spec.kind = SceneLoadKind::append_batch;
    spec.destination_scene_id = scene->id();
    SceneLoadId id;
    if (!host.gameplay()->begin_scene_load(spec, id))
    {
        return false;
    }
    extension.reject = true;
    RuntimeHostStatus status;
    SceneLoadSnapshot load;
    return expect(host.tick(0) == FrameStatus::failed && host.status(status) &&
                      status.error_code == "render_failed" && !status.error.empty() &&
                      host.gameplay()->scene_load_status(id, load) &&
                      load.state == SceneLoadState::failed && host.tick(0) == FrameStatus::failed &&
                      host.status(status) && status.error_code == "render_failed",
                  "render failure preserves request errors and cannot become blank-frame success");
}

bool test_input_failure(const EngineConfig& config, const RuntimeParallelConfig& parallel)
{
    test_support::ResourceServices services;
    RuntimeHost host;
    if (!services.initialize(config) ||
        !host.initialize(config, &services.core, &services.loader, nullptr, nullptr,
                         &services.threads, parallel))
    {
        return false;
    }
    auto* scene = host.world()->create_scene("Input failure");
    SceneLoadSpec spec;
    spec.kind = SceneLoadKind::append_batch;
    spec.destination_scene_id = scene ? scene->id() : 0;
    SceneLoadId id;
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = SDL_GetWindowID(host.window()->native_window());
    if (!scene || !host.gameplay()->begin_scene_load(spec, id) || !SDL_PushEvent(&event) ||
        !host.window()->tick(0))
    {
        return false;
    }
    RuntimeHostStatus status;
    SceneLoadSnapshot load;
    return expect(host.poll_events(0) == FrameStatus::failed && host.status(status) &&
                      status.error_code == "input_update_failed" && !status.error.empty() &&
                      host.gameplay()->scene_load_status(id, load) &&
                      load.state == SceneLoadState::failed &&
                      host.poll_events(0) == FrameStatus::failed && host.status(status) &&
                      status.error_code == "input_update_failed",
                  "an event outside the input frame is finalized once at the next host safe point");
}

bool test_deployment_selection(const EngineConfig& config, const RuntimeParallelConfig& parallel)
{
    test_support::ResourceServices services;
    if (!services.initialize(config))
    {
        return false;
    }
    RuntimeHost host;
    if (!host.initialize(config, &services.core, &services.loader, create_script, observe_domains,
                         &services.threads, parallel))
    {
        return false;
    }
    auto& session = *host.gameplay();
    const std::string platform = services.core.catalog().data().target_platform;
    const auto manifest = App::persistent_data_path() / "assets/available.json";
    const auto versions = App::persistent_data_path() / "assets/versions";
    if (!write_version(App::assets_path(), "builtin", platform) ||
        !write_file(manifest, "invalid") ||
        !expect(
            session.start() && session.script_status()->active_revision == "builtin" &&
                session.last_error() == "Invalid asset version manifest",
            "invalid persistent metadata preserves diagnostics and starts the complete builtin"))
    {
        return false;
    }
    session.stop(GameplayStopCause::requested);
    if (!write_file(App::assets_path() / "catalog.bin", "invalid") ||
        !write_version(versions / "external", "external", platform, manifest))
    {
        return false;
    }
    // A new session has no previously loaded builtin metadata to mask this dependency.
    host.shutdown();
    if (!host.initialize(config, &services.core, &services.loader, create_script, observe_domains,
                         &services.threads, parallel) ||
        !expect(host.gameplay()->start() &&
                    host.gameplay()->script_status()->active_revision == "external",
                "a complete persistent version starts independently of a broken builtin catalog"))
    {
        return false;
    }
    auto& active = *host.gameplay();
    const auto run = active.script_status()->run_id;
    if (!write_file(App::persistent_data_path() / "script-control.json", R"({"hotReload":true})") ||
        !write_file(manifest, "invalid"))
    {
        return false;
    }
    for (std::size_t pass = 0; pass < 4; ++pass)
    {
        if (!background_tasks->wait() || host.poll_events(0.0) == FrameStatus::failed ||
            !active.tick())
        {
            return false;
        }
    }
    if (!expect(active.script_status()->running && active.script_status()->run_id == run &&
                    active.script_status()->active_revision == "external" &&
                    active.last_error() == "Invalid asset version manifest",
                "invalid hot reload publication keeps the active complete version"))
    {
        return false;
    }
    active.stop(GameplayStopCause::requested);
    if (!write_version(App::assets_path(), "builtin", platform) ||
        !write_version(versions / "incomplete", "incomplete", platform, manifest))
    {
        return false;
    }
    std::error_code error;
    fs::remove(versions / "incomplete/gen/10000000-0000-4000-8000-000000000001", error);
    host.shutdown();
    if (error || !host.initialize(config, &services.core, &services.loader, create_script,
                                  observe_domains, &services.threads, parallel))
    {
        return false;
    }
    if (!expect(host.gameplay()->start() &&
                    host.gameplay()->script_status()->active_revision == "builtin" &&
                    !host.gameplay()->last_error().empty(),
                "a missing persistent entity selects the complete builtin as a single version"))
    {
        return false;
    }
    host.gameplay()->stop(GameplayStopCause::requested);
    const auto wrong_platform = platform == "android" ? "windows" : "android";
    if (!write_version(versions / "wrong-platform", "wrong-platform", wrong_platform, manifest) ||
        !expect(host.gameplay()->start() &&
                    host.gameplay()->script_status()->active_revision == "builtin" &&
                    host.gameplay()->last_error() ==
                        "Published resource catalog targets a different platform",
                "a complete deployment for another platform cannot replace the native baseline"))
    {
        return false;
    }
    host.shutdown();
    if (!write_file(App::assets_path() / "catalog.bin", "invalid") ||
        !write_file(manifest, "invalid") ||
        !host.initialize(config, &services.core, &services.loader, create_script, observe_domains,
                         &services.threads, parallel))
    {
        return false;
    }
    return expect(!host.gameplay()->start() && !host.gameplay()->script_status()->running &&
                      !host.gameplay()->last_error().empty(),
                  "startup reports failure when neither complete version is usable");
}

} // namespace

int main(int, char**)
{
    test_support::ResourceFixture directory;
    RuntimeSettings settings;
    AppConfig app;
    app.application_name = "engine-runtime-host-test";
    app.assets_path = directory.root / "builtin";
    app.persistent_data_path = directory.root / "persistent";
    app.cache_path = directory.root / "cache";
    if (directory.root.empty() || !test_support::load_runtime_settings(settings) ||
        !App::initialize(app))
    {
        return 1;
    }
    settings.engine.width = 320;
    settings.engine.height = 240;
    settings.engine.window_title = "Runtime host contract test";
    const bool success = test_movie_presentation(settings.engine, settings.parallel) &&
                         test_movie_clock_and_startup(settings.engine, settings.parallel) &&
                         test_completion_fairness(settings.engine, settings.parallel) &&
                         test_prepare_results(settings.engine, settings.parallel) &&
                         test_scene_publication_order(settings.engine, settings.parallel) &&
                         test_render_failure(settings.engine, settings.parallel) &&
                         test_input_failure(settings.engine, settings.parallel) &&
                         test_deployment_selection(settings.engine, settings.parallel);
    App::shutdown();
    return success ? 0 : 1;
}
