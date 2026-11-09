function(engine_configure_camera_plugin)
    engine_add_system(plugins/runtime camera)
    target_sources(engine_camera PRIVATE
        src/engine/plugins/runtime/camera/camera_controller.cpp
        src/engine/plugins/runtime/camera/camera_validation.cpp
        src/engine/plugins/runtime/camera/camera_type_registry.cpp
        src/engine/plugins/runtime/camera/camera_state_machine.cpp
        src/engine/plugins/runtime/camera/modules/fixed_camera_module.cpp
        src/engine/plugins/runtime/camera/modules/follow_camera_module.cpp
        src/engine/plugins/runtime/camera/modules/orbit_camera_module.cpp
        src/engine/plugins/runtime/camera/modules/aim_camera_module.cpp
        src/engine/plugins/runtime/camera/modules/lens_camera_module.cpp
        src/engine/plugins/runtime/camera/modules/noise_camera_module.cpp
        src/engine/plugins/runtime/camera/modules/collision_camera_module.cpp
        src/engine/plugins/runtime/camera/modules/bounds_camera_module.cpp
        src/engine/plugins/runtime/camera/conditions/parameter_camera_condition.cpp
        src/engine/plugins/runtime/camera/conditions/trigger_camera_condition.cpp
        src/engine/plugins/runtime/camera/conditions/target_camera_condition.cpp
        src/engine/plugins/runtime/camera/conditions/state_time_camera_condition.cpp)
    target_include_directories(engine_camera PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_camera PUBLIC engine::world engine::physics engine::scene_ecs)
endfunction()
engine_register_plugin(NAME runtime/camera CONFIGURE engine_configure_camera_plugin OPTIONS ENGINE_ENABLE_RUNTIME_DEPS)
