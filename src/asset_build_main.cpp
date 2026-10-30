#if ENGINE_HAS_CHARACTER_CONTROLLER_EDITOR
#include "plugins/editor/character_controller/character_controller_codec.hpp"
#endif
#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "plugins/editor/asset_build/asset_build_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/utils/path_utils.hpp"

#if ENGINE_HAS_FBX_IMPORTER
#include "plugins/editor/fbx_import/fbx_importer.hpp"
#endif
#if ENGINE_HAS_USD_IMPORTER
#include "plugins/editor/usd_import/usd_importer.hpp"
#endif

#if ENGINE_HAS_MODELING_IMPORTER
#include "plugins/editor/modeling/modeling_mesh_importer.hpp"
#endif

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
void usage()
{
    std::puts("Usage: asset_build_app --project <project-directory> "
              "[--target macos|ios|windows|android|linux]\n"
              "       asset_build_app --project <project-directory> "
              "--manifest settings/<file>.toml [--item <name> ...]");
}
} // namespace

int main(int argc, char** argv)
{
    std::filesystem::path project;
    std::string_view target;
    std::filesystem::path manifest;
    std::vector<std::string> items;
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
        else if (argument == "--manifest" && index + 1 < argc && manifest.empty())
        {
            manifest = engine::PathUtils::from_utf8(argv[++index]);
        }
        else if (argument == "--item" && index + 1 < argc)
        {
            items.emplace_back(argv[++index]);
        }
        else
        {
            usage();
            return 2;
        }
    }
    if (project.empty() || (!manifest.empty() && !target.empty()) ||
        (manifest.empty() && !items.empty()))
    {
        usage();
        return 2;
    }
    engine::editor::AssetPipelineSystem pipeline;
    const engine::EngineConfig config;
    if (!pipeline.initialize(config))
    {
        std::fprintf(stderr, "Asset pipeline initialization failed: %s\n",
                     pipeline.last_error().c_str());
        return 1;
    }
#if ENGINE_HAS_CHARACTER_CONTROLLER_EDITOR
    if (!engine::plugins::character_controller::register_controller_codec())
    {
        std::fputs("Cannot register character controller scene codec\n", stderr);
        return 1;
    }
#endif
#if ENGINE_HAS_FBX_IMPORTER
    std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> fbx_importer =
        std::make_unique<engine::plugins::fbx_import::FbxImporter>();
    if (!pipeline.add_importer(fbx_importer))
    {
        std::fputs("Cannot register FBX importer\n", stderr);
        return 1;
    }
#endif
#if ENGINE_HAS_USD_IMPORTER
    std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> usd_importer =
        std::make_unique<engine::plugins::usd_import::UsdImporter>();
    if (!pipeline.add_importer(usd_importer))
    {
        std::fputs("Cannot register USD importer\n", stderr);
        return 1;
    }
#endif
#if ENGINE_HAS_MODELING_IMPORTER
    std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> importer =
        std::make_unique<engine::plugins::modeling::ModelingMeshImporter>();
    if (!pipeline.set_importer(importer))
    {
        std::fputs("Cannot register modeling importer\n", stderr);
        return 1;
    }
#endif
    if (!manifest.empty())
    {
        engine::plugins::asset_build::AssetBuildSystem build(pipeline);
        if (!build.initialize(config) || !build.build(project, manifest, items))
        {
            std::fprintf(stderr, "Resource build failed: %s\n", build.last_error().c_str());
            return 1;
        }
        const auto& stats = build.last_build();
        const nlohmann::json receipt = {{"status", "ok"},
                                        {"output", engine::PathUtils::to_utf8(stats.output_path)},
                                        {"targetPlatform", pipeline.catalog().target_platform},
                                        {"resourceCount", stats.resource_count},
                                        {"packageCount", stats.package_count},
                                        {"reusedPackageCount", stats.reused_package_count},
                                        {"revision", stats.revision}};
        std::puts(receipt.dump().c_str());
        return 0;
    }
    if (!pipeline.build_assets(project, target))
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
