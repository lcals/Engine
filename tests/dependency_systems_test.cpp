#include "runtime/engine_config.hpp"
#include "runtime/system/system.hpp"

#if ENGINE_HAS_RUNTIME
#include "runtime/animation/animation_system.hpp"
#include "runtime/audio/audio_system.hpp"
#include "runtime/font/font_system.hpp"
#include "runtime/navigation/navigation_system.hpp"
#include "runtime/physics/physics_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/ui/ui_system.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"
#endif

#if ENGINE_HAS_EDITOR
#include "editor/editor_environment.hpp"
#include "editor/editor_system.hpp"
#include "editor/project/project_system.hpp"
#endif

#if ENGINE_HAS_ASSET_PIPELINE
#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#endif

#if ENGINE_TEST_SCRIPT_PLUGIN
#include "plugins/runtime/script/script_system.hpp"
#endif

#include <cstdio>

namespace
{

[[nodiscard]] bool check_lifecycle(engine::System& system, const char* name,
                                   const engine::EngineConfig& config)
{
    for (int iteration = 0; iteration < 2; ++iteration)
    {
        if (!system.initialize(config) || !system.is_initialized())
        {
            std::fprintf(stderr, "%s initialization failed on pass %d\n", name, iteration);
            return false;
        }
        if (!system.tick(1.0 / 60.0))
        {
            std::fprintf(stderr, "%s update failed\n", name);
            return false;
        }
        system.shutdown();
        system.shutdown();
        if (system.is_initialized() || system.tick(1.0 / 60.0))
        {
            std::fprintf(stderr, "%s remained active after shutdown\n", name);
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    engine::EngineConfig config;

#if ENGINE_HAS_RUNTIME
    engine::ResourceSystem resource;
    engine::WorldSystem world;
    engine::PhysicsSystem physics;
    engine::AudioSystem audio;
    engine::NavigationSystem navigation;
    engine::AnimationSystem animation;
    engine::WindowSystem ui_window;
    engine::FontSystem fonts;
    engine::UiSystem ui(ui_window, world, fonts);
    if (!check_lifecycle(resource, "ResourceSystem", config) ||
        !check_lifecycle(world, "WorldSystem", config) ||
        !check_lifecycle(physics, "PhysicsSystem", config) ||
        !check_lifecycle(audio, "AudioSystem", config) ||
        !check_lifecycle(navigation, "NavigationSystem", config) ||
        !check_lifecycle(animation, "AnimationSystem", config) ||
        !check_lifecycle(ui, "UiSystem", config))
    {
        return 1;
    }
#endif

#if ENGINE_HAS_EDITOR
    engine::editor::ProjectSystem project(ENGINE_TEST_PROJECT);
    if (!check_lifecycle(project, "ProjectSystem", config) || !project.initialize(config))
    {
        return 1;
    }
    engine::editor::EditorEnvironment environment(project,
                                                  std::filesystem::path(ENGINE_TEST_PROJECT) /
                                                      "cache" / "dependency-test-persistent");
    if (!environment.initialize())
    {
        return 1;
    }
    engine::editor::EditorSystem editor(environment);
    if (!check_lifecycle(editor, "EditorSystem", config))
    {
        return 1;
    }
    environment.shutdown();
    project.shutdown();
#endif

#if ENGINE_HAS_ASSET_PIPELINE
    engine::editor::AssetPipelineSystem asset_pipeline;
    if (!check_lifecycle(asset_pipeline, "AssetPipelineSystem", config))
    {
        return 1;
    }
#endif

#if ENGINE_TEST_SCRIPT_PLUGIN
    engine::plugins::script::ScriptSystem script;
    if (!check_lifecycle(script, "ScriptSystem", config))
    {
        return 1;
    }
#endif

    std::puts("Dependency systems initialized, updated, and shut down twice");
    return 0;
}
