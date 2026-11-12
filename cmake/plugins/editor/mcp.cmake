function(engine_configure_mcp_plugin)
    add_library(
        engine_project_mcp_tools STATIC
        src/engine/plugins/editor/mcp/editor_mcp_endpoint.cpp
        src/engine/plugins/editor/mcp/project_mcp_tools.cpp
        src/engine/plugins/editor/mcp/project_resource_tools.cpp
        src/engine/plugins/editor/mcp/project_runtime_tools.cpp
        src/engine/plugins/editor/mcp/project_lifecycle_tools.cpp
        platforms/android/editor/plugins/mcp/android_sync.cpp
        platforms/ios/editor/plugins/mcp/ios_container.cpp
        platforms/ios/editor/plugins/mcp/ios_sync.cpp
        src/engine/plugins/editor/mcp/editor_mcp_tools.cpp
        src/engine/plugins/editor/mcp/editor_tool_registration.cpp
        src/engine/plugins/editor/mcp/physics_mcp_tools.cpp
        src/engine/plugins/editor/mcp/audio_mcp_tools.cpp
        src/engine/plugins/editor/mcp/animation_mcp_tools.cpp
        src/engine/plugins/editor/mcp/model_mcp_tools.cpp
        src/engine/plugins/editor/mcp/prefab_mcp_tools.cpp
        src/engine/plugins/editor/mcp/render_mcp_tools.cpp
        src/engine/plugins/editor/mcp/scene_preview_mcp_tools.cpp)
    add_library(engine::project_mcp_tools ALIAS engine_project_mcp_tools)
    if(ENGINE_ENABLE_TEST_SUPPORT)
        target_sources(engine_project_mcp_tools PRIVATE src/engine/plugins/editor/mcp/project_script_mcp_tools.cpp)
    endif()
    target_include_directories(engine_project_mcp_tools PRIVATE "${PROJECT_SOURCE_DIR}/src/engine"
        "${PROJECT_SOURCE_DIR}/platforms")
    target_compile_definitions(
        engine_project_mcp_tools
        PRIVATE ENGINE_NODE_EXECUTABLE="${ENGINE_NODE_EXECUTABLE}"
                ENGINE_SCRIPT_BUILDER="${PROJECT_SOURCE_DIR}/scripts/build_scripts.mjs")
    target_link_libraries(
        engine_project_mcp_tools
        PUBLIC engine::mcp engine::mcp_observation
        PRIVATE engine::asset_pipeline engine::asset_build engine::asset_sync engine::resource
                SDL3::SDL3 Threads::Threads unofficial::libimobiledevice::libimobiledevice)
    engine_apply_options(engine_project_mcp_tools)

endfunction()

engine_register_plugin(
    NAME
    editor/mcp
    CONFIGURE
    engine_configure_mcp_plugin
    DEPENDS
    runtime/mcp
    editor/asset_build
    editor/asset_sync
    OPTIONS
    ENGINE_ENABLE_ASSET_PIPELINE_DEPS
    VCPKG_FEATURES
    ios-device-sync)
