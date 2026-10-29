# 每个系统独立编译，公开头文件搜索路径由所属目标传递。
function(engine_add_system scope name)
    if(ARGC GREATER 2)
        set(_source "${ARGV2}")
    else()
        set(_source "src/engine/${scope}/${name}/${name}_system.cpp")
    endif()
    add_library(engine_${name} STATIC "${_source}")
    add_library(engine::${name} ALIAS engine_${name})
    target_link_libraries(engine_${name} PUBLIC engine::core)
    engine_apply_options(engine_${name})
endfunction()

if(ENGINE_ENABLE_RUNTIME_DEPS)
    find_package(Threads REQUIRED)
    if(ANDROID)
        add_library(engine_platform STATIC platforms/android/native/app.cpp)
    else()
        add_library(engine_platform STATIC platforms/desktop/app.cpp)
    endif()
    add_library(engine::app ALIAS engine_platform)
    target_compile_features(engine_platform PUBLIC cxx_std_20)
    target_link_libraries(
        engine_platform
        PUBLIC engine::core
        PRIVATE SDL3::SDL3)
    engine_apply_options(engine_platform)

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
                bgfx::bx
                basisu::basisu_encoder)
    include(${PROJECT_SOURCE_DIR}/cmake/render_shaders.cmake)
    if(ANDROID)
        target_sources(engine_window PRIVATE platforms/android/native/android_surface.cpp)
        target_include_directories(engine_window PRIVATE "${PROJECT_SOURCE_DIR}/platforms")
        target_include_directories(engine_render PRIVATE "${PROJECT_SOURCE_DIR}/platforms")
        target_link_libraries(engine_render PRIVATE android)
    endif()

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
    include(${PROJECT_SOURCE_DIR}/cmake/scene_schema.cmake)
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

