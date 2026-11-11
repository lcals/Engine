function(engine_configure_character_controller_plugin)
    engine_add_system(plugins/runtime character_controller)
    target_sources(engine_character_controller
                   PRIVATE src/engine/plugins/runtime/character_controller/character_controller.cpp
                           src/engine/plugins/runtime/character_controller/character_camera.cpp
                           src/engine/plugins/runtime/character_controller/character_camera_system.cpp
                           src/engine/plugins/runtime/character_controller/character_visual.cpp
                           src/engine/plugins/runtime/character_controller/character_camera_parameter_system.cpp
                           src/engine/plugins/runtime/character_controller/character_ability_registry.cpp
                           src/engine/plugins/runtime/character_controller/abilities/sprint_ability.cpp
                           src/engine/plugins/runtime/character_controller/abilities/jump_ability.cpp
                           src/engine/plugins/runtime/character_controller/abilities/crouch_ability.cpp)
    target_link_libraries(engine_character_controller PUBLIC engine::world engine::physics engine::camera engine::scene_ecs)
    target_include_directories(engine_character_controller PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_character_controller PRIVATE engine::scene_ecs)
endfunction()

engine_register_plugin(
    NAME runtime/character_controller CONFIGURE engine_configure_character_controller_plugin
    DEPENDS runtime/camera
    OPTIONS ENGINE_ENABLE_RUNTIME_DEPS)
