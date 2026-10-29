#include "engine_config.hpp"
#include "system.hpp"

#if ENGINE_HAS_RUNTIME
#include "animation_system.hpp"
#include "navigation_system.hpp"
#include "physics_system.hpp"
#include "resource_system.hpp"
#include "ui_system.hpp"
#include "world_system.hpp"
#endif

#if ENGINE_HAS_EDITOR
#include "editor_system.hpp"
#include "project_system.hpp"
#endif

#if ENGINE_HAS_ASSET_PIPELINE
#include "asset_pipeline_system.hpp"
#endif

#if ENGINE_HAS_V8
#include "script_system.hpp"
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
    engine::NavigationSystem navigation;
    engine::AnimationSystem animation;
    engine::UiSystem ui;
    if (!check_lifecycle(resource, "ResourceSystem", config) ||
        !check_lifecycle(world, "WorldSystem", config) ||
        !check_lifecycle(physics, "PhysicsSystem", config) ||
        !check_lifecycle(navigation, "NavigationSystem", config) ||
        !check_lifecycle(animation, "AnimationSystem", config) ||
        !check_lifecycle(ui, "UiSystem", config))
    {
        return 1;
    }
#endif

#if ENGINE_HAS_EDITOR
    engine::ProjectSystem project(ENGINE_TEST_PROJECT);
    if (!check_lifecycle(project, "ProjectSystem", config) || !project.initialize(config))
    {
        return 1;
    }
    engine::EditorSystem editor(project);
    if (!check_lifecycle(editor, "EditorSystem", config))
    {
        return 1;
    }
    project.shutdown();
#endif

#if ENGINE_HAS_ASSET_PIPELINE
    engine::AssetPipelineSystem asset_pipeline;
    if (!check_lifecycle(asset_pipeline, "AssetPipelineSystem", config))
    {
        return 1;
    }
#endif

#if ENGINE_HAS_V8
    engine::ScriptSystem script;
    if (!check_lifecycle(script, "ScriptSystem", config))
    {
        return 1;
    }
#endif

    std::puts("Dependency systems initialized, updated, and shut down twice");
    return 0;
}
