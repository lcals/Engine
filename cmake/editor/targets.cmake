if(ENGINE_ENABLE_EDITOR_DEPS)
    engine_add_system(editor project)
    # toml++ 的 formatter 在异常开关下具有不同布局；所有自有目标统一采用无异常头文件实现。
    # 不链接 vcpkg 的带异常二进制库，避免同名 formatter 符号混入同一进程。
    get_target_property(_project_toml_includes tomlplusplus::tomlplusplus
                        INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(engine_project SYSTEM PRIVATE ${_project_toml_includes})
    target_link_libraries(engine_project PRIVATE engine::log)
    target_compile_definitions(engine_project PRIVATE TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0)

    engine_add_system(editor editor src/engine/editor/editor_system.cpp)
    target_sources(
        engine_editor
        PRIVATE src/engine/editor/render/render_commands.cpp
                src/engine/editor/editor_environment.cpp
                src/engine/editor/commands/editor_commands.cpp
                src/engine/editor/commands/scene_commands.cpp
                src/engine/editor/commands/components/component_edit.cpp
                src/engine/editor/commands/components/collider_commands.cpp
                src/engine/editor/commands/components/rigid_body_commands.cpp
                src/engine/editor/commands/components/audio_source_commands.cpp
                src/engine/editor/commands/components/audio_listener_commands.cpp
                src/engine/editor/commands/editor_command_channel.cpp)
    target_include_directories(engine_editor PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    if(NOT ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
        target_sources(engine_editor PRIVATE src/engine/editor/asset_pipeline/project_lock.cpp)
    endif()
    target_link_libraries(
        engine_editor
        PUBLIC nlohmann_json::nlohmann_json
        PRIVATE engine::project
                engine::render
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
        PRIVATE src/engine/editor/asset_pipeline/asset_metadata.cpp
                src/engine/editor/asset_pipeline/asset_database.cpp
                src/engine/editor/asset_pipeline/asset_importer.cpp
                src/engine/editor/asset_pipeline/asset_importer_registry.cpp
                src/engine/editor/asset_pipeline/importer_utils.cpp
                src/engine/editor/asset_pipeline/asset_io.cpp
                src/engine/editor/asset_pipeline/asset_publish.cpp
                src/engine/editor/asset_pipeline/project_lock.cpp
                src/engine/editor/asset_pipeline/scene_importer.cpp
                src/engine/editor/asset_pipeline/components/scene_component_codec.cpp
                src/engine/editor/asset_pipeline/components/transform_codec.cpp
                src/engine/editor/asset_pipeline/components/collider_codec.cpp
                src/engine/editor/asset_pipeline/components/rigid_body_codec.cpp
                src/engine/editor/asset_pipeline/components/audio_source_codec.cpp
                src/engine/editor/asset_pipeline/components/audio_listener_codec.cpp
                src/engine/editor/asset_pipeline/components/camera_codec.cpp
                src/engine/editor/asset_pipeline/components/light_codec.cpp
                src/engine/editor/asset_pipeline/components/mesh_renderer_codec.cpp
                src/engine/editor/asset_pipeline/importers/mesh_importer.cpp
                src/engine/editor/asset_pipeline/importers/shader_importer.cpp
                src/engine/editor/asset_pipeline/importers/material_importer.cpp
                src/engine/editor/asset_pipeline/importers/scene_importer.cpp
                src/engine/editor/asset_pipeline/importers/texture_importer.cpp
                src/engine/editor/asset_pipeline/importers/audio_importer.cpp
                src/engine/editor/asset_pipeline/importers/script_importer.cpp
                src/engine/editor/asset_pipeline/importers/source_importer.cpp)
    target_include_directories(engine_asset_pipeline PRIVATE "${PROJECT_SOURCE_DIR}/src/engine")
    target_link_libraries(
        engine_asset_pipeline
        PUBLIC engine::scene_data
        PRIVATE engine::task
                engine::world
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
        target_sources(engine_editor PRIVATE src/engine/editor/scene/scene_persistence.cpp)
        target_include_directories(engine_editor SYSTEM PRIVATE ${_toml_includes})
        target_compile_definitions(engine_editor PRIVATE TOML_HEADER_ONLY=1 TOML_EXCEPTIONS=0)
        target_link_libraries(engine_editor PRIVATE engine::asset_pipeline)
    endif()

endif()
