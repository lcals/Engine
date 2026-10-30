function(engine_configure_mcp_plugin)
    engine_add_system(plugins/editor mcp)
    target_sources(engine_mcp PRIVATE src/engine/plugins/editor/mcp/mcp_schema.cpp)
    target_include_directories(engine_mcp PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_mcp PUBLIC nlohmann_json::nlohmann_json)
    target_compile_definitions(engine_mcp PUBLIC JSON_NOEXCEPTION=1)

    add_library(
        engine_project_mcp_tools STATIC
        src/engine/plugins/editor/mcp/project_mcp_tools.cpp
        src/engine/plugins/editor/mcp/project_resource_tools.cpp
        src/engine/plugins/editor/mcp/project_runtime_tools.cpp
        src/engine/plugins/editor/mcp/project_lifecycle_tools.cpp
        src/engine/plugins/editor/mcp/android_sync.cpp
        src/engine/plugins/editor/mcp/editor_mcp_tools.cpp
        src/engine/plugins/editor/mcp/physics_mcp_tools.cpp
        src/engine/plugins/editor/mcp/audio_mcp_tools.cpp
        src/engine/plugins/editor/mcp/animation_mcp_tools.cpp
        src/engine/plugins/editor/mcp/model_mcp_tools.cpp
        src/engine/plugins/editor/mcp/render_mcp_tools.cpp
        src/engine/plugins/editor/mcp/scene_preview_mcp_tools.cpp)
    add_library(engine::project_mcp_tools ALIAS engine_project_mcp_tools)
    target_include_directories(engine_project_mcp_tools PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(
        engine_project_mcp_tools
        PUBLIC engine::mcp
        PRIVATE engine::asset_pipeline engine::asset_build engine::asset_sync engine::resource
                SDL3::SDL3 Threads::Threads)
    engine_apply_options(engine_project_mcp_tools)

endfunction()

engine_register_plugin(
    NAME
    editor/mcp
    CONFIGURE
    engine_configure_mcp_plugin
    DEPENDS
    editor/asset_build
    editor/asset_sync
    OPTIONS
    ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
