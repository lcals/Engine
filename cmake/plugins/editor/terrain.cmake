function(engine_configure_terrain_editor_plugin)
    add_library(
        engine_terrain_editor STATIC
        src/engine/plugins/editor/terrain/terrain_importer.cpp
        src/engine/plugins/editor/terrain/terrain_source.cpp
        src/engine/plugins/editor/terrain/terrain_codec.cpp)
    add_library(engine::terrain_editor ALIAS engine_terrain_editor)
    target_include_directories(engine_terrain_editor PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    get_target_property(_terrain_toml_includes tomlplusplus::tomlplusplus
                        INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(engine_terrain_editor SYSTEM PUBLIC ${_terrain_toml_includes})
    target_link_libraries(engine_terrain_editor PUBLIC engine::terrain engine::asset_pipeline)
    if(TARGET engine_editor)
        target_sources(
            engine_terrain_editor
            PRIVATE src/engine/plugins/editor/terrain/terrain_commands.cpp
                    src/engine/plugins/editor/terrain/terrain_files.cpp
                    src/engine/plugins/editor/terrain/terrain_state_edit.cpp
                    src/engine/plugins/editor/terrain/terrain_settings_edit.cpp)
        target_link_libraries(engine_terrain_editor PRIVATE engine::editor
                                                            nlohmann_json::nlohmann_json SDL3::SDL3)
    endif()
    target_compile_definitions(
        engine_terrain_editor
        PUBLIC TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0
        PRIVATE JSON_NOEXCEPTION=1
                ENGINE_SCHEMA_SOURCE_DIR="${PROJECT_SOURCE_DIR}/include/schemas")
    engine_apply_options(engine_terrain_editor)
    if("editor/mcp" IN_LIST ENGINE_SELECTED_PLUGINS)
        add_library(engine_terrain_mcp_tools STATIC
                    src/engine/plugins/editor/terrain/terrain_mcp_tools.cpp)
        add_library(engine::terrain_mcp_tools ALIAS engine_terrain_mcp_tools)
        target_link_libraries(engine_terrain_mcp_tools PUBLIC engine::project_mcp_tools)
        engine_apply_options(engine_terrain_mcp_tools)
    endif()
endfunction()

engine_register_plugin(
    NAME
    editor/terrain
    CONFIGURE
    engine_configure_terrain_editor_plugin
    DEPENDS
    runtime/terrain
    OPTIONS
    ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
