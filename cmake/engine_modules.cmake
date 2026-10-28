# 每个系统独立编译，公开头文件搜索路径由所属目标传递。
function(engine_add_system name)
    add_library(engine_${name} STATIC "src/engine/${name}/${name}_system.cpp")
    add_library(engine::${name} ALIAS engine_${name})
    target_include_directories(engine_${name} PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include/engine/${name}>")
    target_link_libraries(engine_${name} PUBLIC engine::core)
    engine_apply_options(engine_${name})
endfunction()

if(ENGINE_ENABLE_V8)
    engine_add_system(script)
    target_link_libraries(engine_script PRIVATE engine::v8)
endif()

if(ENGINE_ENABLE_RUNTIME_DEPS)
    engine_add_system(window)
    target_link_libraries(engine_window PRIVATE SDL3::SDL3)

    engine_add_system(render)
    target_link_libraries(engine_render PRIVATE engine::window SDL3::SDL3 bgfx::bgfx)
    if(ANDROID)
        target_sources(engine_window PRIVATE platforms/android/native/android_surface.cpp)
        target_include_directories(engine_window PRIVATE "${PROJECT_SOURCE_DIR}/platforms/android/native")
        target_include_directories(engine_render PRIVATE "${PROJECT_SOURCE_DIR}/platforms/android/native")
        target_link_libraries(engine_render PRIVATE android)
    endif()

    engine_add_system(resource)
    target_link_libraries(engine_resource PRIVATE flatbuffers::flatbuffers basisu::basisu_encoder)

    engine_add_system(world)
    target_link_libraries(engine_world PRIVATE flecs::flecs_static)

    engine_add_system(physics)
    target_link_libraries(engine_physics PRIVATE Jolt::Jolt)

    engine_add_system(navigation)
    target_link_libraries(engine_navigation PRIVATE RecastNavigation::Recast RecastNavigation::Detour)

    engine_add_system(animation)
    target_link_libraries(engine_animation PRIVATE ozz_animation)
    # 上游子项目头文件按第三方处理，保留本项目的严格警告。
    get_target_property(_ozz_include ozz_animation INTERFACE_INCLUDE_DIRECTORIES)
    if(_ozz_include)
        target_include_directories(engine_animation SYSTEM PRIVATE ${_ozz_include})
    endif()

    engine_add_system(ui)
    target_link_libraries(engine_ui PRIVATE RmlUi::Core)

    set(ENGINE_RUNTIME_SYSTEM_TARGETS
        engine::window engine::resource engine::world engine::physics
        engine::navigation engine::animation engine::ui engine::render)

    add_library(engine_runtime STATIC src/engine/engine.cpp)
    add_library(engine::runtime ALIAS engine_runtime)
    target_link_libraries(engine_runtime PUBLIC engine::core PRIVATE ${ENGINE_RUNTIME_SYSTEM_TARGETS})
    target_compile_definitions(engine_runtime PRIVATE ENGINE_HAS_V8=$<BOOL:${ENGINE_ENABLE_V8}>)
    if(ENGINE_ENABLE_V8)
        target_link_libraries(engine_runtime PRIVATE engine::script)
    endif()
    engine_apply_options(engine_runtime)
endif()

if(ENGINE_ENABLE_EDITOR_DEPS)
    engine_add_system(project)
    target_link_libraries(engine_project PRIVATE tomlplusplus::tomlplusplus)
    target_compile_definitions(engine_project PRIVATE TOML_EXCEPTIONS=0)

    engine_add_system(editor)
    target_link_libraries(engine_editor PRIVATE engine::project imgui::imgui)
endif()

if(ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
    engine_add_system(asset_pipeline)
    target_link_libraries(engine_asset_pipeline PRIVATE usd Taskflow::Taskflow
        unofficial::sqlite3::sqlite3 efsw::efsw meshoptimizer::meshoptimizer)
    target_compile_definitions(engine_asset_pipeline PRIVATE TF_DISABLE_EXCEPTION_HANDLING=1)
endif()
