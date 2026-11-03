if(ENGINE_ENABLE_RUNTIME_DEPS)
    add_library(engine_storage STATIC src/engine/runtime/storage/key_value_store.cpp)
    add_library(engine::storage ALIAS engine_storage)
    target_include_directories(engine_storage PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    engine_configure_platform_storage()
    target_link_libraries(
        engine_storage
        PUBLIC engine::core
        PRIVATE SDL3::SDL3 nlohmann_json::nlohmann_json)
    target_compile_definitions(engine_storage PRIVATE JSON_NOEXCEPTION=1)
    engine_apply_options(engine_storage)

    find_package(Threads REQUIRED)
    engine_add_system(runtime log)
    target_link_libraries(engine_log PRIVATE engine::app Threads::Threads SDL3::SDL3)
endif()

if(ENGINE_ENABLE_RUNTIME_DEPS)

    engine_add_system(runtime task)
    target_sources(engine_task PRIVATE src/engine/runtime/task/thread_system.cpp)
    target_link_libraries(engine_task PRIVATE TBB::tbb Threads::Threads)
    engine_enable_profiling(engine_task)
    engine_configure_platform_tasks()

    engine_add_system(runtime window)
    target_link_libraries(
        engine_window
        PUBLIC engine::scene_data
        PRIVATE SDL3::SDL3)

    engine_add_system(runtime input)
    target_sources(engine_input PRIVATE src/engine/runtime/input/input_devices.cpp
                                        src/engine/runtime/input/input_mapping.cpp)
    target_include_directories(engine_input PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(
        engine_input
        PUBLIC engine::scene_data
        PRIVATE engine::window SDL3::SDL3)

    engine_add_system(runtime render)
    engine_enable_profiling(engine_render)
    target_sources(
        engine_render
        PRIVATE src/engine/runtime/render/render_surface.cpp
                src/engine/runtime/render/render_extension.cpp
                src/engine/runtime/render/render_math.cpp
                src/engine/runtime/render/render_settings.cpp
                src/engine/runtime/render/renderer_callbacks.cpp
                src/engine/runtime/render/render_scene.cpp
                src/engine/runtime/render/scene_draw.cpp
                src/engine/runtime/render/scene_capture.cpp
                src/engine/runtime/render/passes/clustered_lighting_pass.cpp
                src/engine/runtime/render/passes/surface_pass.cpp
                src/engine/runtime/render/passes/screen_space_reflection_pass.cpp
                src/engine/runtime/render/passes/planar_reflection_pass.cpp
                src/engine/runtime/render/passes/planar_reflection_math.cpp
                src/engine/runtime/render/render_scene_resources.cpp
                src/engine/runtime/render/scene_gpu_resources.cpp
                src/engine/runtime/render/scene_render_query.cpp
                src/engine/runtime/render/resources/gpu_mesh.cpp
                src/engine/runtime/render/resources/gpu_geometry.cpp
                src/engine/runtime/render/resources/gpu_program.cpp
                src/engine/runtime/render/resources/gpu_texture.cpp
                src/engine/runtime/render/passes/shadow_frame.cpp
                src/engine/runtime/render/passes/shadow_pass.cpp
                src/engine/runtime/render/passes/skinning_pass.cpp
                src/engine/runtime/render/passes/ambient_occlusion_pass.cpp
                src/engine/runtime/render/passes/hdr_post_process_pass.cpp)
    target_include_directories(engine_render PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(
        engine_render
        PUBLIC engine::scene_data
        PRIVATE engine::window
                engine::task
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
    engine_embed_assao_shaders(engine_render)
    engine_embed_skinning_shader(engine_render)
    engine_embed_hdr_shaders(engine_render)
    engine_embed_clustered_shader(engine_render)
    engine_embed_ssr_shaders(engine_render)
    engine_configure_platform_graphics()

    engine_add_system(runtime resource)
    engine_enable_profiling(engine_resource)
    target_sources(
        engine_resource
        PRIVATE src/engine/runtime/resource/builtin_resources.cpp
                src/engine/runtime/utils/mesh_utils.cpp
                src/engine/runtime/resource/resource.cpp
                src/engine/runtime/resource/resource_codec.cpp
                src/engine/runtime/resource/codecs/resource_codecs.cpp
                src/engine/runtime/resource/codecs/binary_resource_codec.cpp
                src/engine/runtime/resource/codecs/scene_resource_codec.cpp
                src/engine/runtime/resource/codecs/prefab_resource_codec.cpp
                src/engine/runtime/resource/codecs/mesh_resource_codec.cpp
                src/engine/runtime/resource/codecs/shader_resource_codec.cpp
                src/engine/runtime/resource/codecs/material_resource_codec.cpp
                src/engine/runtime/resource/codecs/texture_resource_codec.cpp
                src/engine/runtime/resource/ktx_texture.cpp
                src/engine/runtime/resource/codecs/audio_resource_codec.cpp
                src/engine/runtime/resource/codecs/font_resource_codec.cpp
                src/engine/runtime/resource/codecs/light_probe_resource_codec.cpp
                src/engine/runtime/resource/codecs/ui_resource_codec.cpp
                src/engine/runtime/resource/codecs/model_resource_codec.cpp
                src/engine/runtime/resource/codecs/skeleton_resource_codec.cpp
                src/engine/runtime/resource/codecs/animation_resource_codec.cpp
                src/engine/runtime/resource/resource_catalog.cpp
                src/engine/runtime/resource/virtual_file_system.cpp)
    target_include_directories(engine_resource PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    engine_embed_render_shaders(engine_resource cube font surface water)
    target_link_libraries(
        engine_resource
        PUBLIC engine::scene_data
        PRIVATE engine::task flatbuffers::flatbuffers basisu::basisu_encoder SDL3::SDL3 bgfx::bimg)

    engine_add_system(runtime world)
    engine_enable_profiling(engine_world)
    include(${PROJECT_SOURCE_DIR}/cmake/core/scene_schema.cmake)
    engine_add_scene_schema()
    add_library(engine_scene_ecs INTERFACE)
    add_library(engine::scene_ecs ALIAS engine_scene_ecs)
    target_link_libraries(engine_scene_ecs INTERFACE engine::scene_data flecs::flecs_static)
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
                src/engine/runtime/scene/prefab_instantiation.cpp
                src/engine/runtime/scene/audio_listener.cpp
                src/engine/runtime/scene/camera.cpp
                src/engine/runtime/scene/camera_math.cpp
                src/engine/runtime/scene/light.cpp
                src/engine/runtime/scene/environment_component.cpp
                src/engine/runtime/scene/post_process_component.cpp
                src/engine/runtime/scene/planar_reflection_component.cpp
                src/engine/runtime/scene/light_probe_volume_component.cpp
                src/engine/runtime/scene/mesh_renderer.cpp
                src/engine/runtime/scene/material.cpp
                src/engine/runtime/scene/node.cpp
                src/engine/runtime/scene/transform.cpp
                src/engine/runtime/scene/scene.cpp)
    target_link_libraries(
        engine_world
        PUBLIC engine::scene_data
        PRIVATE engine::scene_ecs engine::resource engine::log bgfx::bx)
    target_include_directories(engine_world PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")

    engine_add_system(runtime physics)
    engine_enable_profiling(engine_physics)
    target_link_libraries(engine_physics PRIVATE Jolt::Jolt engine::world engine::resource engine::task)
    # Jolt's public job-system base is built without RTTI; the bridge must match it.
    if(MSVC)
        target_compile_options(engine_physics PRIVATE /GR-)
    else()
        target_compile_options(engine_physics PRIVATE -fno-rtti)
    endif()
    engine_add_system(runtime audio)
    engine_enable_profiling(engine_audio)
    target_link_libraries(engine_audio PRIVATE engine::world engine::resource SDL3::SDL3)

    engine_add_system(runtime navigation)
    engine_enable_profiling(engine_navigation)
    target_link_libraries(engine_navigation PRIVATE engine::task)
    target_link_libraries(
        engine_navigation
        PUBLIC engine::scene_data
        PRIVATE RecastNavigation::Recast RecastNavigation::Detour RecastNavigation::DetourCrowd)

    engine_add_system(runtime animation)
    engine_enable_profiling(engine_animation)
    target_include_directories(engine_animation PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(engine_animation PRIVATE engine::task ozz_animation ozz_animation_offline
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
    engine_enable_profiling(engine_ui)
    target_sources(
        engine_ui PRIVATE src/engine/runtime/ui/rml_font.cpp src/engine/runtime/ui/rml_render.cpp
                          src/engine/runtime/ui/rml_input.cpp)
    target_include_directories(engine_ui PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(
        engine_ui
        PRIVATE RmlUi::Core
                engine::font
                engine::resource
                engine::world
                engine::window
                SDL3::SDL3
                bgfx::bgfx
                bgfx::bx
                basisu::basisu_encoder)
    engine_embed_render_shaders(engine_ui ui)

    foreach(_ecs_target engine_render engine_physics engine_audio engine_animation engine_font engine_ui)
        target_link_libraries(${_ecs_target} PRIVATE engine::scene_ecs)
    endforeach()

    set(ENGINE_RUNTIME_SYSTEM_TARGETS
        engine::task
        engine::window
        engine::input
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
        PRIVATE engine::world
                engine::render
                engine::resource
                engine::physics
                engine::audio
                engine::animation
                engine::font
                engine::ui)
    target_link_libraries(engine_gameplay PRIVATE engine::app engine::log
                                                  nlohmann_json::nlohmann_json SDL3::SDL3)
    target_compile_definitions(engine_gameplay PRIVATE JSON_NOEXCEPTION=1)
    target_link_libraries(engine_gameplay PRIVATE engine::task)
    engine_enable_profiling(engine_gameplay)
    engine_apply_options(engine_gameplay)

    add_library(engine_runtime STATIC src/engine/runtime/app/runtime_host.cpp
        src/engine/runtime/app/runtime_settings.cpp)
    add_library(engine::runtime ALIAS engine_runtime)
    target_link_libraries(
        engine_runtime
        PUBLIC engine::core engine::gameplay
        PRIVATE ${ENGINE_RUNTIME_SYSTEM_TARGETS} nlohmann_json::nlohmann_json)
    target_compile_definitions(engine_runtime PRIVATE JSON_NOEXCEPTION=1)
    engine_apply_options(engine_runtime)
    engine_enable_profiling(engine_runtime)
endif()
