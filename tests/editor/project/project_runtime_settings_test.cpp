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
    if (argc < 2 || status ||
        !expect(!engine::editor::load_project_runtime_settings(directory.root, "windows", settings,
                                                               error) &&
                    settings.engine.window_title == "Engine Editor",
                "missing project settings fail without changing output"))
    {
        return 1;
    }
    const auto runtime = directory.root / "settings/runtime-engine.toml";
    const auto parallel = directory.root / "settings/runtime-parallel.toml";
    const auto copy_parallel = [&]()
    {
        std::filesystem::copy_file(
            std::filesystem::path(argv[1]) / "settings/runtime-parallel.toml", parallel,
            std::filesystem::copy_options::overwrite_existing, status);
        return !status;
    };
    if (!write(runtime, "[common]\nwindow_title=\"Configured game\"\nwidth=1600\nheight=900\n"
                        "target_frame_rate=45\n[platform.android]\ntarget_frame_rate=30\n") ||
        !copy_parallel() ||
        !write(directory.root / "settings/runtime-game.toml",
               "[common]\ngame_language=\"zh-CN\"\nstartup_movie=\"movies/intro.movie\"\n"
               "[platform.android]\nstartup_movie=\"movies/mobile.movie\"\n") ||
        !expect(engine::editor::load_project_runtime_settings(directory.root, "android", settings,
                                                              error) &&
                    settings.engine.window_title == "Configured game" &&
                    settings.engine.width == 1600 && settings.engine.target_frame_rate == 30 &&
                    settings.parallel.threads.worker_limit == 3 &&
                    settings.parallel.rendering.texture_batch_size == 2 &&
                    settings.game_language == "zh-CN" &&
                    settings.startup_movie == "movies/mobile.movie",
                "editor aggregates complete TOML modules and validates the runtime contract"))
    {
        return 1;
    }
    const char* invalid[] = {"invalid",
                             "version=2",
                             "worker_limit=3",
                             "[common.render]\ntexture_batch_size=65",
                             "[platform.other]",
                             "[common]\ncompletion_budget_seconds=nan",
                             "[common]\ndate=1979-05-27",
                             "[quality.low]\nworker_limit=1"};
    for (const auto text : invalid)
    {
        if (!write(parallel, text) ||
            !expect(!engine::editor::load_project_runtime_settings(directory.root, "android",
                                                                   settings, error) &&
                        !error.empty() && settings.parallel.threads.worker_limit == 3 &&
                        settings.engine.width == 1600,
                    "malformed TOML and invalid field values reject atomically"))
        {
            return 1;
        }
    }
    if (!write(runtime, "[common]\nwindow_title=\"Configured game\"\nwidth=1600\nheight=900\n"
                        "target_frame_rate=45\n[platform.android]\nwidth=0") ||
        !copy_parallel() ||
        !expect(engine::editor::load_project_runtime_settings(directory.root, "windows", settings,
                                                              error) &&
                    settings.engine.width == 1600 && settings.parallel.threads.worker_limit == 0 &&
                    settings.startup_movie == "movies/intro.movie",
                "unselected platforms do not contribute runtime fields") ||
        !expect(!engine::editor::load_project_runtime_settings(directory.root, "desktop", settings,
                                                               error),
                "platform must be an explicit supported build target"))
    {
        return 1;
    }
    for (int index = 1; index < argc; ++index)
    {
        for (const auto platform : {"windows", "macos", "linux", "android", "ios"})
        {
            settings = {};
            if (!engine::editor::load_project_runtime_settings(argv[index], platform, settings,
                                                               error))
            {
                std::fprintf(stderr, "%s: %s\n", argv[index], error.c_str());
                return 1;
            }
        }
    }
    return 0;
}
