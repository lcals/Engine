if(ENGINE_BUILD_APP AND ENGINE_ENABLE_EDITOR_DEPS)
    add_executable(editor_app src/editor_main.cpp)
    target_link_libraries(
        editor_app
        PRIVATE engine::editor
                engine::project
                engine::runtime
                engine::app
                engine::log
                SDL3::SDL3
                fmt::fmt)
    if(ENGINE_ENABLE_SCRIPT_RUNTIME)
        if(NOT ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
            message(FATAL_ERROR "Editor scripting requires ENGINE_ENABLE_ASSET_PIPELINE_DEPS")
        endif()
        target_link_libraries(editor_app PRIVATE engine::script engine::asset_pipeline)
    endif()
    engine_link_resource_plugin(editor_app)
    if(TARGET engine_modeling_editor)
        target_link_libraries(editor_app PRIVATE engine::modeling_editor)
        target_compile_definitions(editor_app PRIVATE ENGINE_HAS_MODELING_PLUGIN=1)
    else()
        target_compile_definitions(editor_app PRIVATE ENGINE_HAS_MODELING_PLUGIN=0)
    endif()
    engine_apply_options(editor_app)
endif()

if(ENGINE_BUILD_APP AND TARGET engine_asset_build)
    add_executable(asset_build_app src/asset_build_main.cpp)
    target_link_libraries(asset_build_app PRIVATE engine::asset_build engine::asset_pipeline
                                                  nlohmann_json::nlohmann_json)
    target_compile_definitions(asset_build_app PRIVATE JSON_NOEXCEPTION=1)
    engine_apply_options(asset_build_app)
endif()

if(ENGINE_BUILD_APP AND TARGET engine_project_mcp_tools)
    add_executable(mcp_app src/mcp_main.cpp)
    target_link_libraries(mcp_app PRIVATE engine::project_mcp_tools engine::asset_pipeline
                                          engine::asset_build engine::asset_sync SDL3::SDL3)
    if(TARGET engine_modeling_mcp_tools)
        target_link_libraries(mcp_app PRIVATE engine::modeling_mcp_tools)
        target_compile_definitions(mcp_app PRIVATE ENGINE_HAS_MODELING_PLUGIN=1)
    else()
        target_compile_definitions(mcp_app PRIVATE ENGINE_HAS_MODELING_PLUGIN=0)
    endif()
    engine_apply_options(mcp_app)
endif()
