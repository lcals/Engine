function(engine_configure_character_controller_plugin)
    engine_add_system(plugins/runtime character_controller)
    target_sources(engine_character_controller
                   PRIVATE src/engine/plugins/runtime/character_controller/character_controller.cpp
                           src/engine/plugins/runtime/character_controller/character_camera.cpp
                           src/engine/plugins/runtime/character_controller/character_camera_system.cpp
                           src/engine/plugins/runtime/character_controller/character_visual.cpp)
    target_link_libraries(engine_character_controller PUBLIC engine::world engine::physics
                                                             engine::window)
endfunction()

engine_register_plugin(
    NAME runtime/character_controller CONFIGURE engine_configure_character_controller_plugin
    OPTIONS ENGINE_ENABLE_RUNTIME_DEPS)
