function(engine_configure_character_controller_editor_plugin)
    add_library(engine_character_controller_editor STATIC
                src/engine/plugins/editor/character_controller/character_controller_codec.cpp)
    add_library(engine::character_controller_editor ALIAS engine_character_controller_editor)
    target_include_directories(engine_character_controller_editor
                               PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    get_target_property(_controller_toml_includes tomlplusplus::tomlplusplus
                        INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(engine_character_controller_editor SYSTEM
                               PUBLIC ${_controller_toml_includes})
    target_link_libraries(
        engine_character_controller_editor
        PUBLIC engine::character_controller engine::asset_pipeline
        PRIVATE nlohmann_json::nlohmann_json)
    if(TARGET engine_editor)
        target_sources(
            engine_character_controller_editor
            PRIVATE src/engine/plugins/editor/character_controller/character_controller_commands.cpp
        )
        target_link_libraries(engine_character_controller_editor PRIVATE engine::editor)
    endif()
    target_compile_definitions(
        engine_character_controller_editor
        PUBLIC TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0
        PRIVATE JSON_NOEXCEPTION=1)
    engine_apply_options(engine_character_controller_editor)
    if("editor/mcp" IN_LIST ENGINE_SELECTED_PLUGINS)
        add_library(
            engine_character_controller_mcp_tools STATIC
            src/engine/plugins/editor/character_controller/character_controller_mcp_tools.cpp)
        add_library(engine::character_controller_mcp_tools ALIAS
                    engine_character_controller_mcp_tools)
        target_include_directories(engine_character_controller_mcp_tools
                                   PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_character_controller_mcp_tools
                              PUBLIC engine::project_mcp_tools)
        engine_apply_options(engine_character_controller_mcp_tools)
    endif()
endfunction()

engine_register_plugin(
    NAME
    editor/character_controller
    CONFIGURE
    engine_configure_character_controller_editor_plugin
    DEPENDS
    runtime/character_controller
    OPTIONS
    ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
