function(engine_configure_resource_graph_plugin)
    engine_add_system(plugins/runtime resource_graph)
    target_link_libraries(engine_resource_graph PUBLIC engine::resource)
endfunction()

engine_register_plugin(NAME runtime/resource_graph CONFIGURE engine_configure_resource_graph_plugin
                       OPTIONS ENGINE_ENABLE_RUNTIME_DEPS)
