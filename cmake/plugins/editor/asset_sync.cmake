function(engine_configure_asset_sync_plugin)
    engine_add_system(plugins/editor asset_sync)
    target_link_libraries(engine_asset_sync PRIVATE nlohmann_json::nlohmann_json engine::resource
                                                    SDL3::SDL3)
    target_compile_definitions(engine_asset_sync PRIVATE JSON_NOEXCEPTION=1)

endfunction()

engine_register_plugin(NAME editor/asset_sync CONFIGURE engine_configure_asset_sync_plugin OPTIONS
                       ENGINE_ENABLE_RUNTIME_DEPS)
