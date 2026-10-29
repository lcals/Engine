#include "editor_system.hpp"
#include "engine.hpp"
#include "engine_config.hpp"
#include "project_system.hpp"

#include <SDL3/SDL_main.h>
#include <fmt/base.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <thread>

namespace
{

void print_usage()
{
    fmt::print("Usage: editor_app --project <project-directory> [--frames <positive-count>]\n");
}

} // namespace

int main(int argc, char* argv[])
{
    engine::EngineConfig config;
    config.window_title = "Engine Editor";
    std::string project_root;
    std::uint64_t frame_limit = 0;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument(argv[index]);
        if (argument == "--help" && argc == 2)
        {
            print_usage();
            return 0;
        }
        if (argument == "--project" && index + 1 < argc && project_root.empty())
        {
            project_root = argv[++index];
            if (project_root.empty())
            {
                print_usage();
                return 2;
            }
        }
        else if (argument == "--frames" && index + 1 < argc && frame_limit == 0)
        {
            const std::string_view value(argv[++index]);
            const auto result =
                std::from_chars(value.data(), value.data() + value.size(), frame_limit);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
                frame_limit == 0)
            {
                print_usage();
                return 2;
            }
        }
        else
        {
            print_usage();
            return 2;
        }
    }
    if (project_root.empty())
    {
        print_usage();
        return 2;
    }

    engine::ProjectSystem project(project_root);
    if (!project.initialize(config))
    {
        std::fputs("Editor project initialization failed\n", stderr);
        return 1;
    }
    auto& engine = engine::Engine::instance();
    if (!engine.initialize(config))
    {
        std::fputs("Engine initialization failed\n", stderr);
        return 1;
    }
    engine::EditorSystem editor(project);
    if (!editor.initialize(config))
    {
        std::fputs("Editor initialization failed\n", stderr);
        engine.shutdown();
        return 1;
    }
    auto previous_time = std::chrono::steady_clock::now();
    std::uint64_t frames = 0;
    int result = 0;
    for (;;)
    {
        const auto current_time = std::chrono::steady_clock::now();
        const double delta_time =
            std::chrono::duration<double>(current_time - previous_time).count();
        previous_time = current_time;
        const auto status = engine.tick(delta_time);
        if (status == engine::FrameStatus::exit_requested)
        {
            break;
        }
        if (status == engine::FrameStatus::failed)
        {
            std::fputs("Engine frame failed\n", stderr);
            result = 1;
            break;
        }
        if (status == engine::FrameStatus::paused)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }
        if (!editor.tick(delta_time))
        {
            std::fputs("Editor frame failed\n", stderr);
            result = 1;
            break;
        }
        ++frames;
        if (frame_limit != 0 && frames >= frame_limit)
        {
            engine.request_exit();
        }
    }
    editor.shutdown();
    engine.shutdown();
    project.shutdown();
    fmt::print("Editor stopped after {} rendered frames\n", frames);
    return result;
}
