function(engine_configure_animation_retarget_plugin)
    add_library(engine_animation_retarget STATIC
        src/engine/plugins/runtime/animation_retarget/animation_retarget.cpp)
    add_library(engine::animation_retarget ALIAS engine_animation_retarget)
    target_link_libraries(engine_animation_retarget PUBLIC engine::resource PRIVATE bgfx::bx)
    engine_apply_options(engine_animation_retarget)
endfunction()

engine_register_plugin(NAME runtime/animation_retarget CONFIGURE
    engine_configure_animation_retarget_plugin OPTIONS ENGINE_ENABLE_RUNTIME_DEPS)
