function(engine_configure_destruction_plugin)
    engine_add_system(plugins/runtime destruction)
    engine_enable_profiling(engine_destruction)
    target_sources(engine_destruction PRIVATE
        src/engine/plugins/runtime/destruction/destruction_component.cpp
        src/engine/plugins/runtime/destruction/destruction_graph.cpp)
    target_link_libraries(engine_destruction PUBLIC engine::world engine::physics engine::resource
                                            PRIVATE engine::blast)
endfunction()

engine_register_plugin(NAME runtime/destruction CONFIGURE engine_configure_destruction_plugin
                       OPTIONS ENGINE_ENABLE_RUNTIME_DEPS)
