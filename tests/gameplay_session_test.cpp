#include "resource_fixture.hpp"

#include "runtime/app/app.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/gameplay/script_runtime.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <future>
#include <string>
#include <thread>

namespace
{

using namespace engine;

bool expect(bool success, const char* message)
{
    if (!success)
    {
        std::fprintf(stderr, "gameplay session: %s\n", message);
    }
    return success;
}

bool write(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    file.close();
    return static_cast<bool>(file);
}

class TestScript final : public ScriptRuntime
{
public:
    bool prepare_gameplay(const ResourceCatalog&, const MountList&, std::string_view,
                          std::string_view) override
    {
        return false;
    }
    bool start_prepared_gameplay() override
    {
        return false;
    }
    void stop_gameplay() noexcept override
    {
        status_.running = false;
    }
    void set_hot_reload(bool enabled) override
    {
        owner_only = owner_only && std::this_thread::get_id() == owner_;
        ++control_calls;
        status_.hot_reload = enabled;
    }
    bool execute_project_script(const ProjectScriptRequest&, std::string& result,
                                std::string& error) override
    {
        result.clear();
        error = "Test runtime has no project script VM";
        return false;
    }
    const ScriptStatus& status() const noexcept override
    {
        return status_;
    }

    bool owner_only = true;
    std::size_t control_calls = 0;

protected:
    bool on_initialize(const EngineConfig&) override
    {
        return true;
    }
    void on_shutdown() noexcept override
    {
    }

private:
    std::thread::id owner_ = std::this_thread::get_id();
    ScriptStatus status_;
};

bool test_background_protocol(const std::filesystem::path& root)
{
    ThreadSystem threads({1});
    TaskSystem tasks(threads, {"DeploymentTest", 1});
    ResourceSystem resources;
    WindowSystem window;
    WorldSystem world;
    RenderSystem renderer(window, &world);
    TestScript script;
    if (!threads.initialize({}) || !tasks.initialize({}) || !resources.initialize({}))
    {
        return false;
    }
    GameplaySession session(world, renderer, &script, nullptr, nullptr, nullptr, nullptr, nullptr,
                            &tasks);
    test_support::ResourceFixture fixture;
    if (!session.set_resources(fixture.catalog, {{"content", fixture.root}}) ||
        !write(root / "script-control.json", R"({"hotReload":true})"))
    {
        return false;
    }

    // 占住唯一 worker，证明正常 tick 不等待 I/O 准备或后台完成。
    std::promise<void> release;
    const auto gate = release.get_future().share();
    auto blocker = tasks.submit("test hold worker",
                                [gate]
                                {
                                    gate.wait();
                                    return true;
                                });
    if (!blocker.valid())
    {
        return false;
    }
    const bool accepted = session.tick();
    const bool untouched = script.control_calls == 0;
    release.set_value();
    if (!expect(accepted && untouched,
                "tick only queues work and never mutates scripts on worker") ||
        !tasks.wait() ||
        !expect(script.control_calls == 0, "worker completion alone cannot apply control"))
    {
        return false;
    }
    (void)tasks.dispatch_completions(8, 1.0);
    if (!session.tick() || !expect(script.status().hot_reload && script.owner_only,
                                   "control is applied by the next owner-thread tick"))
    {
        return false;
    }

    // 读取完成后更换会话资源；旧完成委托不得重新应用已废弃候选。
    if (!tasks.wait())
    {
        return false;
    }
    const auto calls = script.control_calls;
    if (!session.set_resources(fixture.catalog, {{"content", fixture.root}}) ||
        !write(root / "script-control.json", R"({"hotReload":false})"))
    {
        return false;
    }
    (void)tasks.dispatch_completions(8, 1.0);
    if (!session.tick() || !expect(script.control_calls == calls,
                                   "replaced resource state discards old completion generation"))
    {
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (script.status().hot_reload && std::chrono::steady_clock::now() < deadline)
    {
        (void)tasks.dispatch_completions(8, 1.0);
        if (!session.tick())
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!expect(!script.status().hot_reload && script.owner_only,
                "replacement state eventually accepts only the current control file"))
    {
        return false;
    }

    // 清单损坏在完成后报告；不把后台错误写进脚本或活动场景。
    std::error_code error;
    std::filesystem::create_directories(root / "assets", error);
    if (error || !write(root / "assets/available.json", "invalid"))
    {
        return false;
    }
    const auto error_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (session.last_error().empty() && std::chrono::steady_clock::now() < error_deadline)
    {
        (void)tasks.dispatch_completions(8, 1.0);
        if (!session.tick())
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!expect(session.last_error() == "Invalid asset version manifest" &&
                    world.scene() == nullptr,
                "invalid deployment reports on owner without changing the active scene"))
    {
        return false;
    }
    session.stop("test stopped");
    return true;
}

} // namespace

int main()
{
    test_support::ResourceFixture directory;
    if (directory.root.empty())
    {
        return 1;
    }
    AppConfig app;
    app.application_name = "engine-gameplay-session-test";
    app.assets_path = directory.root / "builtin";
    app.persistent_data_path = directory.root / "persistent";
    app.cache_path = directory.root / "cache";
    if (!App::initialize(app))
    {
        return 1;
    }
    bool success = test_background_protocol(app.persistent_data_path);
    std::string status;
    success &=
        expect(ResourceSystem::read_text(app.persistent_data_path / "script-status.json", status) &&
                   status.find("\"running\":false") != std::string::npos &&
                   status.find("test stopped") != std::string::npos,
               "shutdown drains stale writes and publishes the latest stopped status");
    App::shutdown();
    return success ? 0 : 1;
}
