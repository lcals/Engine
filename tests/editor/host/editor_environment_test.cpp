#include "editor/editor_environment.hpp"
#include "editor/editor_system.hpp"
#include "editor/project/project_system.hpp"
#include "runtime/app/app.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
#include <cstddef>
#include <cstdint>
#include <memory>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "editor environment test: %s\n", message);
    }
    return condition;
}

bool directory(const fs::path& path)
{
    std::error_code error;
    fs::create_directories(path, error);
    return !error;
}

bool absent(const fs::path& path)
{
    std::error_code error;
    const bool exists = fs::exists(path, error);
    return !error && !exists;
}

bool write(const fs::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    output.close();
    return static_cast<bool>(output);
}

std::string read(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool resource_submission(engine::editor::EditorEnvironment& environment, const fs::path& root)
{
    engine::ResourceSystem resources;
    engine::WindowSystem window;
    engine::WorldSystem world;
    engine::RenderSystem renderer(window, &world);
    engine::GameplaySession gameplay(world, renderer, nullptr);
    if (!expect(resources.initialize({}), "initialize resource submission services"))
    {
        return false;
    }
    auto candidate = resources.catalog().data();
    candidate.revision = "editor-environment-candidate";
    const auto generation = resources.generation();
    const auto registration = root / "cache/editor-hosts";
    if (!write(registration, "blocked registration directory") ||
        !expect(!environment.apply_resources(candidate, gameplay) &&
                    resources.generation() == generation &&
                    resources.catalog().revision() == "builtin-only",
                "registration I/O failure preserves the active resource directory"))
    {
        return false;
    }
    std::error_code error;
    if (!fs::remove(registration, error) || error)
    {
        return false;
    }
    auto invalid = candidate;
    invalid.schema_version = 0;
    if (!expect(!environment.apply_resources(invalid, gameplay) &&
                    resources.generation() == generation && fs::is_empty(registration, error) &&
                    !error,
                "rejected resource submission releases its candidate registration and lease") ||
        !expect(environment.apply_resources(candidate, gameplay) &&
                    resources.catalog().revision() == candidate.revision,
                "retry commits the resource directory and owns its registration"))
    {
        return false;
    }
    const auto committed_generation = resources.generation();
    return expect(!environment.apply_resources(invalid, gameplay) &&
                      resources.generation() == committed_generation &&
                      resources.catalog().revision() == candidate.revision,
                  "later rejected submissions preserve the existing directory and lease");
}

bool run_cases(const fs::path& workspace)
{
    const auto root = workspace / "project";
    const auto persistent = workspace / "persistent";
    if (!directory(root / "settings") || !directory(root / "cache") || !directory(root / "assets"))
    {
        return false;
    }
    engine::editor::ProjectSystem project(engine::PathUtils::to_utf8(root));
    engine::editor::EditorEnvironment environment(project, persistent);
    const engine::EngineConfig config;
    if (!expect(!environment.initialize() && !engine::App::is_initialized(),
                "uninitialized project cannot acquire App") ||
        !expect(project.initialize(config) && environment.initialize(),
                "initialize editor paths") ||
        !expect(engine::App::assets_path() == root / "cache",
                "editor maps compiled cache directly without a package snapshot") ||
        !expect(engine::App::persistent_data_path() == persistent, "map persistent storage") ||
        !expect(!environment.initialize() && environment.is_initialized(),
                "duplicate initialization preserves the current owner") ||
        !write(root / "cache" / "keep", "compiled") || !write(persistent / "keep", "persistent") ||
        !resource_submission(environment, root))
    {
        return false;
    }
    engine::editor::EditorSystem editor(environment);
    const auto settings = root / "settings/editor-settings.toml";
    if (!expect(!editor.initialize(config), "editor settings file is required"))
    {
        return false;
    }
    for (const char* invalid :
         {"", "[common]\n", "target_frame_rate = 60\n", "common = 60\n", "[common]\nunknown = 60\n",
          "[common]\ntarget_frame_rate = 0\n", "[common]\ntarget_frame_rate = 60.0\n"})
    {
        if (!expect(write(settings, invalid) && !editor.initialize(config),
                    "reject ungrouped, unknown, or invalid editor settings"))
        {
            return false;
        }
    }
    if (!expect(write(settings, "[common]\ntarget_frame_rate = -1\n") && editor.initialize(config),
                "accept the editor common settings table"))
    {
        return false;
    }
    editor.shutdown();
    project.shutdown();
    if (!expect(!environment.is_initialized(), "project lifetime covers environment use"))
    {
        return false;
    }
    environment.shutdown();
    environment.shutdown();
    if (!expect(!engine::App::is_initialized(), "repeated shutdown is safe") ||
        !expect(read(root / "cache" / "keep") == "compiled" &&
                    read(persistent / "keep") == "persistent",
                "closing environment preserves compiled and persistent resources") ||
        !expect(absent(root / "cache" / "editor-builtin"),
                "no byte snapshot directory is created") ||
        !expect(project.initialize(config), "restart project"))
    {
        return false;
    }
    engine::AppConfig external;
    external.application_name = "engine-environment-test";
    external.assets_path = root / "assets";
    external.persistent_data_path = workspace / "external-persistent";
    external.cache_path = workspace / "external-cache";
    if (!expect(engine::App::initialize(external), "initialize another App owner") ||
        !expect(!environment.initialize(), "reject a competing App owner"))
    {
        return false;
    }
    environment.shutdown();
    if (!expect(engine::App::is_initialized(), "failed initialization cannot close another owner"))
    {
        return false;
    }
    engine::App::shutdown();
    const auto invalid_persistent = workspace / "persistent-file";
    if (!write(invalid_persistent, "file"))
    {
        return false;
    }
    engine::editor::EditorEnvironment invalid(project, invalid_persistent);
    if (!expect(!invalid.initialize() && !engine::App::is_initialized(),
                "path failure leaves App unowned"))
    {
        return false;
    }
    {
        engine::editor::EditorEnvironment scoped(project, persistent);
        if (!expect(scoped.initialize(), "initialize scoped environment"))
        {
            return false;
        }
        engine::App::shutdown();
        if (!expect(!scoped.is_initialized(), "premature App shutdown invalidates environment"))
        {
            return false;
        }
    }
    return expect(!engine::App::is_initialized() && read(root / "cache" / "keep") == "compiled",
                  "destructor only releases owned environment state");
}
} // namespace

int main(int argc, char* argv[])
{
    if (argc != 2 || argv[1][0] == '\0')
    {
        return 2;
    }
    std::error_code error;
    const auto workspace = fs::absolute(fs::path(argv[1]) / "editor-environment-cases", error);
    if (error)
    {
        return 1;
    }
    fs::remove_all(workspace, error);
    if (error || !directory(workspace))
    {
        return 1;
    }
    const bool passed = run_cases(workspace);
    engine::App::shutdown();
    fs::remove_all(workspace, error);
    return passed && !error ? 0 : 1;
}
