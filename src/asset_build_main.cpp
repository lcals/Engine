#if ENGINE_HAS_ANIMATION_TOOLS
#include "plugins/editor/animation_tools/retarget_importer.hpp"
#endif
#if ENGINE_HAS_TERRAIN_EDITOR
#include "plugins/editor/terrain/terrain_codec.hpp"
#include "plugins/editor/terrain/terrain_importer.hpp"
#endif
#if ENGINE_HAS_DESTRUCTION_EDITOR
#include "plugins/editor/destruction/destruction_codec.hpp"
#include "plugins/editor/destruction/destruction_importer.hpp"
#endif
#if ENGINE_HAS_EFFECTS_EDITOR
#include "plugins/editor/effects/effect_codec.hpp"
#include "plugins/editor/effects/effect_importer.hpp"
#endif

#if ENGINE_HAS_CHARACTER_CONTROLLER_EDITOR
#include "plugins/editor/character_controller/character_controller_codec.hpp"
#endif
#if ENGINE_HAS_NAVIGATION_CROWD_EDITOR
#include "plugins/editor/navigation_crowd/navigation_agent_codec.hpp"
#endif
#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "plugins/editor/asset_build/asset_build_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"
#include "runtime/utils/path_utils.hpp"

#if ENGINE_HAS_ASSET_IMPORT_PLUGIN
#include "plugins/editor/asset_import/font_importer.hpp"
#include "plugins/editor/asset_import/ui_importer.hpp"
#include "plugins/editor/asset_import/json_importer.hpp"
#include "plugins/editor/asset_import/movie_importer.hpp"
#include "plugins/editor/asset_import/fbx_importer.hpp"
#include "plugins/editor/asset_import/usd_importer.hpp"
#endif

#if ENGINE_HAS_MODELING_IMPORTER
#include "plugins/editor/modeling/modeling_mesh_importer.hpp"
#endif

#include <SDL3/SDL_main.h>
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
              "--package [--manifest settings/<file>.toml] [--item <name> ...] "
              "[--target macos|ios|windows|android|linux]");
}
} // namespace

int main(int argc, char** argv)
{
    std::filesystem::path project;
    std::string_view target;
    std::filesystem::path manifest;
    bool package = false;
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
        else if (argument == "--package" && !package)
        {
            package = true;
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
    if (project.empty() || (!package && manifest.empty() && !items.empty()))
    {
        usage();
        return 2;
    }
    engine::ThreadSystem threads;
    engine::TaskSystem tasks(threads, {"Editor", 8, engine::TaskPriority::low});
    engine::editor::AssetPipelineSystem pipeline(tasks);
    const engine::EngineConfig config;
    if (!threads.initialize(config) || !tasks.initialize(config))
    {
        std::fprintf(stderr, "Task services initialization failed: %s %s\n",
                     threads.last_error().c_str(), tasks.last_error().c_str());
        return 1;
    }
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
#if ENGINE_HAS_NAVIGATION_CROWD_EDITOR
    if (!engine::plugins::navigation_crowd::register_navigation_agent_codec())
    {
        std::fputs("Cannot register navigation agent scene codec\n", stderr);
        return 1;
    }
#endif
#if ENGINE_HAS_EFFECTS_EDITOR
    std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> effect_importer =
        std::make_unique<engine::plugins::effects::EffectImporter>();
    if (!engine::plugins::effects::register_effect_codec() ||
        !pipeline.add_importer(effect_importer))
    {
        std::fputs("Cannot register effects importer and scene codec\n", stderr);
        return 1;
    }
#endif
#if ENGINE_HAS_TERRAIN_EDITOR
    std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> terrain_importer =
        std::make_unique<engine::plugins::terrain::TerrainImporter>();
    if (!engine::plugins::terrain::register_terrain_codec() ||
        !pipeline.add_importer(terrain_importer))
    {
        std::fputs("Cannot register terrain importer and scene codec\n", stderr);
        return 1;
    }
#endif
#if ENGINE_HAS_DESTRUCTION_EDITOR
    std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> destruction_importer =
        std::make_unique<engine::plugins::destruction::DestructionImporter>();
    if (!engine::plugins::destruction::register_destruction_codec() ||
        !pipeline.add_importer(destruction_importer))
    {
        std::fputs("Cannot register destruction importer and scene codec\n", stderr);
        return 1;
    }
#endif
#if ENGINE_HAS_ANIMATION_TOOLS
    std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> retarget_importer =
        std::make_unique<engine::plugins::animation_tools::RetargetImporter>();
    if (!pipeline.add_importer(retarget_importer))
    {
        return 1;
    }
#endif
#if ENGINE_HAS_ASSET_IMPORT_PLUGIN
    std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> font_importer =
        std::make_unique<engine::plugins::asset_import::FontImporter>();
    if (!pipeline.add_importer(font_importer))
    {
        std::fputs("Cannot register font importer\n", stderr);
        return 1;
    }
    std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> ui_importer =
        std::make_unique<engine::plugins::asset_import::UiImporter>();
    if (!pipeline.add_importer(ui_importer))
    {
        std::fputs("Cannot register ui importer\n", stderr);
        return 1;
    }
    std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> json_importer =
        std::make_unique<engine::plugins::asset_import::JsonImporter>();
    if (!pipeline.add_importer(json_importer))
    {
        std::fputs("Cannot register JSON importer\n", stderr);
        return 1;
    }
    std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> movie_importer =
        std::make_unique<engine::plugins::asset_import::MovieImporter>();
    if (!pipeline.add_importer(movie_importer))
    {
        std::fputs("Cannot register movie importer\n", stderr);
        return 1;
    }
    std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> fbx_importer =
        std::make_unique<engine::plugins::asset_import::FbxImporter>();
    if (!pipeline.add_importer(fbx_importer))
    {
        std::fputs("Cannot register FBX importer\n", stderr);
        return 1;
    }
    std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> usd_importer =
        std::make_unique<engine::plugins::asset_import::UsdImporter>();
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
    if (package || !manifest.empty())
    {
        engine::plugins::asset_build::AssetBuildSystem build(pipeline);
        if (!build.initialize(config) || !build.build(project, manifest, items, target))
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
