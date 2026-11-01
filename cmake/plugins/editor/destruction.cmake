function(engine_configure_destruction_editor_plugin)
    add_library(
        engine_destruction_editor STATIC
        src/engine/plugins/editor/destruction/destruction_authoring.cpp
        src/engine/plugins/editor/destruction/destruction_importer.cpp
        src/engine/plugins/editor/destruction/destruction_source.cpp
        src/engine/plugins/editor/destruction/destruction_codec.cpp)
    add_library(engine::destruction_editor ALIAS engine_destruction_editor)
    target_include_directories(engine_destruction_editor PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    get_target_property(_destruction_toml_includes tomlplusplus::tomlplusplus
                        INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(engine_destruction_editor SYSTEM
                               PUBLIC ${_destruction_toml_includes})
    target_link_libraries(
        engine_destruction_editor
        PUBLIC engine::destruction engine::asset_pipeline
        PRIVATE engine::blast_authoring)
    if(TARGET engine_editor)
        target_sources(
            engine_destruction_editor
            PRIVATE src/engine/plugins/editor/destruction/destruction_commands.cpp
                    src/engine/plugins/editor/destruction/destruction_files.cpp
                    src/engine/plugins/editor/destruction/destruction_state_edit.cpp
                    src/engine/plugins/editor/destruction/destruction_settings_edit.cpp)
        target_link_libraries(engine_destruction_editor
                              PRIVATE engine::editor nlohmann_json::nlohmann_json SDL3::SDL3)
    endif()
    target_compile_definitions(
        engine_destruction_editor
        PUBLIC TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0
        PRIVATE JSON_NOEXCEPTION=1
                ENGINE_SCHEMA_SOURCE_DIR="${PROJECT_SOURCE_DIR}/include/schemas/runtime")
    engine_apply_options(engine_destruction_editor)
    if("editor/mcp" IN_LIST ENGINE_SELECTED_PLUGINS)
        add_library(engine_destruction_mcp_tools STATIC
                    src/engine/plugins/editor/destruction/destruction_mcp_tools.cpp)
        add_library(engine::destruction_mcp_tools ALIAS engine_destruction_mcp_tools)
        target_include_directories(engine_destruction_mcp_tools
                                   PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_destruction_mcp_tools PUBLIC engine::project_mcp_tools)
        engine_apply_options(engine_destruction_mcp_tools)
    endif()
endfunction()

engine_register_plugin(
    NAME
    editor/destruction
    CONFIGURE
    engine_configure_destruction_editor_plugin
    DEPENDS
    runtime/destruction
    VCPKG_FEATURES
    destruction-authoring
    OPTIONS
    ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
