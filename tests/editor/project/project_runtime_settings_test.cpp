#include "resource_fixture.hpp"

#include "editor/project/runtime_settings_io.hpp"

#include <cstdio>
#include <fstream>
#include <string>

namespace
{

bool write(const std::filesystem::path& path, std::string_view text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    file.close();
    return static_cast<bool>(file);
}

bool expect(bool success, const char* message)
{
    if (!success)
    {
        std::fprintf(stderr, "project runtime settings: %s\n", message);
    }
    return success;
}

} // namespace

int main(int argc, char* argv[])
{
    test_support::ResourceFixture directory;
    std::error_code status;
    std::filesystem::create_directory(directory.root / "settings", status);
    engine::RuntimeSettings settings;
    settings.engine.window_title = "Engine Editor";
    std::string error;
    if (status || !expect(engine::editor::load_project_runtime_settings(directory.root, "desktop",
                                                                        settings, error) &&
                              settings.engine.window_title == "Engine Editor",
                          "missing project settings retain host defaults"))
    {
        return 1;
    }
    const auto runtime = directory.root / "settings/runtime.toml";
    const auto parallel = directory.root / "settings/parallel.toml";
    if (!write(runtime, "version=1\n[defaults.engine]\nwidth=1600\ntarget_frame_rate=45\n[profiles."
                        "mobile.engine]\ntarget_frame_rate=30\n") ||
        !write(parallel, "version=1\n[defaults.threads]\nworker_limit=4\n[profiles.mobile.threads]"
                         "\nworker_limit=2\n[overrides.render]\ntexture_batch_size=3\n") ||
        !expect(engine::editor::load_project_runtime_settings(directory.root, "mobile", settings,
                                                              error) &&
                    settings.engine.window_title == "Engine Editor" &&
                    settings.engine.width == 1600 && settings.engine.target_frame_rate == 30 &&
                    settings.parallel.threads.worker_limit == 2 &&
                    settings.parallel.render.texture_batch_size == 3,
                "editor aggregates TOML modules and reuses the runtime field contract"))
    {
        return 1;
    }
    const char* invalid[] = {"invalid",
                             "version=2",
                             "version=1\n[defaults]\nworker_limit=3",
                             "version=1\n[defaults.render]\ntexture_batch_size=65",
                             "version=1\n[profiles.other]",
                             "version=1\n[defaults]\ncompletion_budget_seconds=nan",
                             "version=1\n[defaults]\ndate=1979-05-27"};
    for (const auto text : invalid)
    {
        if (!write(parallel, text) ||
            !expect(!engine::editor::load_project_runtime_settings(directory.root, "mobile",
                                                                   settings, error) &&
                        !error.empty() && settings.parallel.threads.worker_limit == 2 &&
                        settings.engine.width == 1600,
                    "malformed TOML and invalid field values reject atomically"))
        {
            return 1;
        }
    }
    if (!write(runtime, "version=1\n[defaults.parallel.threads]\nworker_limit=3") ||
        !write(parallel, "version=1\n[defaults.threads]\nworker_limit=4") ||
        !expect(!engine::editor::load_project_runtime_settings(directory.root, "desktop", settings,
                                                               error),
                "two module files cannot silently redefine the same parallel layer"))
    {
        return 1;
    }
    for (int index = 1; index < argc; ++index)
    {
        for (const auto profile : {"desktop", "mobile"})
        {
            settings = {};
            if (!engine::editor::load_project_runtime_settings(argv[index], profile, settings,
                                                               error))
            {
                std::fprintf(stderr, "%s: %s\n", argv[index], error.c_str());
                return 1;
            }
        }
    }
    return 0;
}
