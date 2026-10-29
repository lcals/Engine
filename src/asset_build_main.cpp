#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/utils/path_utils.hpp"

#include <cstdio>
#include <filesystem>
#include <string_view>

namespace
{
void usage()
{
    std::puts("Usage: asset_build_app --project <project-directory> "
              "[--target macos|ios|windows|android|linux]");
}
} // namespace

int main(int argc, char** argv)
{
    std::filesystem::path project;
    std::string_view target;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument(argv[index]);
        if (argument == "--help" && argc == 2)
        {
            usage();
            return 0;
        }
        if (argument == "--project" && index + 1 < argc && project.empty())
        {
            project = engine::PathUtils::from_utf8(argv[++index]);
        }
        else if (argument == "--target" && index + 1 < argc && target.empty())
        {
            target = argv[++index];
        }
        else
        {
            usage();
            return 2;
        }
    }
    if (project.empty())
    {
        usage();
        return 2;
    }
    engine::AssetPipelineSystem pipeline;
    const engine::EngineConfig config;
    if (!pipeline.initialize(config) || !pipeline.build_assets(project, target))
    {
        std::fprintf(stderr, "Asset build failed: %s\n", pipeline.last_error().c_str());
        return 1;
    }
    const auto& stats = pipeline.last_build();
    std::printf(
        "Asset build completed: imported=%zu reused=%zu removed=%zu threads=%zu revision=%s\n",
        stats.imported, stats.reused, stats.removed, stats.import_threads, stats.revision.c_str());
    return 0;
}
