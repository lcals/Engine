function(engine_configure_asset_build_plugin)
    get_target_property(_toml_includes tomlplusplus::tomlplusplus INTERFACE_INCLUDE_DIRECTORIES)
    engine_add_system(plugins/editor asset_build)
    target_include_directories(engine_asset_build PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_include_directories(engine_asset_build SYSTEM PRIVATE ${_toml_includes})
    target_compile_definitions(engine_asset_build PRIVATE TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0)
    target_link_libraries(engine_asset_build PRIVATE engine::asset_pipeline engine::resource
                                                     engine::core engine::scene_data)
endfunction()

engine_register_plugin(NAME editor/asset_build CONFIGURE engine_configure_asset_build_plugin
                       OPTIONS ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
