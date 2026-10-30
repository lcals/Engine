if(ENGINE_ENABLE_RUNTIME_DEPS)
    find_package(Threads REQUIRED)
    engine_add_system(runtime log)
    target_link_libraries(engine_log PRIVATE engine::app Threads::Threads SDL3::SDL3)
endif()

if(ENGINE_ENABLE_RUNTIME_DEPS)
    engine_add_system(runtime performance)
    if(ENGINE_ENABLE_TRACY)
        target_compile_definitions(engine_performance PRIVATE ENGINE_ENABLE_TRACY=1)
        target_link_libraries(engine_performance PRIVATE Tracy::TracyClient)
    endif()

    engine_add_system(runtime window)
    target_link_libraries(
        engine_window
        PUBLIC engine::scene_data
        PRIVATE SDL3::SDL3)

    engine_add_system(runtime render)
    target_link_libraries(
        engine_render
        PRIVATE engine::window
                engine::world
                engine::resource
                engine::font
                engine::ui
                SDL3::SDL3
                bgfx::bgfx
                bgfx::bimg_encode
                bgfx::bx
                basisu::basisu_encoder)
    include(${PROJECT_SOURCE_DIR}/cmake/core/render_shaders.cmake)
    engine_embed_render_shaders(engine_render shadow)
    engine_configure_platform_graphics()

    engine_add_system(runtime resource)
    target_sources(
        engine_resource
        PRIVATE src/engine/runtime/resource/builtin_resources.cpp
                src/engine/runtime/utils/mesh_utils.cpp
                src/engine/runtime/resource/resource.cpp
                src/engine/runtime/resource/resource_codec.cpp
                src/engine/runtime/resource/codecs/resource_codecs.cpp
                src/engine/runtime/resource/codecs/binary_resource_codec.cpp
                src/engine/runtime/resource/codecs/scene_resource_codec.cpp
                src/engine/runtime/resource/codecs/mesh_resource_codec.cpp
                src/engine/runtime/resource/codecs/shader_resource_codec.cpp
                src/engine/runtime/resource/codecs/material_resource_codec.cpp
                src/engine/runtime/resource/codecs/texture_resource_codec.cpp
                src/engine/runtime/resource/ktx_texture.cpp
                src/engine/runtime/resource/codecs/audio_resource_codec.cpp
                src/engine/runtime/resource/codecs/font_resource_codec.cpp
                src/engine/runtime/resource/codecs/ui_resource_codec.cpp
                src/engine/runtime/resource/codecs/model_resource_codec.cpp
                src/engine/runtime/resource/codecs/skeleton_resource_codec.cpp
                src/engine/runtime/resource/codecs/animation_resource_codec.cpp
                src/engine/runtime/resource/resource_catalog.cpp
                src/engine/runtime/resource/virtual_file_system.cpp)
    target_include_directories(engine_resource PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    engine_embed_render_shaders(engine_resource cube font)
    target_link_libraries(
        engine_resource
        PUBLIC engine::scene_data
        PRIVATE flatbuffers::flatbuffers basisu::basisu_encoder SDL3::SDL3 bgfx::bimg)

    engine_add_system(runtime world)
    include(${PROJECT_SOURCE_DIR}/cmake/core/scene_schema.cmake)
    engine_add_scene_schema()
    target_sources(
        engine_world
        PRIVATE src/engine/runtime/scene/component.cpp
                src/engine/runtime/scene/component_registry.cpp
                src/engine/runtime/scene/collider.cpp
                src/engine/runtime/scene/character_motor.cpp
                src/engine/runtime/scene/rigid_body.cpp
                src/engine/runtime/scene/audio_source.cpp
                src/engine/runtime/scene/animator.cpp
                src/engine/runtime/scene/ui_component.cpp
                src/engine/runtime/scene/text_component.cpp
                src/engine/runtime/scene/model_instantiation.cpp
                src/engine/runtime/scene/audio_listener.cpp
                src/engine/runtime/scene/camera.cpp
                src/engine/runtime/scene/light.cpp
                src/engine/runtime/scene/environment_component.cpp
                src/engine/runtime/scene/post_process_component.cpp
                src/engine/runtime/scene/mesh_renderer.cpp
                src/engine/runtime/scene/material.cpp
                src/engine/runtime/scene/node.cpp
                src/engine/runtime/scene/transform.cpp
                src/engine/runtime/scene/scene.cpp)
    target_link_libraries(
        engine_world
        PUBLIC engine::scene_data
        PRIVATE flecs::flecs_static engine::resource engine::log)
    target_include_directories(engine_world PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")

    engine_add_system(runtime physics)
    target_link_libraries(engine_physics PRIVATE Jolt::Jolt engine::world engine::resource)
    engine_add_system(runtime audio)
    target_link_libraries(engine_audio PRIVATE engine::world engine::resource SDL3::SDL3)

    engine_add_system(runtime navigation)
    target_link_libraries(engine_navigation PRIVATE RecastNavigation::Recast
                                                    RecastNavigation::Detour)

    engine_add_system(runtime animation)
    target_link_libraries(engine_animation PRIVATE ozz_animation ozz_animation_offline ozz_geometry
                                                   engine::world engine::resource)
    # 上游子项目头文件按第三方处理，保留本项目的严格警告。
    get_target_property(_ozz_include ozz_animation INTERFACE_INCLUDE_DIRECTORIES)
    if(_ozz_include)
        target_include_directories(engine_animation SYSTEM PRIVATE ${_ozz_include})
    endif()

    engine_add_system(runtime font)
    target_link_libraries(
        engine_font
        PUBLIC engine::scene_data
        PRIVATE engine::resource engine::world bgfx::bgfx bgfx::bx)
    engine_add_system(runtime ui)
    target_sources(
        engine_ui
        PRIVATE src/engine/runtime/ui/rml_font.cpp
                src/engine/runtime/ui/rml_render.cpp
                src/engine/runtime/ui/rml_input.cpp)
    target_include_directories(engine_ui PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(
        engine_ui
        PRIVATE RmlUi::Core engine::font engine::resource engine::world engine::window
                SDL3::SDL3 bgfx::bgfx bgfx::bx basisu::basisu_encoder)
    engine_embed_render_shaders(engine_ui ui)

    set(ENGINE_RUNTIME_SYSTEM_TARGETS
        engine::performance
        engine::window
        engine::resource
        engine::world
        engine::physics
        engine::audio
        engine::navigation
        engine::animation
        engine::font
        engine::ui
        engine::render)

    add_library(engine_gameplay STATIC src/engine/runtime/gameplay/gameplay_session.cpp)
    add_library(engine::gameplay ALIAS engine_gameplay)
    target_link_libraries(
        engine_gameplay
        PUBLIC engine::core engine::scene_data
        PRIVATE engine::world engine::render engine::resource engine::physics engine::audio
                engine::animation engine::font engine::ui)
    target_link_libraries(engine_gameplay PRIVATE engine::app engine::log
                                                  nlohmann_json::nlohmann_json SDL3::SDL3)
    target_compile_definitions(engine_gameplay PRIVATE JSON_NOEXCEPTION=1)
    engine_apply_options(engine_gameplay)

    add_library(engine_runtime STATIC src/engine/runtime/app/runtime_host.cpp)
    add_library(engine::runtime ALIAS engine_runtime)
    target_link_libraries(
        engine_runtime
        PUBLIC engine::core engine::gameplay
        PRIVATE ${ENGINE_RUNTIME_SYSTEM_TARGETS})
    engine_apply_options(engine_runtime)
endif()
