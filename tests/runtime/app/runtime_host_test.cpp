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
#include "runtime/render/render_system.hpp"
#include "runtime/task/task_system.hpp"

#include <SDL3/SDL_main.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
    bool execute_project_script(const ResourceHandle&, const ProjectScriptRequest&,
                                std::string& result, std::string& error) override
    {
        result.clear();
        error = "Test runtime has no VM";
        return false;
    }
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
    session.stop();
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
    active.stop();
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
    host.gameplay()->stop();
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
                         test_deployment_selection(settings.engine, settings.parallel);
    App::shutdown();
    return success ? 0 : 1;
}
