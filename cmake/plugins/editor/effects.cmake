function(engine_configure_effects_editor_plugin)
    add_library(engine_effects_editor STATIC
                src/engine/plugins/editor/effects/effect_importer.cpp
                src/engine/plugins/editor/effects/effect_codec.cpp)
    add_library(engine::effects_editor ALIAS engine_effects_editor)
    get_target_property(_effects_toml_includes tomlplusplus::tomlplusplus
                        INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(engine_effects_editor SYSTEM PUBLIC ${_effects_toml_includes})
    target_link_libraries(engine_effects_editor
                          PUBLIC engine::effects engine::asset_pipeline
                          PRIVATE engine::effects_backend)
    if(TARGET engine_editor)
        target_sources(engine_effects_editor
                       PRIVATE src/engine/plugins/editor/effects/effect_commands.cpp)
        target_link_libraries(engine_effects_editor PRIVATE engine::editor nlohmann_json::nlohmann_json)
    endif()
    target_compile_definitions(engine_effects_editor PUBLIC TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0
                               PRIVATE JSON_NOEXCEPTION=1
                                       ENGINE_SCHEMA_SOURCE_DIR="${PROJECT_SOURCE_DIR}/include/schemas/runtime")
    engine_apply_options(engine_effects_editor)
    if("editor/mcp" IN_LIST ENGINE_SELECTED_PLUGINS)
        add_library(engine_effects_mcp_tools STATIC
                    src/engine/plugins/editor/effects/effect_mcp_tools.cpp)
        add_library(engine::effects_mcp_tools ALIAS engine_effects_mcp_tools)
        target_include_directories(engine_effects_mcp_tools
                                   PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_effects_mcp_tools PUBLIC engine::project_mcp_tools)
        engine_apply_options(engine_effects_mcp_tools)
    endif()
endfunction()

engine_register_plugin(NAME editor/effects CONFIGURE engine_configure_effects_editor_plugin
                       DEPENDS runtime/effects OPTIONS ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
