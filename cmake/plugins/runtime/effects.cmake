function(engine_configure_effects_plugin)
    engine_add_system(plugins/runtime effects
                      src/engine/plugins/runtime/effects/effect_system.cpp)
    target_sources(engine_effects PRIVATE src/engine/plugins/runtime/effects/codecs/effect_resource_codec.cpp)
    target_sources(
        engine_effects
        PRIVATE src/engine/plugins/runtime/effects/effect_component.cpp
                src/engine/plugins/runtime/effects/effect_validation.cpp
                src/engine/plugins/runtime/effects/effect_resource_path.cpp
                src/engine/plugins/runtime/effects/effect_resources.cpp
                src/engine/plugins/runtime/effects/effect_model_loader.cpp)
    target_include_directories(engine_effects PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_effects PUBLIC engine::world engine::render engine::resource
                         PRIVATE engine::effects_backend engine::scene_ecs)
    engine_embed_effects_shaders(engine_effects)
    engine_enable_profiling(engine_effects)
endfunction()

engine_register_plugin(NAME runtime/effects CONFIGURE engine_configure_effects_plugin
                       OPTIONS ENGINE_ENABLE_RUNTIME_DEPS)
