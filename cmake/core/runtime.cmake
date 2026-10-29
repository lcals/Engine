if(ENGINE_ENABLE_RUNTIME_DEPS)
    find_package(Threads REQUIRED)
    engine_add_system(runtime log)
    target_link_libraries(engine_log PRIVATE engine::app Threads::Threads SDL3::SDL3)
endif()

if(ENGINE_ENABLE_V8)
    if(ENGINE_ENABLE_SCRIPT_RUNTIME)
        engine_add_system(runtime script)
        # 每个绑定一个实现文件；新增类只需在注册表登记，不再维护第二份构建清单。
        file(GLOB _engine_script_binding_sources CONFIGURE_DEPENDS
             "${PROJECT_SOURCE_DIR}/src/engine/runtime/script/bindings/*_binding.cpp")
        target_sources(
            engine_script
            PRIVATE ${_engine_script_binding_sources}
                    src/engine/runtime/script/bindings/binding_context.cpp
                    src/engine/runtime/script/bindings/script_binding_registry.cpp)
        target_include_directories(engine_script PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_script PRIVATE engine::app engine::log SDL3::SDL3
                                                    nlohmann_json::nlohmann_json)
        target_compile_definitions(engine_script PRIVATE JSON_NOEXCEPTION=1)
    else()
        engine_add_system(runtime script src/engine/runtime/script/script_system_probe.cpp)
    endif()
    target_link_libraries(engine_script PRIVATE engine::v8)
endif()

if(ENGINE_ENABLE_RUNTIME_DEPS)
    engine_add_system(runtime window)
    target_link_libraries(engine_window PRIVATE SDL3::SDL3)

    engine_add_system(runtime render)
    target_link_libraries(
        engine_render
        PRIVATE engine::window
                engine::world
                engine::resource
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
                src/engine/runtime/resource/resource_catalog.cpp
                src/engine/runtime/resource/virtual_file_system.cpp)
    engine_embed_render_shaders(engine_resource)
    target_link_libraries(
        engine_resource
        PUBLIC engine::scene_data
        PRIVATE flatbuffers::flatbuffers basisu::basisu_encoder SDL3::SDL3)
    if(ENGINE_ENABLE_SCRIPT_RUNTIME)
        target_link_libraries(engine_script PRIVATE engine::resource)
    endif()

    engine_add_system(runtime world)
    include(${PROJECT_SOURCE_DIR}/cmake/core/scene_schema.cmake)
    engine_add_scene_schema()
    target_sources(
        engine_world
        PRIVATE src/engine/runtime/scene/component.cpp
                src/engine/runtime/scene/camera.cpp
                src/engine/runtime/scene/light.cpp
                src/engine/runtime/scene/mesh_renderer.cpp
                src/engine/runtime/scene/material.cpp
                src/engine/runtime/scene/node.cpp
                src/engine/runtime/scene/transform.cpp
                src/engine/runtime/scene/scene.cpp)
    target_link_libraries(
        engine_world
        PUBLIC engine::scene_data
        PRIVATE flecs::flecs_static engine::resource)
    if(ENGINE_ENABLE_SCRIPT_RUNTIME)
        target_link_libraries(engine_script PRIVATE engine::world)
    endif()

    engine_add_system(runtime physics)
    target_link_libraries(engine_physics PRIVATE Jolt::Jolt)

    engine_add_system(runtime navigation)
    target_link_libraries(engine_navigation PRIVATE RecastNavigation::Recast
                                                    RecastNavigation::Detour)

    engine_add_system(runtime animation)
    target_link_libraries(engine_animation PRIVATE ozz_animation)
    # 上游子项目头文件按第三方处理，保留本项目的严格警告。
    get_target_property(_ozz_include ozz_animation INTERFACE_INCLUDE_DIRECTORIES)
    if(_ozz_include)
        target_include_directories(engine_animation SYSTEM PRIVATE ${_ozz_include})
    endif()

    engine_add_system(runtime ui)
    target_link_libraries(engine_ui PRIVATE RmlUi::Core)

    set(ENGINE_RUNTIME_SYSTEM_TARGETS
        engine::window
        engine::resource
        engine::world
        engine::physics
        engine::navigation
        engine::animation
        engine::ui
        engine::render)

    add_library(engine_gameplay STATIC src/engine/runtime/gameplay/gameplay_session.cpp)
    add_library(engine::gameplay ALIAS engine_gameplay)
    target_link_libraries(
        engine_gameplay
        PUBLIC engine::core engine::scene_data
        PRIVATE engine::world engine::render engine::resource)
    target_compile_definitions(
        engine_gameplay PRIVATE ENGINE_HAS_SCRIPT_RUNTIME=$<BOOL:${ENGINE_ENABLE_SCRIPT_RUNTIME}>)
    if(ENGINE_ENABLE_SCRIPT_RUNTIME)
        target_link_libraries(engine_gameplay PRIVATE engine::script engine::app engine::log
                                                      nlohmann_json::nlohmann_json SDL3::SDL3)
        target_compile_definitions(engine_gameplay PRIVATE JSON_NOEXCEPTION=1)
    endif()
    engine_apply_options(engine_gameplay)

    add_library(engine_runtime STATIC src/engine/runtime/app/runtime_host.cpp)
    add_library(engine::runtime ALIAS engine_runtime)
    target_link_libraries(
        engine_runtime
        PUBLIC engine::core engine::gameplay
        PRIVATE ${ENGINE_RUNTIME_SYSTEM_TARGETS})
    target_compile_definitions(engine_runtime PRIVATE ENGINE_HAS_V8=$<BOOL:${ENGINE_ENABLE_V8}>)
    target_compile_definitions(
        engine_runtime PUBLIC ENGINE_HAS_SCRIPT_RUNTIME=$<BOOL:${ENGINE_ENABLE_SCRIPT_RUNTIME}>)
    if(ENGINE_ENABLE_V8)
        target_link_libraries(engine_runtime PRIVATE engine::script)
    endif()
    engine_apply_options(engine_runtime)
endif()
