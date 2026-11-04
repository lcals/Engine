function(engine_configure_animation_tools_plugin)
    add_library(engine_animation_tools STATIC
        src/engine/plugins/editor/animation_tools/retarget_importer.cpp)
    add_library(engine::animation_tools ALIAS engine_animation_tools)
    target_include_directories(engine_animation_tools PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    get_target_property(_animation_tools_toml tomlplusplus::tomlplusplus INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(engine_animation_tools SYSTEM PRIVATE ${_animation_tools_toml})
    target_compile_definitions(engine_animation_tools PRIVATE TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0)
    target_link_libraries(engine_animation_tools PUBLIC engine::asset_pipeline
        PRIVATE engine::resource)
    engine_apply_options(engine_animation_tools)
    if(TARGET engine_editor)
        add_library(engine_animation_tools_editor STATIC
            src/engine/plugins/editor/animation_tools/animation_authoring_commands.cpp)
        add_library(engine::animation_tools_editor ALIAS engine_animation_tools_editor)
        target_include_directories(engine_animation_tools_editor PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_include_directories(engine_animation_tools_editor SYSTEM PRIVATE ${_animation_tools_toml})
        target_compile_definitions(engine_animation_tools_editor PRIVATE
            TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0 JSON_NOEXCEPTION=1)
        target_link_libraries(engine_animation_tools_editor PUBLIC engine::editor
            PRIVATE engine::asset_import engine_ufbx engine::asset_pipeline engine::resource SDL3::SDL3)
        engine_apply_options(engine_animation_tools_editor)
    endif()
    if("editor/mcp" IN_LIST ENGINE_SELECTED_PLUGINS)
        add_library(engine_animation_tools_mcp_tools STATIC
            src/engine/plugins/editor/animation_tools/animation_authoring_mcp_tools.cpp)
        add_library(engine::animation_tools_mcp_tools ALIAS engine_animation_tools_mcp_tools)
        target_link_libraries(engine_animation_tools_mcp_tools PUBLIC engine::project_mcp_tools)
        engine_apply_options(engine_animation_tools_mcp_tools)
    endif()
endfunction()

engine_register_plugin(NAME editor/animation_tools CONFIGURE engine_configure_animation_tools_plugin
    DEPENDS editor/asset_import
    OPTIONS ENGINE_ENABLE_RUNTIME_DEPS ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
