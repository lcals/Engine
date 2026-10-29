#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "editor/asset_sync/asset_sync_system.hpp"
#include "plugins/mcp/mcp_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL_main.h>

#include <cstdio>
#include <filesystem>
#include <string_view>
#include <utility>

int main(int argc, char** argv)
{
    std::filesystem::path project;
    engine::McpTarget target;
    bool adb_supplied = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument(argv[index]);
        if (argument == "--help")
        {
            std::fputs("Usage: mcp_app --project <project-directory> "
                       "(--runtime-dir <persistent-directory> | "
                       "--android-serial <serial> --android-package <package> [--adb <path>])\n",
                       stdout);
            return 0;
        }
        if ((argument == "--project" || argument == "--runtime-dir") && index + 1 < argc)
        {
            auto& destination = argument == "--project" ? project : target.runtime_dir;
            if (!destination.empty())
            {
                std::fputs("Duplicate MCP startup argument\n", stderr);
                return 2;
            }
            destination = engine::PathUtils::from_utf8(argv[++index]);
        }
        else if ((argument == "--android-serial" || argument == "--android-package") &&
                 index + 1 < argc)
        {
            auto& destination =
                argument == "--android-serial" ? target.android_serial : target.android_package;
            if (!destination.empty())
            {
                std::fputs("Duplicate Android target argument\n", stderr);
                return 2;
            }
            destination = argv[++index];
        }
        else if (argument == "--adb" && index + 1 < argc && !adb_supplied)
        {
            target.adb_executable = engine::PathUtils::from_utf8(argv[++index]);
            adb_supplied = true;
        }
        else
        {
            std::fputs("Invalid MCP startup arguments; use --help\n", stderr);
            return 2;
        }
    }
    const bool android = !target.android_serial.empty() || !target.android_package.empty();
    if (project.empty() ||
        (android && (!target.runtime_dir.empty() || target.android_serial.empty() ||
                     target.android_package.empty())) ||
        (!android && (target.runtime_dir.empty() || adb_supplied)))
    {
        std::fputs("Require --project and exactly one explicit local or Android target\n", stderr);
        return 2;
    }
    const engine::EngineConfig config{};
    engine::AssetPipelineSystem pipeline;
    engine::AssetSyncSystem sync;
    engine::McpSystem mcp(pipeline, sync, project, std::move(target));
    if (!pipeline.initialize(config) || !sync.initialize(config) || !mcp.initialize(config))
    {
        std::fputs("Cannot initialize MCP tool host\n", stderr);
        return 1;
    }
    return mcp.run_stdio() ? 0 : 1;
}
