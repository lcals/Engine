function(engine_configure_navigation_crowd_plugin)
    engine_add_system(plugins/runtime navigation_crowd)
    target_sources(engine_navigation_crowd
                   PRIVATE src/engine/plugins/runtime/navigation_crowd/navigation_agent.cpp)
    target_link_libraries(engine_navigation_crowd PUBLIC engine::world engine::navigation)
    target_link_libraries(engine_navigation_crowd PRIVATE engine::scene_ecs)
    engine_enable_profiling(engine_navigation_crowd)
endfunction()

engine_register_plugin(NAME runtime/navigation_crowd CONFIGURE
                       engine_configure_navigation_crowd_plugin OPTIONS ENGINE_ENABLE_RUNTIME_DEPS)
