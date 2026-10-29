#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "editor/asset_sync/asset_sync_system.hpp"
#include "plugins/mcp/mcp_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL_main.h>

#include <cstdio>
#include <filesystem>
#include <string_view>

int main(int argc, char** argv)
{
    std::filesystem::path project;
    std::filesystem::path runtime;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument(argv[index]);
        if (argument == "--help")
        {
            std::fputs("Usage: mcp_app --project <project-directory> "
                       "--runtime-dir <editor-persistent-directory>\n",
                       stdout);
            return 0;
        }
        if ((argument == "--project" || argument == "--runtime-dir") && index + 1 < argc)
        {
            auto& destination = argument == "--project" ? project : runtime;
            if (!destination.empty())
            {
                std::fputs("Duplicate MCP startup argument\n", stderr);
                return 2;
            }
            destination = engine::PathUtils::from_utf8(argv[++index]);
        }
        else
        {
            std::fputs("Expected --project and --runtime-dir; use --help\n", stderr);
            return 2;
        }
    }
    if (project.empty() || runtime.empty())
    {
        std::fputs("Both --project and --runtime-dir are required\n", stderr);
        return 2;
    }
    const engine::EngineConfig config{};
    engine::AssetPipelineSystem pipeline;
    engine::AssetSyncSystem sync;
    engine::McpSystem mcp(pipeline, sync, project, runtime);
    if (!pipeline.initialize(config) || !sync.initialize(config) || !mcp.initialize(config))
    {
        std::fputs("Cannot initialize MCP tool host\n", stderr);
        return 1;
    }
    return mcp.run_stdio() ? 0 : 1;
}