if(ENGINE_ENABLE_EDITOR_DEPS)
    engine_add_system(editor project)
    target_link_libraries(engine_project PRIVATE tomlplusplus::tomlplusplus engine::log)
    target_compile_definitions(engine_project PRIVATE TOML_EXCEPTIONS=0)

    engine_add_system(editor editor src/engine/editor/editor_system.cpp)
    target_sources(
        engine_editor
        PRIVATE src/engine/editor/editor_environment.cpp
                src/engine/editor/commands/editor_commands.cpp
                src/engine/editor/commands/scene_commands.cpp
                src/engine/editor/commands/editor_command_channel.cpp)
    target_include_directories(engine_editor PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    if(NOT ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
        target_sources(engine_editor PRIVATE src/engine/editor/asset_pipeline/project_lock.cpp)
    endif()
    target_link_libraries(
        engine_editor
        PUBLIC nlohmann_json::nlohmann_json
        PRIVATE engine::project
                engine::gameplay
                engine::app
                engine::log
                engine::resource
                engine::world
                imgui::imgui
                SDL3::SDL3)
    target_compile_definitions(engine_editor PUBLIC JSON_NOEXCEPTION=1)
    target_compile_definitions(
        engine_editor
        PRIVATE ENGINE_HAS_ASSET_PIPELINE=$<BOOL:${ENGINE_ENABLE_ASSET_PIPELINE_DEPS}>)
endif()

if(ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
    engine_add_system(editor task)
    target_link_libraries(engine_task PRIVATE Taskflow::Taskflow Threads::Threads)
    target_compile_definitions(engine_task PRIVATE TF_DISABLE_EXCEPTION_HANDLING=1)

    engine_add_system(editor asset_pipeline)
    target_sources(
        engine_asset_pipeline
        PRIVATE src/engine/editor/asset_pipeline/asset_database.cpp
                src/engine/editor/asset_pipeline/asset_importers.cpp
                src/engine/editor/asset_pipeline/asset_io.cpp
                src/engine/editor/asset_pipeline/asset_publish.cpp
                src/engine/editor/asset_pipeline/project_lock.cpp
                src/engine/editor/asset_pipeline/scene_importer.cpp
                src/engine/editor/asset_pipeline/texture_importer.cpp)
    target_include_directories(engine_asset_pipeline PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(
        engine_asset_pipeline
        PUBLIC engine::scene_data
        PRIVATE engine::task
                engine::resource
                usd
                unofficial::sqlite3::sqlite3
                efsw::efsw
                basisu::basisu_encoder
                meshoptimizer::meshoptimizer
                nlohmann_json::nlohmann_json
                SDL3::SDL3)
    # vcpkg 的编译库使用异常 ABI；导入器按上游支持方式使用无异常头文件实现。
    get_target_property(_toml_includes tomlplusplus::tomlplusplus INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(engine_asset_pipeline SYSTEM PRIVATE ${_toml_includes})
    target_compile_definitions(
        engine_asset_pipeline
        PRIVATE TOML_HEADER_ONLY=1
                TOML_EXCEPTIONS=0
                JSON_NOEXCEPTION=1
                ENGINE_SHADERC_EXECUTABLE="${ENGINE_SHADERC_EXECUTABLE}"
                ENGINE_BGFX_SHADER_INCLUDE_DIR="${ENGINE_BGFX_SHADER_INCLUDE_DIR}"
                ENGINE_SCHEMA_SOURCE_DIR="${PROJECT_SOURCE_DIR}/include/schemas/runtime")
    find_program(ENGINE_NODE_EXECUTABLE NAMES node)
    if(NOT ENGINE_NODE_EXECUTABLE)
        set(ENGINE_NODE_EXECUTABLE "")
    endif()
    target_compile_definitions(
        engine_asset_pipeline
        PRIVATE ENGINE_NODE_EXECUTABLE="${ENGINE_NODE_EXECUTABLE}"
                ENGINE_SCRIPT_BUILDER="${PROJECT_SOURCE_DIR}/scripts/build_scripts.mjs")

    if(TARGET engine_editor)
        target_link_libraries(engine_editor PRIVATE engine::asset_pipeline)
    endif()

    engine_add_system(editor asset_build)
    target_include_directories(engine_asset_build PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_include_directories(engine_asset_build SYSTEM PRIVATE ${_toml_includes})
    target_compile_definitions(engine_asset_build PRIVATE TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0)
    target_link_libraries(engine_asset_build PRIVATE engine::asset_pipeline engine::resource
                                                     engine::core engine::scene_data)
endif()

if(ENGINE_ENABLE_SCRIPT_RUNTIME AND NOT ANDROID)
    engine_add_system(editor asset_sync)
    target_link_libraries(engine_asset_sync PRIVATE nlohmann_json::nlohmann_json engine::resource
                                                    SDL3::SDL3)
    target_compile_definitions(engine_asset_sync PRIVATE JSON_NOEXCEPTION=1)

    if(ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
        engine_add_system(plugins mcp)
        target_sources(engine_mcp PRIVATE src/engine/plugins/mcp/mcp_schema.cpp)
        target_include_directories(engine_mcp PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(engine_mcp PUBLIC nlohmann_json::nlohmann_json)
        target_compile_definitions(engine_mcp PUBLIC JSON_NOEXCEPTION=1)

        add_library(
            engine_project_mcp_tools STATIC
            src/engine/plugins/mcp/project_mcp_tools.cpp
            src/engine/plugins/mcp/project_resource_tools.cpp
            src/engine/plugins/mcp/project_runtime_tools.cpp
            src/engine/plugins/mcp/project_lifecycle_tools.cpp
            src/engine/plugins/mcp/android_sync.cpp
            src/engine/plugins/mcp/editor_mcp_tools.cpp)
        add_library(engine::project_mcp_tools ALIAS engine_project_mcp_tools)
        target_include_directories(engine_project_mcp_tools
                                   PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
        target_link_libraries(
            engine_project_mcp_tools
            PUBLIC engine::mcp
            PRIVATE engine::asset_pipeline engine::asset_build engine::asset_sync engine::resource
                    SDL3::SDL3 Threads::Threads)
        engine_apply_options(engine_project_mcp_tools)
    endif()
endif()
