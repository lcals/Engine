function(engine_configure_navigation_crowd_editor_plugin)
    add_library(engine_navigation_crowd_editor STATIC
                src/engine/plugins/editor/navigation_crowd/navigation_agent_codec.cpp)
    add_library(engine::navigation_crowd_editor ALIAS engine_navigation_crowd_editor)
    target_include_directories(engine_navigation_crowd_editor
                               PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    get_target_property(_navigation_crowd_toml_includes tomlplusplus::tomlplusplus
                        INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(engine_navigation_crowd_editor SYSTEM
                               PUBLIC ${_navigation_crowd_toml_includes})
    target_link_libraries(engine_navigation_crowd_editor PUBLIC engine::navigation_crowd
                                                                engine::asset_pipeline)
    target_compile_definitions(engine_navigation_crowd_editor PUBLIC TOML_HEADER_ONLY=1
                                                                     TOML_EXCEPTIONS=0)
    engine_apply_options(engine_navigation_crowd_editor)
endfunction()

engine_register_plugin(
    NAME
    editor/navigation_crowd
    CONFIGURE
    engine_configure_navigation_crowd_editor_plugin
    DEPENDS
    runtime/navigation_crowd
    OPTIONS
    ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
