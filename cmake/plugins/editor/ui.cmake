function(engine_configure_ui_editor_plugin)
    add_library(engine_ui_editor STATIC
        src/engine/plugins/editor/ui/ui_editor_commands.cpp
        src/engine/plugins/editor/ui/ui_input.cpp
        src/engine/plugins/editor/ui/ui_source.cpp)
    add_library(engine::ui_editor ALIAS engine_ui_editor)
    target_include_directories(engine_ui_editor PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    get_target_property(_ui_editor_toml tomlplusplus::tomlplusplus INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(engine_ui_editor SYSTEM PRIVATE ${_ui_editor_toml})
    target_compile_definitions(engine_ui_editor PRIVATE TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0
        JSON_NOEXCEPTION=1)
    target_link_libraries(engine_ui_editor PUBLIC engine::editor engine::ui
        PRIVATE engine::asset_pipeline engine::asset_import engine::window engine::resource engine::task
        SDL3::SDL3)
    engine_apply_options(engine_ui_editor)
    if("editor/mcp" IN_LIST ENGINE_SELECTED_PLUGINS)
        add_library(engine_ui_editor_mcp_tools STATIC
            src/engine/plugins/editor/ui/ui_editor_mcp_tools.cpp)
        add_library(engine::ui_editor_mcp_tools ALIAS engine_ui_editor_mcp_tools)
        target_link_libraries(engine_ui_editor_mcp_tools PUBLIC engine::project_mcp_tools)
        target_compile_definitions(engine_ui_editor_mcp_tools PRIVATE JSON_NOEXCEPTION=1)
        engine_apply_options(engine_ui_editor_mcp_tools)
    endif()
endfunction()

engine_register_plugin(NAME editor/ui CONFIGURE engine_configure_ui_editor_plugin
    DEPENDS editor/asset_import OPTIONS ENGINE_ENABLE_RUNTIME_DEPS ENGINE_ENABLE_EDITOR_DEPS
        ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
