#if ENGINE_HAS_TERRAIN_EDITOR
#include "plugins/editor/terrain/terrain_codec.hpp"
#include "plugins/editor/terrain/terrain_importer.hpp"
#endif
#if ENGINE_HAS_DESTRUCTION_EDITOR
#include "plugins/editor/destruction/destruction_codec.hpp"
#include "plugins/editor/destruction/destruction_importer.hpp"
#endif
#if ENGINE_HAS_TERRAIN_MCP
#include "plugins/editor/terrain/terrain_mcp_tools.hpp"
#endif
#if ENGINE_HAS_DESTRUCTION_MCP
#include "plugins/editor/destruction/destruction_mcp_tools.hpp"
#endif
#if ENGINE_HAS_EFFECTS_EDITOR
#include "plugins/editor/effects/effect_codec.hpp"
#include "plugins/editor/effects/effect_importer.hpp"
#endif
#if ENGINE_HAS_EFFECTS_MCP
#include "plugins/editor/effects/effect_mcp_tools.hpp"
#endif

#if ENGINE_HAS_CHARACTER_CONTROLLER_EDITOR
#include "plugins/editor/character_controller/character_controller_codec.hpp"
#endif
#if ENGINE_HAS_NAVIGATION_CROWD_EDITOR
#include "plugins/editor/navigation_crowd/navigation_agent_codec.hpp"
#endif
#if ENGINE_HAS_CHARACTER_CONTROLLER_MCP
#include "plugins/editor/character_controller/character_controller_mcp_tools.hpp"
#endif
#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "plugins/editor/asset_build/asset_build_system.hpp"
#include "plugins/editor/asset_sync/asset_sync_system.hpp"
#include "plugins/editor/mcp/editor_mcp_tools.hpp"
#include "plugins/editor/mcp/mcp_system.hpp"
#include "plugins/editor/mcp/project_mcp_tools.hpp"
#include "plugins/editor/mcp/render_mcp_tools.hpp"
#include "plugins/editor/mcp/scene_preview_mcp_tools.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"
#include "runtime/utils/path_utils.hpp"

#if ENGINE_HAS_FONT_IMPORTER
#include "plugins/editor/font_import/font_importer.hpp"
#endif
#if ENGINE_HAS_UI_IMPORTER
#include "plugins/editor/ui_import/ui_importer.hpp"
#endif
#if ENGINE_HAS_JSON_IMPORTER
#include "plugins/editor/json_import/json_importer.hpp"
#endif
#if ENGINE_HAS_FBX_IMPORTER
#include "plugins/editor/fbx_import/fbx_importer.hpp"
#endif
#if ENGINE_HAS_USD_IMPORTER
#include "plugins/editor/usd_import/usd_importer.hpp"
#endif

#if ENGINE_HAS_MODELING_IMPORTER
#include "plugins/editor/modeling/modeling_mesh_importer.hpp"
#endif

#if ENGINE_HAS_MODELING_PLUGIN
#include "plugins/editor/modeling/modeling_mcp_tools.hpp"
#endif
#if ENGINE_HAS_SCENE_BAKE
#include "plugins/editor/scene_bake/scene_bake_mcp_tools.hpp"
#endif

#include <SDL3/SDL_main.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string_view>
#include <utility>

int main(int argc, char** argv)
{
    std::filesystem::path project;
    engine::plugins::mcp::McpTarget target;
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
    engine::ThreadSystem threads;
    engine::TaskSystem tasks(threads, {"Editor", 4, engine::TaskPriority::low});
    engine::editor::AssetPipelineSystem pipeline(tasks);
    engine::plugins::asset_build::AssetBuildSystem builder(pipeline);
    engine::plugins::asset_sync::AssetSyncSystem sync;
    engine::plugins::mcp::ProjectMcpTools project_tools(pipeline, builder, sync, project, target);
    engine::plugins::mcp::EditorMcpTools editor_tools(project, target);
#if ENGINE_HAS_MODELING_PLUGIN
    engine::plugins::modeling::ModelingMcpTools modeling_tools(editor_tools);
#endif
    engine::plugins::mcp::RenderMcpTools render_tools(editor_tools);
    engine::plugins::mcp::ScenePreviewMcpTools preview_tools(editor_tools);
#if ENGINE_HAS_SCENE_BAKE
    engine::plugins::scene_bake::SceneBakeMcpTools scene_bake_tools(editor_tools);
#endif
    engine::plugins::mcp::McpSystem mcp;
    if (!threads.initialize(config) || !tasks.initialize(config))
    {
        std::fprintf(stderr, "Cannot initialize task services: %s %s\n",
                     threads.last_error().c_str(), tasks.last_error().c_str());
        return 1;
    }
    if (!pipeline.initialize(config))
    {
        std::fputs("Cannot initialize asset pipeline\n", stderr);
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
#if ENGINE_HAS_FONT_IMPORTER
    std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> font_importer =
        std::make_unique<engine::plugins::font_import::FontImporter>();
    if (!pipeline.add_importer(font_importer))
    {
        std::fputs("Cannot register font importer\n", stderr);
        return 1;
    }
#endif
#if ENGINE_HAS_UI_IMPORTER
    std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> ui_importer =
        std::make_unique<engine::plugins::ui_import::UiImporter>();
    if (!pipeline.add_importer(ui_importer))
    {
        std::fputs("Cannot register ui importer\n", stderr);
        return 1;
    }
#endif
#if ENGINE_HAS_JSON_IMPORTER
    std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> json_importer =
        std::make_unique<engine::plugins::json_import::JsonImporter>();
    if (!pipeline.add_importer(json_importer))
    {
        std::fputs("Cannot register JSON importer\n", stderr);
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
#if ENGINE_HAS_CHARACTER_CONTROLLER_MCP
    if (!engine::plugins::character_controller::register_controller_mcp_tools(mcp, editor_tools))
    {
        std::fputs("Cannot register character controller MCP tools\n", stderr);
        return 1;
    }
#endif
#if ENGINE_HAS_EFFECTS_MCP
    if (!engine::plugins::effects::register_effect_mcp_tools(mcp, editor_tools))
    {
        std::fputs("Cannot register effects MCP tools\n", stderr);
        return 1;
    }
#endif
#if ENGINE_HAS_TERRAIN_MCP
    if (!engine::plugins::terrain::register_terrain_mcp_tools(mcp, editor_tools))
    {
        std::fputs("Cannot register terrain MCP tools\n", stderr);
        return 1;
    }
#endif
#if ENGINE_HAS_DESTRUCTION_MCP
    if (!engine::plugins::destruction::register_destruction_mcp_tools(mcp, editor_tools))
    {
        std::fputs("Cannot register destruction MCP tools\n", stderr);
        return 1;
    }
#endif
    if (!builder.initialize(config) || !sync.initialize(config) || !project_tools.initialize() ||
        !project_tools.register_tools(mcp) || !editor_tools.register_tools(mcp) ||
        (!android && !render_tools.register_tools(mcp)) ||
        (!android && !preview_tools.register_tools(mcp)) ||
#if ENGINE_HAS_SCENE_BAKE
        (!android && !scene_bake_tools.register_tools(mcp)) ||
#endif
#if ENGINE_HAS_MODELING_PLUGIN
        (!android && !modeling_tools.register_tools(mcp)) ||
#endif
        !mcp.initialize(config))
    {
        std::fputs("Cannot initialize MCP tool host\n", stderr);
        return 1;
    }
    return mcp.run_stdio() ? 0 : 1;
}
